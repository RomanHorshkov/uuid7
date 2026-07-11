/**
 * @file uuid7.c
 * @brief UUIDv7 generator implementation with process-wide monotonic reservation state.
 *
 * The implementation separates two concerns:
 * - ordering is provided by a single atomic 64-bit state word containing `(unix_ms << 12) | rand_a`;
 * - unpredictability is provided by the configured RNG, which fills the `rand_b` tail before the state reservation is attempted.
 *
 * The CAS loop reserves one unique `(timestamp, rand_a)` pair per successful call. If real time moves forward, the sequence starts at zero
 * for the new millisecond. If real time stalls, regresses, or the logical clock is already ahead, the generator continues from the last
 * reserved state. Sequence overflow advances the logical millisecond by one and restarts the sequence at zero.
 *
 * State import is raise-only. A valid persisted UUIDv7 can move the process floor forward during initialization, but no import path can
 * rewind `g_v7_state`.
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

/*****************************************************************************************************************************************
 * PRIVATE DEFINES
 *****************************************************************************************************************************************
 */

/**
 * @brief Width in bits of the UUIDv7 `rand_a` / sequence field stored in the internal monotonic state word.
 *
 * The generator keeps its monotonic reservation state in a single 64-bit integer with this logical layout:
 *
 * - bits `[63:12]`: Unix timestamp in milliseconds
 * - bits `[11:0]` : 12-bit sequence / `rand_a` value
 *
 * This constant defines the width of the low-order sequence field. It is used by the packing, unpacking, and masking helpers below.
 *
 * @note A value of `12` matches the UUIDv7 `rand_a` field width defined by the format used in this module.
 */
#define V7_SEQ_BITS         12u

/**
 * @brief Bitmask for extracting or constraining the low 12-bit sequence field.
 *
 * Expands to a mask with the lowest `V7_SEQ_BITS` bits set to `1`. With the current configuration this is equivalent to `0x0FFF`.
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
 * Since the lower `V7_SEQ_BITS` bits are reserved for the sequence field, the timestamp is shifted left by exactly that amount so both
 * values occupy disjoint bit ranges inside one 64-bit integer.
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
 * Expanded form: `(((uint64_t)(ms) << V7_MS_SHIFT) | ((uint64_t)(seq) & V7_SEQ_MASK))`
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
 * This reverses the timestamp portion of `V7_PACK()` by shifting the input right by `V7_MS_SHIFT` bits and discarding the low sequence
 * field.
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
 * This reverses the sequence portion of `V7_PACK()` by masking the low `V7_SEQ_BITS` bits and casting the result to `uint16_t`.
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
 * This constant is applied after right-shifting multi-byte fields so the result is truncated to one octet before storing into the output
 * UUID byte array.
 */
#define V7_BYTE_MASK        0xFFu

/**
 * @brief Extract one big-endian byte from the 48-bit Unix millisecond
 *        timestamp used in the UUIDv7 output layout.
 *
 * The UUID stores the timestamp in bytes `0..5` in network order (most-significant byte first). This macro selects byte @p n from a
 * timestamp value by shifting the requested octet into the low 8 bits and masking with `V7_BYTE_MASK`.
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
 * Shifting the 12-bit sequence right by 8 positions exposes the upper nibble before applying `V7_SEQ_HIGH_MASK`.
 */
#define V7_SEQ_HIGH_SHIFT   8u

/**
 * @brief Mask for the high 4-bit nibble of the 12-bit `rand_a` sequence field
 *        after it has been shifted down to the low bits.
 *
 * With the current layout this mask equals `0x0F` and is used when composing UUID byte 6 together with the version nibble.
 */
#define V7_SEQ_HIGH_MASK    0x0Fu

/**
 * @brief UUID version nibble encoded in byte 6.
 */
#define V7_VERSION          0x07u

/**
 * @brief Bit shift used to place or extract the 4-bit UUID version nibble.
 */
#define V7_VERSION_SHIFT    4u

/**
 * @brief Mask for the low 8 bits of the 12-bit `rand_a` sequence field.
 *
 * This mask isolates `rand_a[7:0]`, which are written directly into UUID byte 7.
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
 * This mask keeps only the payload portion that may coexist with the variant marker in the same byte.
 */
#define V7_RB0_LOW6_MASK    0x3Fu

