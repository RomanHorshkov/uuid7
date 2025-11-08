/**
 * @file uuid.c
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
 * Thread-safety: `uuid_gen()` is safe for concurrent calls thanks to the
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
#include "uuid.h"

#include <sodium.h>  // randombytes_buf()
#include <stdatomic.h>
#include <string.h>
#include <time.h>

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

/****************************************************************************
 * PRIVATE FUNCTIONS PROTOTYPES
 ****************************************************************************
 */

/**
 * @brief Returns current real time in milliseconds since Unix epoch.
 * 
 * @return uint64_t Current time in ms.
 */
static inline uint64_t realtime_ms(void);

/****************************************************************************
 * PUBLIC FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

int uuid_gen(uint8_t* out)
{
    if(!out) return -1;

    uint64_t use_ms;
    uint16_t seq12;

    /* Reserve strictly increasing (ms,seq) using a CAS loop */
    for(;;)
    {
        const uint64_t now_ms = realtime_ms();

        uint64_t       prev     = atomic_load_explicit(&g_v7_state, memory_order_relaxed);
        const uint64_t prev_ms  = V7_UNPACK_MS(prev);
        const uint16_t prev_seq = V7_UNPACK_SEQ(prev);

        /* clamp to non-decreasing ms */
        use_ms = (now_ms >= prev_ms) ? now_ms : prev_ms;

        uint16_t next_seq;
        if(use_ms == prev_ms)
        {
            /* same ms => bump seq */
            next_seq = (uint16_t)((prev_seq + 1u) & 0x0FFFu);
            /* if wrapped within same ms, wait for the next ms and retry */
            if(next_seq == 0u) continue;
        }
        else
        {
            /* New ms: initialize sequence to a CSPRNG-derived 12-bit value.
             * Avoid starting at 0 to reduce predictability and clustering.
             * Using libsodium's randombytes_buf provides cryptographic randomness. */
            uint16_t rnd = 0;
            randombytes_buf(&rnd, sizeof(rnd));
            rnd &= 0x0FFFu;         /* keep 12 bits */
            if(rnd == 0u) rnd = 1u; /* avoid zero-start for predictability */
            next_seq = rnd;
        }

        const uint64_t next = V7_PACK(use_ms, next_seq);
        if(atomic_compare_exchange_weak_explicit(&g_v7_state, &prev, next, memory_order_acq_rel,
                                                 memory_order_relaxed))
        {
            seq12 = next_seq;
            break;
        }
    }

    /* random tail: V7_RB_BYTES bytes of CSPRNG entropy. Some bits are
     * consumed by the version/variant fields above, the remainder form the
     * variable/random tail of the UUID. */
    uint8_t rb[V7_RB_BYTES];
    randombytes_buf(rb, V7_RB_BYTES);

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
    for(uint8_t i = 0; i < V7_RB_BYTES - 1u; ++i)
    {
        out[V7_MS_BYTES + 2u + i] = rb[1 + i];
    }

    return 0;
}

/****************************************************************************
 * PRIVATE FUNCTIONS DEFINITIONS
 ****************************************************************************
 */

static inline uint64_t realtime_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}
