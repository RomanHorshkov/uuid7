/**
 * @file uuid7.c
 * @brief UUIDv7 generator implementation with monotonic sequence and random
 *        tail generation.
 *
 * This module produces RFC-v7 style UUIDs (time-ordered) with the following
 * characteristics and guarantees:
 *
 * - Layout: 16 bytes where bytes 0..5 contain a 48-bit unix millisecond
 *   timestamp (big-endian). Bytes 6..7 contain a 12-bit sequence and the
 *   4-bit version (7). Bytes 8..15 contain the variant and a random tail.
 * - Monotonicity: generated values are strictly non-decreasing when observed
 *   as (timestamp, sequence) pairs. A global atomic 64-bit word stores the
 *   last used (ms,seq) packed as (ms << 12) | seq. A CAS loop reserves the
 *   next pair to ensure uniqueness across threads in the same process.
 * 
 * Sequence policy:
 *
 * - When real time moves to a new millisecond, rand_a/seq starts at 0.
 * - If multiple UUIDs are generated in the same millisecond, seq increments.
 * - If the wall clock moves backward, the generator continues from the last
 *   reserved logical state.
 * - If seq reaches 4095 and another UUID is needed before time advances, the
 *   logical millisecond is advanced by 1 and seq restarts at 0.
 *
 * The sequence is deterministic. Entropy is provided by rand_b.
 *
 * Thread-safety: `uuid7_gen()` is safe for concurrent calls thanks to the
 * atomic CAS loop protecting `g_v7_state`.
 *
 * Security note: the configured RNG should provide cryptographically secure
 * random bytes for rand_b.
 *
 * @author  Roman Horshkov <https://github.com/RomanHorshkov>
 * @date    may 2026
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#    include <sys/random.h>
#endif

#include "uuid7.h"

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/**
 * @brief Width in bits of the UUIDv7 `rand_a` / sequence field stored in the
 *        internal monotonic state word.
 *
 * The generator keeps its monotonic reservation state in a single 64-bit
 * integer with this logical layout:
 *
 * - bits `[63:12]`: Unix timestamp in milliseconds
 * - bits `[11:0]` : 12-bit sequence / `rand_a` value
 *
 * This constant defines the width of the low-order sequence field. It is used
 * by the packing, unpacking, and masking helpers below.
 *
 * @note A value of `12` matches the UUIDv7 `rand_a` field width defined by the
 *       format used in this module.
 */
#define V7_SEQ_BITS         12u

/**
 * @brief Bitmask for extracting or constraining the low 12-bit sequence field.
 *
 * Expands to a mask with the lowest `V7_SEQ_BITS` bits set to `1`. With the
 * current configuration this is equivalent to `0x0FFF`.
 *
 * Typical uses:
 *
 * - keep only the valid low 12 bits of a candidate sequence value
 * - extract the stored sequence from a packed state word
 *
 * @note Any bits above bit 11 are cleared when this mask is applied.
 */
#define V7_SEQ_MASK         ((UINT64_C(1) << V7_SEQ_BITS) - UINT64_C(1))

/**
 * @brief Left-shift, in bits, applied to the millisecond timestamp when
 *        packing the internal monotonic state word.
 *
 * Since the lower `V7_SEQ_BITS` bits are reserved for the sequence field, the
 * timestamp is shifted left by exactly that amount so both values occupy
 * disjoint bit ranges inside one 64-bit integer.
 */
#define V7_MS_SHIFT         V7_SEQ_BITS

/**
 * @brief Pack a millisecond timestamp and 12-bit sequence into one 64-bit
 *        monotonic state word.
 *
 * Resulting bit layout:
 *
 * - upper bits: `ms`
 * - lower 12 bits: `seq & V7_SEQ_MASK`
 *
 * Expanded form:
 * `(((uint64_t)(ms) << V7_MS_SHIFT) | ((uint64_t)(seq) & V7_SEQ_MASK))`
 *
 * @param ms
 *     Unix timestamp in milliseconds. It is cast to `uint64_t` before being
 *     shifted into the upper portion of the packed word.
 * @param seq
 *     Sequence / `rand_a` candidate. Only its low 12 bits are preserved; any
 *     higher bits are discarded by `V7_SEQ_MASK`.
 *
 * @return 64-bit packed value suitable for atomic comparison and storage in
 *         `g_v7_state`.
 *
 * @note This macro packs the module's internal `(ms, seq)` reservation state.
 *       It does not by itself produce the final 16-byte UUID representation.
 */
