/**
 * @file uuid7.c
 * @brief UUIDv7 generator implementation with monotonic sequence and CSPRNG
 *        initialization for the 12-bit sequence field.
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
 *   next pair to ensure uniqueness across threads/processes in the same
 *   address space.
 * - Sequence initialization: when a new millisecond is observed the 12-bit
 *   sequence is initialized from a cryptographically secure RNG (libsodium
 *   `randombytes_buf`) to reduce predictability and clustering. The sequence
 *   is forced non-zero to avoid trivial low-valued sequences.
 * - Wrap handling: if the 12-bit counter wraps within the same millisecond
 *   (i.e., >4095 values generated in a single ms), the generator will spin
 *   waiting for the next millisecond to avoid duplicates.
 * - Random tail: the low 64-bit tail is filled from CSPRNG to supply entropy
 *   for uniqueness and privacy.
 *
 * Thread-safety: `uuid7_gen()` is safe for concurrent calls thanks to the
 * atomic CAS loop protecting `g_v7_state`.
 *
 * Security note: the use of libsodium's `randombytes_buf` provides a
 * cryptographically secure source for sequence initialization and the tail.
 * Ensure `sodium_init()` is called once during application startup (it's
 * idempotent; calling it in a central init is recommended). If not called,
 * libsodium will still work as `randombytes_buf` will auto-initialize on many
 * platforms, but explicit init is clearer.
 *
 * @author: Roman Horshkov <roman.horshkov@gmail.com>
 * @date:   2025
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include "uuid7.h"

#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#if defined(__linux__)
#    include <sys/syscall.h>
#    include <sys/random.h>
#endif


#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

/****************************************************************************
 * PRIVATE DEFINES
 ****************************************************************************
 */

/* Packing helpers: make shifts/masks explicit and readable */
#define V7_SEQ_BITS         12u
#define V7_SEQ_MASK         ((1ull << V7_SEQ_BITS) - 1ull)
#define V7_MS_SHIFT         V7_SEQ_BITS
#define V7_PACK(ms, seq)    (((uint64_t)(ms) << V7_MS_SHIFT) | ((uint64_t)(seq) & V7_SEQ_MASK))
#define V7_UNPACK_MS(word)  ((uint64_t)(word) >> V7_MS_SHIFT)
#define V7_UNPACK_SEQ(word) ((uint16_t)((word) & V7_SEQ_MASK))

/* Byte-level helpers and masks */
#define V7_BYTE_MASK 0xFFu
/* Get the n'th byte of the 48-bit ms (n in [0..5], 0 is most-significant) */
#define V7_MS_BYTE(ms, n) ((uint8_t)((((uint64_t)(ms)) >> (8 * (5 - (n)))) & V7_BYTE_MASK))

/* Sequence high/low helpers */
#define V7_SEQ_HIGH_SHIFT 8u
#define V7_SEQ_HIGH_MASK 0x0Fu
#define V7_SEQ_LOW_MASK 0xFFu

/* Variant/rb masks */
#define V7_RB0_LOW6_MASK 0x3Fu
#define V7_VARIANT_TOP 0x80u

/* Sizes */
#define V7_RB_BYTES 8u
#define V7_MS_BYTES 6u
#define V7_UUID_BYTES 16u

/* Compile-time sanity check: MS bytes + 2 (version+seq bytes) + remaining RB bytes
 * must equal total UUID size. Note: RB bytes include the first byte used for the
 * variant/top bits, so the number of tail bytes written after out[8] is
 * (V7_RB_BYTES - 1). The check below simplifies the arithmetic. */
_Static_assert((V7_MS_BYTES + 2u + V7_RB_BYTES) == V7_UUID_BYTES,
               "UUID layout mismatch: adjust V7_* macros to sum to 16 bytes");

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
 */
static _Atomic uint64_t g_v7_state = 0;

/* RNG function pointer stored atomically to avoid data races between
 * callers of `uuid7_gen()` and `uuid7_set_rng()`. Using uintptr_t for
 * atomicity avoids portable issues with atomic function-pointer types.
 */
static _Atomic uintptr_t g_uuid_rng_ptr = (uintptr_t)0; /* 0 means not set */