/**
 * @brief Mask for the RFC variant bits stored in UUID byte 8.
 */
#define V7_VARIANT_MASK     0xC0u

/**
 * @brief Pre-encoded RFC variant bits for UUID byte 8.
 *
 * The UUID variant required by RFC 4122 / RFC 9562 is binary `10` in the two most-significant bits of byte 8. OR-ing with this value sets
 * bit 7 and leaves bit 6 cleared.
 */
#define V7_VARIANT_TOP      0x80u

/**
 * @brief Number of random-tail bytes stored in the low 64 bits of the UUID
 *        assembly buffer before variant adjustment.
 *
 * The implementation samples 8 random bytes, then overlays the variant bits in the first of those bytes when constructing bytes `8..15`.
 */
#define V7_RB_BYTES         8u

/**
 * @brief Number of bytes occupied by the UUIDv7 Unix millisecond timestamp.
 *
 * UUIDv7 stores a 48-bit timestamp, which corresponds to exactly 6 bytes in the binary output representation.
 */
#define V7_MS_BYTES         6u

/**
 * @brief Convert whole seconds to milliseconds.
 *
 * This macro is used when collapsing a `struct timespec` into a single Unix millisecond timestamp.
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
 * This preserves the intended millisecond-resolution behavior of the UUIDv7 timestamp encoding.
 *
 * @param nsec
 *     Nanosecond component to convert.
 *
 * @return Whole milliseconds extracted from @p nsec as `uint64_t`.
 */
#define NSEC_TO_MSEC(nsec)  ((uint64_t)(nsec) / UINT64_C(1000000))

/*****************************************************************************************************************************************
 * PRIVATE STRUCTURED VARIABLES
 *****************************************************************************************************************************************
 */
/* None */

/*****************************************************************************************************************************************
 * PRIVATE VARIABLES
 *****************************************************************************************************************************************
 */

/* Monotonic state layout in a single 64-bit word:
 *  - bits [63:12] (upper 52 bits): unix milliseconds (uint64_t)
 *  - bits [11:0]  (lower 12 bits) : 12-bit sequence counter
 *
 * This atomic word is not protecting some separate shared object. It is the shared object.
 *
 * Every caller races only on one question: "which (ms, seq) pair do I reserve next?"
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

/* Count of clock_gettime() failures. A failure still yields a UNIQUE id (random
 * tail + monotonic seq), but with a zero timestamp — operationally misleading,
 * so we expose the count as a metric for the operator to alert on rather than
 * failing silently. Relaxed: it is a diagnostic tally, not a synchronization point. */
static _Atomic uint64_t g_clock_failures = 0;

#ifdef UUID7_TESTING
#    include "uuid7_test.h" /* shared prototypes + uuid7_time_fn_t for the hooks below */

static _Atomic(uuid7_time_fn_t) g_uuid_time_func = NULL;
static _Atomic int              g_force_rng_fail = 0;

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

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 *****************************************************************************************************************************************
 */

/**
 * @brief Default RNG implementation backed by getrandom(2), with /dev/urandom fallback.
 *
 * On failure, this function sets a thread-local error flag and zero-fills any remaining bytes to avoid leaving uninitialized data in the
 * UUID.
 *
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 *
 * @return 0 on success.
 * @return negative on failure (e.g., if /dev/urandom cannot be read).
 */
static int _default_rng(void* buf, size_t n);

/**
 * @brief Raise the internal monotonic state `g_v7_state` based on a UUIDv7
 *        buffer.
 *
 * This is used during initialization when a previously generated UUIDv7 value is provided. It extracts the timestamp and sequence from the
 * input UUID and raises `g_v7_state` to at least that value, ensuring that subsequent UUIDs generated by this process remain monotonic with
 * respect to the provided UUID without ever rewinding an already newer in-process state.
 *
 * @param uuid7_buf Pointer to a 16-byte buffer containing the previous UUID7.
 * @return 0 on success.
 * @return -1 if @p uuid7_buf is NULL.
 * @return -2 if @p uuid7_buf does not encode UUID version 7.
 * @return -3 if @p uuid7_buf does not encode the RFC variant bits.
 */
static int _raise_g_v7_state_from_uuid(const void* uuid7_buf);

/*****************************************************************************************************************************************
 * PRIVATE INLINE FUNCTIONS
 *****************************************************************************************************************************************
 */