#define V7_PACK(ms, seq)    (((uint64_t)(ms) << V7_MS_SHIFT) | ((uint64_t)(seq) & V7_SEQ_MASK))

/**
 * @brief Extract the millisecond timestamp from a packed monotonic state word.
 *
 * This reverses the timestamp portion of `V7_PACK()` by shifting the input
 * right by `V7_MS_SHIFT` bits and discarding the low sequence field.
 *
 * @param word
 *     Packed 64-bit monotonic state value produced by `V7_PACK()`.
 *
 * @return The unpacked Unix timestamp in milliseconds as `uint64_t`.
 */
#define V7_UNPACK_MS(word)  ((uint64_t)(word) >> V7_MS_SHIFT)

/**
 * @brief Extract the low 12-bit sequence field from a packed monotonic state
 *        word.
 *
 * This reverses the sequence portion of `V7_PACK()` by masking the low
 * `V7_SEQ_BITS` bits and casting the result to `uint16_t`.
 *
 * @param word
 *     Packed 64-bit monotonic state value produced by `V7_PACK()`.
 *
 * @return The unpacked 12-bit sequence / `rand_a` value as `uint16_t`.
 */
#define V7_UNPACK_SEQ(word) ((uint16_t)((word) & V7_SEQ_MASK))

/**
 * @brief Generic 8-bit mask used when isolating a single byte from a wider
 *        integer value.
 *
 * This constant is applied after right-shifting multi-byte fields so the
 * result is truncated to one octet before storing into the output UUID byte
 * array.
 */
#define V7_BYTE_MASK        0xFFu

/**
 * @brief Extract one big-endian byte from the 48-bit Unix millisecond
 *        timestamp used in the UUIDv7 output layout.
 *
 * The UUID stores the timestamp in bytes `0..5` in network order
 * (most-significant byte first). This macro selects byte @p n from a
 * timestamp value by shifting the requested octet into the low 8 bits and
 * masking with `V7_BYTE_MASK`.
 *
 * Byte index mapping:
 *
 * - `n = 0`: most-significant timestamp byte
 * - `n = 5`: least-significant timestamp byte
 *
 * @param ms
 *     Unix timestamp in milliseconds.
 * @param n
 *     Byte index in the inclusive range `[0..5]`.
 *
 * @return Selected timestamp byte as `uint8_t`.
 *
 * @note Supplying an index outside `[0..5]` is a caller error.
 */
#define V7_MS_BYTE(ms, n)   ((uint8_t)((((uint64_t)(ms)) >> (8 * (5 - (n)))) & V7_BYTE_MASK))

/**
 * @brief Bit shift used to obtain the high 4 bits of the 12-bit `rand_a`
 *        sequence field.
 *
 * The UUIDv7 layout splits `rand_a` across two bytes:
 *
 * - `rand_a[11:8]` is stored in the low nibble of byte 6
 * - `rand_a[7:0]` is stored in byte 7
 *
 * Shifting the 12-bit sequence right by 8 positions exposes the upper nibble
 * before applying `V7_SEQ_HIGH_MASK`.
 */
#define V7_SEQ_HIGH_SHIFT   8u

/**
 * @brief Mask for the high 4-bit nibble of the 12-bit `rand_a` sequence field
 *        after it has been shifted down to the low bits.
 *
 * With the current layout this mask equals `0x0F` and is used when composing
 * UUID byte 6 together with the version nibble.
 */
#define V7_SEQ_HIGH_MASK    0x0Fu