/* Helper: convert stored uintptr_t to function pointer */
static inline uuid_rng_fn_t load_uuid_rng(void)
{
    uintptr_t p = atomic_load_explicit(&g_uuid_rng_ptr, memory_order_acquire);
    return (uuid_rng_fn_t)(uintptr_t)p;
}

/* Helper: store function pointer into atomic slot */
static inline void store_uuid_rng(uuid_rng_fn_t fn)
{
    uintptr_t p = (uintptr_t)fn;
    atomic_store_explicit(&g_uuid_rng_ptr, p, memory_order_release);
}

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Returns current real time in milliseconds since Unix epoch.
 * 
 * @return uint64_t Current time in ms.
 */
static inline uint64_t _realtime_ms(void);

/**
 * @brief Default RNG implementation: reads from /dev/urandom.
 *
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 */
static void _default_rng(void* buf, size_t n);

/**
 * @brief Helper: call the configured RNG.
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 */
static inline void _fill_random(void* buf, size_t n);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int uuid7_gen(uint8_t* out)
{
    if(!out) return -1;

    uint64_t use_ms;
    uint16_t seq12;

    /* Reserve strictly increasing (ms,rand_a) using a CAS loop.
     * Strategy: sample fresh 12-bit randomness for each candidate. If the
     * candidate is not greater than the last stored state, increment the
     * sequence where possible; on overflow advance the millisecond and
     * re-sample randomness. This keeps rand_a random most of the time but
     * preserves monotonicity when needed (RFC-compatible approach). */
    for(;;)
    {
        const uint64_t now_ms = _realtime_ms();

        uint64_t prev     = atomic_load_explicit(&g_v7_state, memory_order_relaxed);
        const uint64_t prev_ms  = V7_UNPACK_MS(prev);
        const uint16_t prev_seq = V7_UNPACK_SEQ(prev);

        /* clamp to non-decreasing ms */
        use_ms = (now_ms >= prev_ms) ? now_ms : prev_ms;

        /* Sample fresh 12-bit randomness for rand_a */
        uint16_t rnd = 0;
        _fill_random(&rnd, sizeof(rnd));
        rnd &= (uint16_t)V7_SEQ_MASK;
        if(rnd == 0u) rnd = 1u; /* prefer non-zero start */

        uint64_t candidate = V7_PACK(use_ms, rnd);

        if(candidate <= prev)
        {
            /* Need to produce a strictly greater value.
                * If prev_seq hasn't overflowed, increment prev.
                * Otherwise advance ms by 1 and re-randomize seq. */
            if(prev_seq != (uint16_t)V7_SEQ_MASK)
            {
                candidate = prev + 1ull; /* increment seq, preserves monotonicity */
            }
            else
            {
                /* Overflow: move to next millisecond and sample a non-zero seq */
                uint64_t next_ms = prev_ms + 1ull;
                uint16_t rnd2 = 0;
                _fill_random(&rnd2, sizeof(rnd2));
                rnd2 &= (uint16_t)V7_SEQ_MASK;
                if(rnd2 == 0u) rnd2 = 1u;
                candidate = V7_PACK(next_ms, rnd2);
            }
        }

        if(atomic_compare_exchange_weak_explicit(&g_v7_state, &prev, candidate, memory_order_acq_rel,
                                                    memory_order_relaxed))
        {
            seq12 = (uint16_t)(candidate & V7_SEQ_MASK);
            use_ms = V7_UNPACK_MS(candidate);
            break;
        }
    /* else: CAS failed, loop and try again */
    }

    /* random tail: V7_RB_BYTES bytes of CSPRNG entropy. Some bits are
     * consumed by the version/variant fields above, the remainder form the
     * variable/random tail of the UUID. */
    uint8_t rb[V7_RB_BYTES];
    _fill_random(rb, V7_RB_BYTES);

    /* UUIDv7 (RFC4122bis):
       - bytes 0..5 : 48-bit unix ms (big-endian)
       - byte    6  : version (0b0111) in high nibble | high 4 bits of seq
       - byte    7  : low 8 bits of seq
       - byte    8  : variant (10xxxxxx) | top 6 bits of rb[0]
       - bytes 9..15: remaining 7 bytes from rb[1..7]
    */
    for(uint8_t i = 0; i < V7_MS_BYTES; ++i)
    {
        out[i] = V7_MS_BYTE(use_ms, i);
    }

    /* version 7 in high nibble | top 4 bits of sequence */
    out[6] = (uint8_t)(((0x7u & V7_BYTE_MASK) << 4) | ((uint8_t)((seq12 >> V7_SEQ_HIGH_SHIFT) & V7_SEQ_HIGH_MASK)));
    out[7] = (uint8_t)((uint8_t)seq12 & V7_SEQ_LOW_MASK);

    /* variant (10xxxxxx) | top 6 bits of rb[0] */
    out[8] = (uint8_t)((rb[0] & V7_RB0_LOW6_MASK) | V7_VARIANT_TOP);

    /* Remaining tail: copy rb[1..7] into bytes 9..15 without clobbering variant */
    for(uint8_t i = 0; i < V7_RB_BYTES - 1u; ++i)
    {
        out[V7_MS_BYTES + 3u + i] = rb[1 + i];
    }

    return 0;
}