/**
 * @brief Raise the internal monotonic state to at least @p floor_state.
 *
 * This helper is used by initialization-time state import. It is intentionally raise-only: if the current state is already newer, it is
 * left unchanged.
 */
static inline void _raise_g_v7_state(uint64_t floor_state)
{
    uint64_t cur = atomic_load_explicit(&g_v7_state, memory_order_acquire);

    while(cur < floor_state)
    {
        if(atomic_compare_exchange_weak_explicit(&g_v7_state, &cur, floor_state, memory_order_acq_rel, memory_order_acquire))
        {
            break;
        }
    }
}

/**
 * @brief Get the current value of the monotonic state word `g_v7_state`
 *
 * `memory_order_relaxed` is enough for this read because the reservation state itself is the only shared datum that matters here.
 */
static inline uint64_t _get_g_v7_state(void)
{
    return (uint64_t)atomic_load_explicit(&g_v7_state, memory_order_relaxed);
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
 * @brief Ensure an RNG function is installed and return it.
 *
 * The default RNG is installed lazily using compare-exchange so a concurrent explicit call to `uuid7_set_rng_func()` cannot be overwritten
 * by a racy fallback store from `uuid7_gen()`.
 */
static inline uuid7_rng_function_t _ensure_rng_func(void)
{
    uuid7_rng_function_t fn = _load_rng_func();
    if(fn) return fn;

    uuid7_rng_function_t expected = NULL;
    if(atomic_compare_exchange_strong_explicit(&g_uuid_rng_func, &expected, _default_rng, memory_order_acq_rel, memory_order_acquire))
    {
        return _default_rng;
    }

    return expected;
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
    uuid7_time_fn_t fn = atomic_load_explicit(&g_uuid_time_func, memory_order_acquire);
    if(fn) return fn();
#endif
    /* On failure fall back to timestamp 0 (still a valid, unique UUID thanks to
     * the random tail + monotonic sequence) BUT bump the failure metric so the
     * operator can alert — a silent zero-timestamp id is operationally
     * misleading. See uuid7_clock_failure_count(). */
    struct timespec ts;
    if(clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
        atomic_fetch_add_explicit(&g_clock_failures, 1u, memory_order_relaxed);
        return (uint64_t)0;
    }

    return SEC_TO_MSEC(ts.tv_sec) + NSEC_TO_MSEC(ts.tv_nsec);
}

/**
 * @brief Fill @p buf with random bytes using the currently configured RNG callback.
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 * @return 0 on success, negative on error (only detectable for default RNG).
 */
static inline int _fill_random(void* buf, size_t n)
{
    return _ensure_rng_func()(buf, n);
}

/*****************************************************************************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

int uuid7_init(uuid7_rng_function_t fn, const void* last_gen_uuid7)
{
    int rc = uuid7_set_rng_func(fn);
    if(rc != 0) return rc;

    if(last_gen_uuid7)
    {
        rc = _raise_g_v7_state_from_uuid(last_gen_uuid7);
        if(rc != 0) return rc;
    }

    return 0;
}

int uuid7_raise_floor(const void* last_uuid7)
{
    /* Raise-only import of a persisted floor WITHOUT touching the RNG selection —
     * for callers that init the generator early (RNG) but only learn the newest
     * existing id later (e.g. after opening the database). New ids are then
     * guaranteed to sort AFTER everything already stored, keeping DBI appends
     * sequential across restarts / snapshot restores / clock rollbacks. */
    if(!last_uuid7) return -1;
    return _raise_g_v7_state_from_uuid(last_uuid7);
}

uint64_t uuid7_clock_failure_count(void)
{
    return atomic_load_explicit(&g_clock_failures, memory_order_relaxed);
}