/**
 * @brief Mask for the low 8 bits of the 12-bit `rand_a` sequence field.
 *
 * This mask isolates `rand_a[7:0]`, which are written directly into UUID byte
 * 7.
 */
#define V7_SEQ_LOW_MASK     0xFFu

/**
 * @brief Mask for the low 6 payload bits of UUID byte 8 after reserving the
 *        top 2 bits for the RFC variant.
 *
 * UUID byte 8 is formed as:
 *
 * - bits `[7:6]`: variant bits (`10`)
 * - bits `[5:0]`: high 6 bits of `rand_b`
 *
 * This mask keeps only the payload portion that may coexist with the variant
 * marker in the same byte.
 */
#define V7_RB0_LOW6_MASK    0x3Fu

/**
 * @brief Pre-encoded RFC variant bits for UUID byte 8.
 *
 * The UUID variant required by RFC 4122 / RFC 9562 is binary `10` in the two
 * most-significant bits of byte 8. OR-ing with this value sets bit 7 and
 * leaves bit 6 cleared.
 */
#define V7_VARIANT_TOP      0x80u

/**
 * @brief Number of random-tail bytes stored in the low 64 bits of the UUID
 *        assembly buffer before variant adjustment.
 *
 * The implementation samples 8 random bytes, then overlays the variant bits in
 * the first of those bytes when constructing bytes `8..15`.
 */
#define V7_RB_BYTES         8u

/**
 * @brief Number of bytes occupied by the UUIDv7 Unix millisecond timestamp.
 *
 * UUIDv7 stores a 48-bit timestamp, which corresponds to exactly 6 bytes in
 * the binary output representation.
 */
#define V7_MS_BYTES         6u

/**
 * @brief Convert whole seconds to milliseconds.
 *
 * This macro is used when collapsing a `struct timespec` into a single Unix
 * millisecond timestamp.
 *
 * @param sec
 *     Whole-second component to convert.
 *
 * @return Equivalent millisecond count as `uint64_t`.
 */
#define SEC_TO_MSEC(sec)    ((uint64_t)(sec) * UINT64_C(1000))

/**
 * @brief Convert nanoseconds to milliseconds using truncating integer
 *        division.
 *
 * This preserves the intended millisecond-resolution behavior of the UUIDv7
 * timestamp encoding.
 *
 * @param nsec
 *     Nanosecond component to convert.
 *
 * @return Whole milliseconds extracted from @p nsec as `uint64_t`.
 */
#define NSEC_TO_MSEC(nsec)  ((uint64_t)(nsec) / UINT64_C(1000000))

/****************************************************************************
 * PRIVATE STUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PRIVATE VARIABLES
 ****************************************************************************
 */

/* Monotonic state layout in a single 64-bit word:
 *  - bits [63:12] (upper 52 bits): unix milliseconds (uint64_t)
 *  - bits [11:0]  (lower 12 bits) : 12-bit sequence counter
 *
 * This atomic word is not protecting some separate shared object.
 * It is the shared object.
 *
 * Every caller races only on one question:
 * "which (ms, seq) pair do I reserve next?"
 *
 * The atomic operations below are therefore about:
 *  - uniqueness: two threads must not reserve the same pair
 *  - global order: all threads must observe one coherent modification order
 *
 * They are not about publishing additional payload data to other threads.
 */
static _Atomic uint64_t g_v7_state = 0;

/* RNG function pointer stored atomically to avoid data races between
 * callers of `uuid7_gen()` and `uuid7_set_rng_func()`.
 */
static _Atomic(uuid7_rng_function_t) g_uuid_rng_func = NULL;

#ifdef UUID7_TESTING
typedef uint64_t (*uuid7_time_fn_t)(void);

static _Atomic(uuid7_time_fn_t) g_uuid_time_func = NULL;
static _Atomic int g_force_rng_fail = 0;

int uuid7_test_set_time_fn(uuid7_time_fn_t fn)
{
    atomic_store_explicit(&g_uuid_time_func, fn, memory_order_release);
    return 0;
}