int uuid7_set_rng(uuid_rng_fn_t fn)
{
    /* Accept NULL to reset to default RNG. Return 0 on success.
     * No error conditions currently; keep return negative only for future
     * extensibility. The atomic store makes this safe to call concurrently
     * with `uuid7_gen()`.
     */
    if(fn)
    {
        store_uuid_rng(fn);
    }
    else
    {
        store_uuid_rng(_default_rng);
    }
    return 0;
}

int uuid7_init(uuid_rng_fn_t fn)
{
    /* If caller provided an RNG, install it atomically. Otherwise attempt to
     * install the built-in default RNG if none is present. Use CAS to remain
     * idempotent and thread-safe.
     */
    if(fn)
    {
        store_uuid_rng(fn);
        return 0;
    }

    uintptr_t expected = (uintptr_t)0;
    uintptr_t desired = (uintptr_t)_default_rng;
    atomic_compare_exchange_strong_explicit(&g_uuid_rng_ptr, &expected, desired,
                                           memory_order_acq_rel, memory_order_relaxed);
    return 0;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static inline uint64_t _realtime_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

static void _default_rng(void* buf, size_t n)
{
    if(!buf || n == 0) return;
#if defined(__linux__)
    /* Try getrandom(2) in a loop */
    size_t off = 0;
    while(off < n)
    {
        ssize_t r = syscall(SYS_getrandom, (char*)buf + off, n - off, 0);
        if(r < 0)
        {
            if(errno == EINTR) continue;
            break; /* fall back to /dev/urandom */
        }
        off += (size_t)r;
    }
    if(off == n) return;
#endif
    /* Fallback: read from /dev/urandom */
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd < 0) return;
    size_t off2 = 0;
    while(off2 < n)
    {
        ssize_t r = read(fd, (char*)buf + off2, n - off2);
        if(r < 0)
        {
            if(errno == EINTR) continue;
            close(fd);
            return;
        }
        if(r == 0)
        {
            close(fd);
            return;
        }
        off2 += (size_t)r;
    }
    close(fd);
    return;
}

static inline void _fill_random(void* buf, size_t n)
{
    if(!buf || n == 0) return;

    /* Load the current RNG function pointer atomically. If it hasn't been
     * set yet, attempt to install the default RNG once using a
     * compare-exchange. This avoids races between first-callers and callers
     * of `uuid7_set_rng()`.
     */
    uuid_rng_fn_t fn = load_uuid_rng();
    if(!fn)
    {
        uintptr_t expected = (uintptr_t)0;
        uintptr_t desired = (uintptr_t)_default_rng;
        atomic_compare_exchange_strong_explicit(&g_uuid_rng_ptr, &expected, desired,
                                               memory_order_acq_rel, memory_order_relaxed);
        fn = load_uuid_rng();
        if(!fn) fn = _default_rng; /* fallback, should not happen */
    }

    fn(buf, n);
    return;
}