int uuid7_gen(void* out_buf)
{
    /* Public contract: the caller owns storage and must provide a writable 16-byte buffer. */
    if(!out_buf) return -1;

    /* Fill the random tail before reserving monotonic state. RNG failure must not burn a sequence value. */
    uint8_t rb[V7_RB_BYTES];
    if(_fill_random(rb, V7_RB_BYTES) != 0) return -2;

    /* Reserved monotonic state returned by the CAS loop below. */
    uint64_t use_ms;  /* Millisecond timestamp reserved for this UUID. */
    uint16_t use_seq; /* rand_a sequence reserved for this UUID. */
    uint64_t candidate = 0;

    /* Reserve strictly increasing (ms, rand_a) using a CAS loop.
     *
     * High-level rule:
     * - new real millisecond        -> start seq at 0
     * - same / older logical time   -> increment seq
     * - seq exhausted at 4095       -> advance logical ms, restart seq at 0
     *
     * The loop exists because another thread may reserve a value between our load of g_v7_state and our attempt to store the next
     * candidate. In that case CAS fails, gives us the newer observed state through `prev`, and we recompute from there.
     */
    for(;;)
    {
        /* Sample wall-clock time for this reservation attempt. */
        const uint64_t now_ms = _get_realtime_ms();

        /* Load the last reserved pair. Relaxed ordering is enough because this read depends only on the atomic state word itself. */
        uint64_t       prev     = _get_g_v7_state();
        const uint64_t prev_ms  = V7_UNPACK_MS(prev);
        const uint16_t prev_seq = V7_UNPACK_SEQ(prev);

        /* Wall clock moved beyond the previous logical state: start a fresh rand_a sequence. */
        if(now_ms > prev_ms)
        {
            /* Start the new (actual) millisecond from rand_a sequence 0. */
            candidate = V7_PACK(now_ms, 0u);
        }

        /* Same millisecond, clock rollback, or logical time already ahead: continue from previous state. */
        else
        {
            /* The 12-bit rand_a sequence still has capacity in this logical millisecond. */
            if(prev_seq < (uint16_t)V7_SEQ_MASK)
            {
                /* Reserve the next rand_a value in the current logical millisecond, preserving strict monotonic order. */
                candidate = V7_PACK(prev_ms, (prev_seq + 1u));
            }

            /* rand_a is exhausted: move logical time forward so generation remains non-blocking. */
            else
            {
                /* Start the next logical millisecond at rand_a zero instead of blocking for wall-clock time to advance. */
                candidate = V7_PACK((prev_ms + 1ULL), 0u);
            }
        }

        /* Reserve the candidate. On failure, `prev` is updated with the newer observed state. */
        if(!atomic_compare_exchange_weak_explicit(&g_v7_state, &prev, candidate, memory_order_acq_rel, memory_order_relaxed))
        {
            /* Another thread reserved first; retry from the newer state now visible through `prev`. */
            continue;
        }

        /* CAS succeeded: this caller now owns the candidate pair and may encode it. */
        use_ms  = V7_UNPACK_MS(candidate);
        use_seq = V7_UNPACK_SEQ(candidate);

        break;
    }

    /* UUIDv7 binary layout (RFC 9562-compatible):
       - bytes 0..5 : 48-bit unix ms (big-endian)
       - byte    6  : version (0b0111) in high nibble | high 4 bits of seq
       - byte    7  : low 8 bits of seq
       - byte    8  : variant (10xxxxxx) | top 6 bits of rb[0]
       - bytes 9..15: remaining 7 bytes from rb[1..7]
    */

    /* Treat caller storage as the binary UUID output byte array. */
    unsigned char* out = (unsigned char*)out_buf;

    for(unsigned int i = 0; i < V7_MS_BYTES; ++i)
    {
        out[i] = V7_MS_BYTE(use_ms, i);
    }

    /* version 7 in high nibble | top 4 bits of sequence */
    out[6] = (unsigned char)(((V7_VERSION & V7_BYTE_MASK) << V7_VERSION_SHIFT) |
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
    _set_rng_func(fn ? fn : _default_rng);
    return 0;
}

/*****************************************************************************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 *****************************************************************************************************************************************
 */

static int _raise_g_v7_state_from_uuid(const void* uuid7_buf)
{
    if(!uuid7_buf) return -1;

    /* Decode the previously generated UUID supplied by the caller. */
    const unsigned char* in = (const unsigned char*)uuid7_buf;

    if(((in[6] >> V7_VERSION_SHIFT) & V7_SEQ_HIGH_MASK) != V7_VERSION) return -2;
    if((in[8] & V7_VARIANT_MASK) != V7_VARIANT_TOP) return -3;

    /* Extract the 48-bit big-endian Unix millisecond timestamp from bytes 0..5. */
    uint64_t ms = 0;
    for(unsigned int i = 0; i < V7_MS_BYTES; ++i)
    {
        ms = (ms << 8) | (uint64_t)in[i];
    }

    /* Extract rand_a from byte 6 low nibble plus byte 7. */
    uint16_t seq = (uint16_t)((((uint16_t)(in[6] & V7_SEQ_HIGH_MASK)) << V7_SEQ_HIGH_SHIFT) | ((uint16_t)in[7]));

    /* Pack the imported floor and raise the process state if it is newer. */
    uint64_t packed = V7_PACK(ms, seq);

    _raise_g_v7_state(packed);
    return 0;
}

static int _default_rng(void* out_buf, size_t n)
{
    /* Treat caller storage as a byte buffer for entropy output. */
    unsigned char* out = (unsigned char*)out_buf;

#ifdef UUID7_TESTING
    if(atomic_load_explicit(&g_force_rng_fail, memory_order_acquire))
    {
        memset(out, 0, n);
        return -1;
    }
#endif /* UUID7_TESTING */

#ifdef __linux__
    /* Prefer getrandom(2) so the default path avoids file descriptors on Linux. */
    size_t off = 0;
    while(off < n)
    {
        ssize_t r = getrandom(out + off, n - off, 0);
        if(r < 0)
        {
            /* EINTR is transient; retry the syscall without treating it as entropy failure. */
            if(errno == EINTR) continue;
            /* Non-interrupt failure falls through to /dev/urandom fallback. */
            break;
        }
        /* A zero-length getrandom() read is unexpected; fall through to the fallback path. */
        if(r == 0) break;

        /* Account for the bytes already produced by this entropy source. */
        off += (size_t)r;
    }
    /* getrandom(2) satisfied the complete request. */
    if(off == n) return 0;
#else
    size_t off = 0;
#endif /* __linux__*/

    /* Fallback: read the remaining bytes from /dev/urandom. */
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        memset(out + off, 0, n - off);
        return -1;
    }
    /* Fill only the suffix not already produced by getrandom(2). */
    size_t off2 = 0;
    while(off2 < (n - off))
    {
        ssize_t r = read(fd, out + off + off2, n - off - off2);
        if(r < 0)
        {
            /* EINTR is transient; retry the syscall without treating it as entropy failure. */
            if(errno == EINTR) continue;
            /* Do not leave partial entropy-looking output on failure. */
            close(fd);
            memset(out + off + off2, 0, n - off - off2);
            return -1;
        }
        if(r == 0)
        {
            /* Do not leave partial entropy-looking output on failure. */
            close(fd);
            memset(out + off + off2, 0, n - off - off2);
            return -1;
        }
        /* Account for the bytes already produced by this entropy source. */
        off2 += (size_t)r;
    }

    close(fd);
    return 0;
}