int uuid7_test_set_default_rng_fail(int enable)
{
    atomic_store_explicit(&g_force_rng_fail, enable ? 1 : 0, memory_order_release);
    return 0;
}

void uuid7_test_reset_state(void)
{
    atomic_store_explicit(&g_v7_state, 0, memory_order_release);
    atomic_store_explicit(&g_uuid_rng_func, NULL, memory_order_release);
    atomic_store_explicit(&g_uuid_time_func, NULL, memory_order_release);
    atomic_store_explicit(&g_force_rng_fail, 0, memory_order_release);
}
#endif

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Default RNG implementation: reads from /dev/urandom.
 *
 * On failure, this function sets a thread-local error flag and zero-fills
 * any remaining bytes to avoid leaving uninitialized data in the UUID.
 *
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 * 
 * @return 0 on success.
 * @return negative on failure (e.g., if /dev/urandom cannot be read).
 */
static int _default_rng(void* buf, size_t n);

/**
 * @brief Set the internal monotonic state `g_v7_state` based on a new state
 *        derived from a previously generated UUIDv7 value.
 * 
 * This is used during initialization when a previously generated UUIDv7 value
 * is provided. It extracts the timestamp and sequence from the input UUID and
 * updates `g_v7_state` to reflect that state, ensuring that subsequent UUIDs
 * generated by this process will be monotonic with respect to the provided
 * UUID.
 * 
 * @param uuid7_buf Pointer to a 16-byte buffer containing the previous UUID7.
 * @return None. This function updates internal state but does not produce output.
 */
static void _set_g_v7_state_from_uuid(const void *uuid7_buf);

/****************************************************************************
 * PRIVATE INLINE FUNCTIONS
 ****************************************************************************
 */

/**
 * @brief Set the internal monotonic state `g_v7_state` based on a new state.
 * 
 * This is used during initialization when a previously generated UUIDv7 value is
 * provided.
 */
static inline void _set_g_v7_state(uint64_t new_state)
{
    atomic_store_explicit(&g_v7_state, new_state, memory_order_release);
}

/**
 * @brief Attempt to reserve a new state in the monotonic state word.
 * 
 * Attempt to reserve `candidate` in g_v7_state.
 *
 * What CAS does:
 * - compare g_v7_state with `prev`
 * - if equal, store `candidate` and return success
 * - if different, leave g_v7_state unchanged, copy the current value
 *   of g_v7_state into `prev`, and return failure
 *
 * Why `weak`:
 * - `weak` CAS is allowed to fail spuriously
 * - this loop already retries, so `weak` is the normal efficient form
 *
 * Why these memory orders:
 * - success = `memory_order_acq_rel`
 *   This is stronger than strictly necessary here, but correct.
 *   The reservation state itself is the only shared datum that matters.
 * - failure = `memory_order_relaxed`
 *   On failure we only need the updated atomic value in `prev` so we
 *   can recompute the next candidate; no extra synchronization is
 *   required.
 *
 * In practice, this code could also work with relaxed ordering on
 * success because correctness depends on atomic uniqueness and the
 * modification order of g_v7_state, not on publishing other data.
 * We keep `acq_rel` here because it is already correct and explicit.
 */
static inline int _cas_g_v7_state(uint64_t candidate, uint64_t prev_state)
{
    return atomic_compare_exchange_weak_explicit(&g_v7_state, &prev_state, candidate,
                                                 memory_order_acq_rel, memory_order_relaxed);
}

/**
 * @brief Get the current value of the monotonic state word `g_v7_state`
 * 
 * memory_order_relaxed is enough for this read, need the atomic value itself.
 * No dependancy on this load to make any other shared memory visible.
 */
static inline uint64_t _get_g_v7_state(void)
{
    return (uint64_t)atomic_load_explicit(&g_v7_state, memory_order_acquire);
}

/**
 * @brief Load the current RNG function pointer atomically.
 * 
 * @return The currently configured RNG function, or NULL if none is set.
 */
static inline uuid7_rng_function_t _load_rng_func(void)
{
    return atomic_load_explicit(&g_uuid_rng_func, memory_order_acquire);
}

/**
 * @brief Store a new RNG function pointer atomically.
 * 
 * @param fn The RNG function to set as the current RNG.
 *           May be NULL to indicate no RNG configured.
 */
static inline void _set_rng_func(uuid7_rng_function_t fn)
{
    atomic_store_explicit(&g_uuid_rng_func, fn, memory_order_release);
}

/**
 * @brief Returns current real time in milliseconds since Unix epoch.
 * 
 * @return uint64_t Current time in ms. On failure, return 0. 
 */
static inline uint64_t _get_realtime_ms(void)
{
#ifdef UUID7_TESTING
    uuid7_time_fn_t fn =
        atomic_load_explicit(&g_uuid_time_func, memory_order_acquire);
    if(fn) return fn();
#endif
    /* returning 0 is a reasonable fallback since it produces valid UUIDs with
     * a known timestamp. The monotonic sequence logic still ensures
     * uniqueness. */
    struct timespec ts;
    if(clock_gettime(CLOCK_REALTIME, &ts) != 0) return (uint64_t)0;

    return SEC_TO_MSEC(ts.tv_sec) + NSEC_TO_MSEC(ts.tv_nsec);
}

/**
 * @brief Call the configured with uuid7_set_rng_func RNG function to write to @p buf.
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 * @return 0 on success, negative on error (only detectable for default RNG).
 */
static inline int _fill_random(void* buf, size_t n)
{
    /* get rng function */
    uuid7_rng_function_t installed_rng_function = _load_rng_func();

    if(!installed_rng_function)
    {
        /* install default if nothing set */
        uuid7_set_rng_func(_default_rng);

        /* get again */
        installed_rng_function = _load_rng_func();
    }

    /* call the RNG function */
    return installed_rng_function(buf, n);
}

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int uuid7_init(uuid7_rng_function_t fn, void *last_gen_uuid7)
{
    /* Set the g_v7_state to the value from the last generated UUID */
    if(last_gen_uuid7) _set_g_v7_state_from_uuid(last_gen_uuid7);

    /* Always set the rng function.
    If a function is passed, it will be used; otherwise, the default RNG is used. */
    return uuid7_set_rng_func(fn);
}