/*****************************************************************************************************************************************
 * SANITY CHECKS
 *****************************************************************************************************************************************
 */

_Static_assert(CHAR_BIT == 8, "uuid7 requires 8-bit bytes");
_Static_assert(UCHAR_MAX == 255u, "uuid7 requires 8-bit unsigned char");
_Static_assert(sizeof(uint8_t) == 1u, "uint8_t must be 8 bits");
_Static_assert(sizeof(uint64_t) == 8u, "uint64_t must be 64 bits");

_Static_assert(UUID7_SIZE_BYTES == 16u, "UUIDv7 size must be 16 bytes");
_Static_assert(V7_SEQ_BITS == 12u, "UUIDv7 rand_a/sequence must be 12 bits");
_Static_assert(V7_SEQ_MASK == 0x0FFFu, "UUIDv7 sequence mask mismatch");
_Static_assert(V7_MS_BYTES == 6u, "UUIDv7 timestamp must be 48 bits");
_Static_assert(V7_RB_BYTES == 8u, "UUIDv7 rand_b assembly expects 8 bytes");

/* Compile-time sanity check: timestamp bytes, version/sequence bytes, and random-tail bytes must sum to 16. V7_RB_BYTES includes the byte
 * carrying variant bits, so only (V7_RB_BYTES - 1) bytes are copied after out_buf[8]. */
_Static_assert((V7_MS_BYTES + 2u + V7_RB_BYTES) == UUID7_SIZE_BYTES, "UUID layout mismatch: adjust V7_* macros to sum to 16 bytes");