int uuid7_gen(void *out_buf)
{
    /* Check input */
    if(!out_buf) return -1;

    /**
     * random tail: V7_RB_BYTES bytes of CSPRNG entropy.
     * 
     * Fill the random tail before reserving the monotonic state.
     *
     * If RNG fails, return without advancing g_v7_state.
     * This avoids burning sequence numbers on failed generations.
     */
    uint8_t rb[V7_RB_BYTES];
    if(_fill_random(rb, V7_RB_BYTES) != 0) return -2;

    /* Prepare variables */
    uint64_t use_ms;  /* ms to use in generating uuid7 */
    uint16_t use_seq; /* sequence to use in generating uuid7 */
    uint64_t candidate = 0;

    /* Reserve strictly increasing (ms,rand_a) using a CAS loop.
     *
     * High-level rule:
     * - new real millisecond        -> start seq at 0
     * - same / older logical time   -> increment seq
     * - seq exhausted at 4095       -> advance logical ms, restart seq at 0
     *
     * The loop exists because another thread may reserve a value between our
     * load of g_v7_state and our attempt to store the next candidate. In that
     * case CAS fails, gives us the newer observed state through `prev`, and we
     * recompute from there.
     */
    for(;;)
    {
        /* get actual time in ms */
        const uint64_t now_ms = _get_realtime_ms();

        /* Load the last reserved (ms, seq) pair.
         *
         * `memory_order_relaxed` is enough for this read because we only need
         * the atomic value itself. We do not depend on this load to make any
         * other shared memory visible. */
        uint64_t       prev     = _get_g_v7_state();
        const uint64_t prev_ms  = V7_UNPACK_MS(prev);
        const uint16_t prev_seq = V7_UNPACK_SEQ(prev);

        /* Clock moved forward */
        if(now_ms > prev_ms)
        {
            /* Start the new (actual) millisecond from rand_a sequence 0. */
            candidate = V7_PACK(now_ms, 0u);
        }

        /**
         * Same millisecond, clock rollback, or logical time already ahead.
         * Preserve monotonicity by continuing from previous state.
         */
        else
        {
            /* rand_a 12 bits sequence is NOT exhausted */
            if(prev_seq < (uint16_t)V7_SEQ_MASK)
            {
                /**
                 * Keep the previous ms and increment the sequence by 1
                 * to get the next candidate. 
                 * This is the common case when multiple UUIDs are generated within the same ms,
                 * or when the clock is stable but not advancing (e.g., due to NTP adjustments
                 * or low-resolution timers).
                 * By incrementing the sequence, ensure that the next UUID is strictly greater
                 * than the previous while still using the same timestamp.
                 */
                candidate = V7_PACK(prev_ms, (prev_seq + 1u));
            }

            /* rand_a 12 bits sequence is exhausted */
            else
            {
                /**
                 * Move logical timestamp forward by one millisecond.
                 * Start the new millisecond from rand_a sequence 0.
                 */
                candidate = V7_PACK((prev_ms + 1ULL), 0u);
            }
        }

        /**
         * CAS - reserve `candidate` in g_v7_state.
         */
        if(!_cas_g_v7_state(candidate, prev))
        {
            /**
             * CAS failed because another thread reserved a UUID first.
             * Retry from the new global state.
             */
            continue;
        }

        /**
         * CAS succeeded:
         * use_ms and use_seq reserved for this uuid7
         */
        use_ms  = V7_UNPACK_MS(candidate);
        use_seq = V7_UNPACK_SEQ(candidate);

        /* Exit the loop */
        break;
    }

    /* UUIDv7 (RFC4122bis):
       - bytes 0..5 : 48-bit unix ms (big-endian)
       - byte    6  : version (0b0111) in high nibble | high 4 bits of seq
       - byte    7  : low 8 bits of seq
       - byte    8  : variant (10xxxxxx) | top 6 bits of rb[0]
       - bytes 9..15: remaining 7 bytes from rb[1..7]
    */
    
    /* cast to unsigned char the output buffer */
    unsigned char* out = (unsigned char*)out_buf;

    for(unsigned int i = 0; i < V7_MS_BYTES; ++i)
    {
        out[i] = V7_MS_BYTE(use_ms, i);
    }

    /* version 7 in high nibble | top 4 bits of sequence */
    out[6] = (unsigned char)(((0x7u & V7_BYTE_MASK) << 4) |
                       ((unsigned char)((use_seq >> V7_SEQ_HIGH_SHIFT) & V7_SEQ_HIGH_MASK)));
    out[7] = (unsigned char)((unsigned char)use_seq & V7_SEQ_LOW_MASK);

    /* variant (10xxxxxx) | top 6 bits of rb[0] */
    out[8] = (unsigned char)((rb[0] & V7_RB0_LOW6_MASK) | V7_VARIANT_TOP);

    /* Remaining tail: copy rb[1..7] into bytes 9..15 without clobbering variant */
    for(unsigned int i = 0; i < V7_RB_BYTES - 1u; ++i)
    {
        out[V7_MS_BYTES + 3u + i] = rb[1 + i];
    }

    return 0;
}

int uuid7_set_rng_func(uuid7_rng_function_t fn)
{
    /* Set the RNG function to user if given,
    otherwise set to default */
    uuid7_rng_function_t set_func = fn ? fn : _default_rng;
    _set_rng_func(set_func);

    /* reload and verify the function
    should not fail, atomic store does not fail */
    return _load_rng_func() == set_func ? 0 : -1;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static void _set_g_v7_state_from_uuid(const void *uuid7_buf)
{
    /* cast to unsigned char the input buffer */
    const unsigned char* in = (const unsigned char*)uuid7_buf;

    /* extract ms from bytes 0..5 */
    uint64_t ms = 0;
    for(unsigned int i = 0; i < V7_MS_BYTES; ++i)
    {
        ms = (ms << 8) | in[i];
    }

    /* extract seq from bytes 6..7 */
    uint16_t seq = ((uint16_t)(in[6] & V7_SEQ_HIGH_MASK) << V7_SEQ_HIGH_SHIFT) |
                    (uint16_t)(in[7]);

    /* pack and store into g_v7_state */
    uint64_t packed = V7_PACK(ms, seq);
    
    _set_g_v7_state(packed);
}

static int _default_rng(void* out_buf, size_t n)
{
    /* cast to unsigned char the output buffer */
    unsigned char* out = (unsigned char*)out_buf;

#ifdef UUID7_TESTING
    if(atomic_load_explicit(&g_force_rng_fail, memory_order_acquire))
    {
        memset(out, 0, n);
        return -1;
    }
#endif /* UUID7_TESTING */

#ifdef __linux__
    /* Try getrandom(2) in a loop,
     * letting the kernel fill up random bytes */
    size_t off = 0;
    while(off < n)
    {
        ssize_t r = getrandom(out + off, n - off, 0);
        if(r < 0)
        {
            /* consider syscall interrupt as retry */
            if(errno == EINTR) continue;
            /* fall back to /dev/urandom */
            break;
        }
        /* shouldn't happen, but treat as failure if it does */
        if(r == 0) break;

        /* sum written bytes */
        off += (size_t)r;
    }
    /* filled all the necessary bytes */
    if(off == n) return 0;
#else
    size_t off = 0;
#endif /* __linux__*/

    /* Fallback: read from /dev/urandom */
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        memset(out + off, 0, n - off);
        return -1;
    }
    /* Fill remaining bytes */
    size_t off2 = 0;
    while(off2 < (n - off))
    {
        ssize_t r = read(fd, out + off + off2, n - off - off2);
        if(r < 0)
        {
            /* consider syscall interrupt as retry */
            if(errno == EINTR) continue;
            /* close fd and set out to zero */
            close(fd);
            memset(out + off + off2, 0, n - off - off2);
            return -1;
        }
        if(r == 0)
        {
            /* close fd and set out to zero */
            close(fd);
            memset(out + off + off2, 0, n - off - off2);
            return -1;
        }
        /* sum written bytes */
        off2 += (size_t)r;
    }

    close(fd);
    return 0;
}

/****************************************************************************
 * SANITY CHECKS
 ****************************************************************************
 */

_Static_assert(CHAR_BIT == 8, "uuid7 requires 8-bit bytes");
_Static_assert(UCHAR_MAX == 255u, "uuid7 requires 8-bit unsigned char");
_Static_assert(sizeof(uint8_t) == 1u, "uint8_t must be 8 bits");
_Static_assert(sizeof(uint64_t) == 8u, "uint64_t must be 64 bits");

/* Compile-time sanity check: MS bytes + 2 (version + seq bytes) + remaining
 * RB bytes must equal total UUID size.
 * Note: RB bytes include the first byte used for the variant/top bits, so
 * the number of tail bytes written after out_buf[8] is (V7_RB_BYTES - 1).
 * The check below simplifies the arithmetic. */
_Static_assert((V7_MS_BYTES + 2u + V7_RB_BYTES) == UUID7_SIZE_BYTES,
               "UUID layout mismatch: adjust V7_* macros to sum to 16 bytes");

_Static_assert(UUID7_SIZE_BYTES == 16u, "UUIDv7 size must be 16 bytes");
_Static_assert(V7_SEQ_BITS == 12u, "UUIDv7 rand_a/sequence must be 12 bits");
_Static_assert(V7_SEQ_MASK == 0x0FFFu, "UUIDv7 sequence mask mismatch");
_Static_assert(V7_MS_BYTES == 6u, "UUIDv7 timestamp must be 48 bits");
_Static_assert(V7_RB_BYTES == 8u, "UUIDv7 rand_b assembly expects 8 bytes");