/**
 * @file integration_test.c
 * @brief CMocka integration tests for UUIDv7 layout, monotonicity, import, error handling, and concurrent uniqueness.
 *
 * The suite uses only the public UUID API plus test-only hooks compiled under UUID7_TESTING. It deliberately exercises both deterministic
 * RNG/time paths and heavy generation paths so regressions in binary layout, raise-only import, sequence overflow, and thread safety are
 * caught before packaging.
 */

#include "uuid7.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

/*
 * Integration test layout
 * -----------------------
 *
 * 1. Test-only hooks exported by uuid7.c under UUID7_TESTING
 * 2. Deterministic helper RNGs and time sources
 * 3. Small UUID decoding / construction helpers
 * 4. Focused tests grouped by behavior:
 *    - binary layout
 *    - initialization / imported-state semantics
 *    - error handling
 *    - monotonicity and overflow
 *    - concurrency and heavy uniqueness checks
 *
 * The tests intentionally exercise the public API only. Imported-state behavior is driven through uuid7_init(fn, last_gen_uuid7); there is
 * no separate public "resume" entry point.
 */

/* Test-only hooks provided by uuid7.c when compiled with -DUUID7_TESTING —
 * shared prototypes so implementation and tests agree (no -Wmissing-prototypes). */
#include "uuid7_test.h"

/* Shared constants used across the integration suite. */
#define TEST_UUID_TIMESTAMP_BYTES         6u
#define TEST_UUID_VERSION_TOP             0x70u
#define TEST_UUID_SEQ_HIGH_MASK           0x0Fu
#define TEST_UUID_VARIANT_MASK            0xC0u
#define TEST_UUID_VARIANT_TOP             0x80u
#define TEST_SEQ_SPACE                    4096u
#define TEST_MONOTONIC_SAMPLE_COUNT       1000u
#define TEST_TIME_REGRESSION_SAMPLE_COUNT 2000u
#define TEST_MT_THREADS                   16u
#define TEST_MT_UUIDS_PER_THREAD          100000u
#define TEST_HEAVY_SINGLE_THREAD_UUIDS    1000000u

/*
 * Deterministic scripted RNG
 * --------------------------
 *
 * Some tests need exact byte-by-byte control over the random tail. The helper below feeds a scripted byte stream first, then increments
 * from a fallback value once the script is exhausted.
 */
typedef struct scripted_rng_ctx
{
    uint8_t script[256];
    size_t  script_len;
    size_t  cursor;
    uint8_t fallback;
} scripted_rng_ctx_t;

/*
 * Thread worker context used by the concurrent uniqueness test. Each worker writes into a disjoint slice of a shared output buffer.
 */
typedef struct thread_ctx
{
    uint8_t* buf;
    size_t   count;
    int      error;
} thread_ctx_t;

static scripted_rng_ctx_t g_rng_ctx;
static _Atomic uint32_t   g_fast_rng_state = 0x12345678u;
static _Atomic uint64_t   g_fake_time_ms   = 0;
static _Atomic uint32_t   g_time_flip      = 0;

/*
 * scripted_rng()
 * --------------
 * Return deterministic bytes so layout tests can assert exact UUID payload.
 */
static int scripted_rng(void* buf, const size_t n)
{
    uint8_t* out = (uint8_t*)buf;

    for(size_t i = 0; i < n; ++i)
    {
        if(g_rng_ctx.cursor < g_rng_ctx.script_len)
        {
            out[i] = g_rng_ctx.script[g_rng_ctx.cursor++];
        }
        else
        {
            out[i] = g_rng_ctx.fallback++;
        }
    }

    return 0;
}

/*
 * rng_prepare_script()
 * --------------------
 * Load scripted RNG state without installing the callback. This is useful for tests that want uuid7_init() itself to install the RNG
 * function.
 */
static void rng_prepare_script(const uint8_t* data, size_t len, uint8_t fallback_start)
{
    if(len > sizeof(g_rng_ctx.script))
    {
        len = sizeof(g_rng_ctx.script);
    }

    if(data && len)
    {
        memcpy(g_rng_ctx.script, data, len);
    }

    g_rng_ctx.script_len = len;
    g_rng_ctx.cursor     = 0;
    g_rng_ctx.fallback   = fallback_start;
}

/*
 * rng_load_script()
 * -----------------
 * Load the scripted byte stream and immediately install scripted_rng() as the active RNG for the library.
 */
static void rng_load_script(const uint8_t* data, size_t len, uint8_t fallback_start)
{
    rng_prepare_script(data, len, fallback_start);
    uuid7_set_rng_func(scripted_rng);
}

/*
 * fast_rng()
 * ----------
 * Cheap deterministic RNG used by heavy uniqueness tests. It is intentionally fast and predictable; these tests validate generator
 * behavior, not entropy quality.
 */
static int fast_rng(void* buf, const size_t n)
{
    uint8_t* out = (uint8_t*)buf;

    for(size_t i = 0; i < n; ++i)
    {
        uint32_t v = atomic_fetch_add_explicit(&g_fast_rng_state, 1u, memory_order_relaxed);
        v          = v * 1103515245u + 12345u;
        out[i]     = (uint8_t)(v >> 24);
    }

    return 0;
}

/*
 * zero_rng()
 * ----------
 * Return all-zero random bytes. This is ideal for tests that care only about timestamp / sequence behavior and want the random tail to stay
 * stable.
 */
static int zero_rng(void* buf, const size_t n)
{
    memset(buf, 0, n);
    return 0;
}

/*
 * fake_time_now() and time_regress_now()
 * --------------------------------------
 * Deterministic time sources used to force stable timestamps and simulated clock regressions.
 */
static uint64_t fake_time_now(void)
{
    return atomic_load_explicit(&g_fake_time_ms, memory_order_relaxed);
}

static uint64_t time_regress_now(void)
{
    uint32_t n = atomic_fetch_add_explicit(&g_time_flip, 1u, memory_order_relaxed);
    return (n & 1u) ? 999u : 1000u;
}

static void set_fake_time(uint64_t ms)
{
    atomic_store_explicit(&g_fake_time_ms, ms, memory_order_relaxed);
}

/*
 * UUID decoding helpers
 * ---------------------
 * These helpers inspect the binary UUID layout emitted by the library.
 */
static uint64_t extract_ms(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    uint64_t ms = 0;

    for(size_t i = 0; i < TEST_UUID_TIMESTAMP_BYTES; ++i)
    {
        ms = (ms << 8) | uuid[i];
    }

    return ms;
}

static uint16_t extract_seq(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    return (uint16_t)(((uint16_t)(uuid[6] & TEST_UUID_SEQ_HIGH_MASK) << 8) | uuid[7]);
}

static uint64_t extract_packed_state(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    return (extract_ms(uuid) << 12) | extract_seq(uuid);
}

/*
 * build_valid_uuid7()
 * -------------------
 * Build a minimal, syntactically valid UUIDv7 buffer with a caller-controlled timestamp and sequence. The random tail is zeroed because
 * imported-state logic only consumes timestamp, version, sequence, and variant bits.
 */
static void build_valid_uuid7(uint8_t uuid[UUID7_SIZE_BYTES], uint64_t ms, uint16_t seq)
{
    memset(uuid, 0, UUID7_SIZE_BYTES);

    for(size_t i = 0; i < TEST_UUID_TIMESTAMP_BYTES; ++i)
    {
        uuid[i] = (uint8_t)((ms >> (8u * (5u - i))) & 0xFFu);
    }

    uuid[6] = (uint8_t)(TEST_UUID_VERSION_TOP | ((seq >> 8) & TEST_UUID_SEQ_HIGH_MASK));
    uuid[7] = (uint8_t)(seq & 0xFFu);
    uuid[8] = TEST_UUID_VARIANT_TOP;
}

static void assert_uuid7_markers(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    assert_int_equal((uuid[6] & 0xF0), TEST_UUID_VERSION_TOP);
    assert_int_equal((uuid[8] & TEST_UUID_VARIANT_MASK), TEST_UUID_VARIANT_TOP);
}

/*
 * reset_state()
 * -------------
 * Reset both the library-under-test and all deterministic helper state so every test starts from a clean, isolated baseline.
 */
static void reset_state(void)
{
    uuid7_test_reset_state();
    memset(&g_rng_ctx, 0, sizeof(g_rng_ctx));
    atomic_store_explicit(&g_fast_rng_state, 0x12345678u, memory_order_relaxed);
    atomic_store_explicit(&g_fake_time_ms, 0, memory_order_relaxed);
    atomic_store_explicit(&g_time_flip, 0, memory_order_relaxed);
}

/**
 * @brief uuid7_gen() must remain usable even if the caller never explicitly calls uuid7_init() or uuid7_set_rng_func().
 *
 * @verifies UUID7-SRS-021 UUID7-SRS-002 UUID7-SRS-003
 */
static void test_default_rng_used_when_uninitialized(void** state)
{
    (void)state;
    reset_state();

    uint8_t   uuid[UUID7_SIZE_BYTES] = {0};
    const int rc                     = uuid7_gen(uuid);

    assert_true(rc == 0 || rc == -2);
    if(rc != 0) return;

    assert_uuid7_markers(uuid);
}

/**
 * @brief Verify the exact byte mapping of the UUID tail.
 *
 * - byte 8 stores variant bits overlaid onto scripted random byte 0
 * - bytes 9..15 store scripted random bytes 1..7 unchanged
 *
 * @verifies UUID7-SRS-002 UUID7-SRS-003 UUID7-SRS-005
 */
static void test_version_variant_and_tail_bytes(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0xAA, 0xBC, 0xCD, 0xDE, 0xEF, 0x01, 0x23, 0x45};
    rng_load_script(script, sizeof(script), 0x10u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_uuid7_markers(uuid);
    assert_int_equal(uuid[8], (uint8_t)((script[0] & 0x3Fu) | TEST_UUID_VARIANT_TOP));

    for(size_t i = 0; i < 7u; ++i)
    {
        assert_int_equal(uuid[9u + i], script[1u + i]);
    }
}

/**
 * @brief The encoded 48-bit timestamp must match the test-time override exactly.
 *
 * @verifies UUID7-SRS-001
 */
static void test_timestamp_matches_override(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x55u);
    set_fake_time(0x010203040506ull);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 0x010203040506ull);
}

/**
 * @brief With a fixed millisecond, packed (ms, seq) state must be strictly increasing for every generated UUID.
 *
 * @verifies UUID7-SRS-004 UUID7-SRS-007 UUID7-SRS-009
 */
static void test_monotonic_non_decreasing_many(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x20u);
    set_fake_time(42u);
    uuid7_test_set_time_fn(fake_time_now);

    uint64_t prev = 0;

    for(size_t i = 0; i < TEST_MONOTONIC_SAMPLE_COUNT; ++i)
    {
        uint8_t uuid[UUID7_SIZE_BYTES] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);

        const uint64_t packed = extract_packed_state(uuid);
        if(i > 0u)
        {
            assert_true(packed > prev);
        }

        prev = packed;
    }
}

/**
 * @brief A single logical millisecond can hold TEST_SEQ_SPACE UUIDs because the sequence field is 12 bits wide (0..4095).
 *
 * The next UUID must roll into the next logical millisecond and restart the sequence at zero.
 *
 * @verifies UUID7-SRS-004 UUID7-SRS-010
 */
static void test_overflow_advances_logical_ms(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    for(size_t i = 0; i <= TEST_SEQ_SPACE; ++i)
    {
        uint8_t uuid[UUID7_SIZE_BYTES] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);

        const uint64_t ms  = extract_ms(uuid);
        const uint16_t seq = extract_seq(uuid);

        if(i == 0u)
        {
            assert_int_equal(ms, 1000u);
            assert_int_equal(seq, 0u);
        }

        if(i == (TEST_SEQ_SPACE - 1u))
        {
            assert_int_equal(ms, 1000u);
            assert_int_equal(seq, 0x0FFFu);
        }

        if(i == TEST_SEQ_SPACE)
        {
            assert_int_equal(ms, 1001u);
            assert_int_equal(seq, 0u);
        }
    }
}

/**
 * @brief The generator must reject NULL output buffers deterministically.
 *
 * @verifies UUID7-SRS-031 UUID7-ICD-007
 */
static void test_null_buffer_rejected(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x20u);
    assert_int_equal(uuid7_gen(NULL), -1);
}

/**
 * @brief uuid7_set_rng_func(NULL) must restore the built-in default RNG.
 *
 * @verifies UUID7-SRS-020 UUID7-ICD-008
 */
static void test_rng_reset_to_default(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x10, 0x20, 0x30, 0x40};
    rng_load_script(script, sizeof(script), 0x50u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_int_equal(uuid7_set_rng_func(NULL), 0);

    const int rc = uuid7_gen(uuid);
    assert_true(rc == 0 || rc == -2);
    if(rc != 0) return;

    assert_uuid7_markers(uuid);
}

/**
 * @brief uuid7_init(fn, NULL) must install the supplied RNG callback.
 *
 * @verifies UUID7-SRS-020 UUID7-ICD-004
 */
static void test_init_accepts_custom_rng(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88};
    rng_prepare_script(script, sizeof(script), 0x60u);

    assert_int_equal(uuid7_init(scripted_rng, NULL), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_uuid7_markers(uuid);
    assert_int_equal(uuid[9], script[1]);
}

/**
 * @brief Passing NULL as the RNG in uuid7_init() must actively restore the default RNG.
 *
 * A previously installed custom RNG must not stay in place.
 *
 * @verifies UUID7-SRS-020 UUID7-ICD-004
 */
static void test_init_null_uses_default_rng(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x22, 0x44, 0x66, 0x88, 0xAA, 0xCC, 0xEE, 0xFF};
    rng_prepare_script(script, sizeof(script), 0x70u);

    assert_int_equal(uuid7_set_rng_func(scripted_rng), 0);
    assert_int_equal(uuid7_init(NULL, NULL), 0);
    assert_int_equal(uuid7_test_set_default_rng_fail(1), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), -2);

    assert_int_equal(uuid7_test_set_default_rng_fail(0), 0);
}

/**
 * @brief Importing a previous UUID through uuid7_init() must raise the internal monotonic floor.
 *
 * The next generated UUID is strictly newer than the imported one.
 *
 * @verifies UUID7-SRS-014
 */
static void test_init_raises_state_from_last_uuid(void** state)
{
    (void)state;
    reset_state();

    uint8_t last_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(last_uuid, 2000u, 10u);

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    assert_int_equal(uuid7_init(zero_rng, last_uuid), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 2000u);
    assert_int_equal(extract_seq(uuid), 11u);
}

/**
 * @brief Re-running uuid7_init() with an older imported UUID must never rewind the generator's monotonic state.
 *
 * @verifies UUID7-SRS-016
 */
static void test_repeated_init_does_not_move_state_backward(void** state)
{
    (void)state;
    reset_state();

    uint8_t newer_uuid[UUID7_SIZE_BYTES];
    uint8_t older_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(newer_uuid, 2000u, 10u);
    build_valid_uuid7(older_uuid, 1000u, 50u);

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(500u);
    uuid7_test_set_time_fn(fake_time_now);

    assert_int_equal(uuid7_init(zero_rng, newer_uuid), 0);
    assert_int_equal(uuid7_init(zero_rng, older_uuid), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 2000u);
    assert_int_equal(extract_seq(uuid), 11u);
}

/**
 * @brief Imported state must be rejected when the version nibble is not 7.
 *
 * @verifies UUID7-SRS-017 UUID7-ICD-004
 */
static void test_init_rejects_invalid_import_version(void** state)
{
    (void)state;
    reset_state();

    uint8_t invalid_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(invalid_uuid, 1234u, 5u);
    invalid_uuid[6] = (uint8_t)((0x6u << 4) | (invalid_uuid[6] & TEST_UUID_SEQ_HIGH_MASK));

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    assert_int_equal(uuid7_init(zero_rng, invalid_uuid), -2);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 1000u);
    assert_int_equal(extract_seq(uuid), 0u);
}

/**
 * @brief Imported state must be rejected when the RFC variant bits are not 10xxxxxx.
 *
 * @verifies UUID7-SRS-017 UUID7-ICD-004
 */
static void test_init_rejects_invalid_import_variant(void** state)
{
    (void)state;
    reset_state();

    uint8_t invalid_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(invalid_uuid, 1234u, 5u);
    invalid_uuid[8] = 0x40u;

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    assert_int_equal(uuid7_init(zero_rng, invalid_uuid), -3);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 1000u);
    assert_int_equal(extract_seq(uuid), 0u);
}

/**
 * @brief The public error contract for RNG failure is -2.
 *
 * @verifies UUID7-SRS-024 UUID7-ICD-007
 */
static void test_default_rng_failure_returns_minus2(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng_func(NULL), 0);
    assert_int_equal(uuid7_test_set_default_rng_fail(1), 0);

    uint8_t uuid[UUID7_SIZE_BYTES];
    memset(uuid, 0xAA, sizeof(uuid));
    assert_int_equal(uuid7_gen(uuid), -2);

    assert_int_equal(uuid7_test_set_default_rng_fail(0), 0);
}

/**
 * @brief The default-RNG failure hook must not affect callers that install a custom RNG implementation.
 *
 * @verifies UUID7-SRS-020
 */
static void test_rng_failure_ignored_for_custom_rng(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x90u);
    assert_int_equal(uuid7_test_set_default_rng_fail(1), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_int_equal(uuid7_test_set_default_rng_fail(0), 0);
}

/*
 * thread_generate()
 * -----------------
 * Worker body for the multithreaded integration test. Each thread asserts local monotonicity and correct UUID markers while writing its
 * slice.
 */
static void* thread_generate(void* arg)
{
    thread_ctx_t* ctx  = (thread_ctx_t*)arg;
    uint64_t      prev = 0;

    for(size_t i = 0; i < ctx->count; ++i)
    {
        uint8_t*  uuid = ctx->buf + (i * UUID7_SIZE_BYTES);
        const int rc   = uuid7_gen(uuid);
        if(rc != 0)
        {
            ctx->error = rc;
            return NULL;
        }

        const uint64_t packed = extract_packed_state(uuid);
        if(i > 0u && packed <= prev)
        {
            ctx->error = -3;
            return NULL;
        }

        if((uuid[6] & 0xF0u) != TEST_UUID_VERSION_TOP || (uuid[8] & TEST_UUID_VARIANT_MASK) != TEST_UUID_VARIANT_TOP)
        {
            ctx->error = -4;
            return NULL;
        }

        prev = packed;
    }

    return NULL;
}

static int cmp_uuid(const void* a, const void* b)
{
    return memcmp(a, b, UUID7_SIZE_BYTES);
}

/**
 * @brief Concurrent callers must never receive duplicate UUIDs.
 *
 * @verifies UUID7-SRS-007 UUID7-SRS-028
 */
static void test_multithreaded_uniqueness(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng_func(fast_rng), 0);

    const size_t total = TEST_MT_THREADS * TEST_MT_UUIDS_PER_THREAD;
    uint8_t*     all   = calloc(total, UUID7_SIZE_BYTES);
    assert_non_null(all);

    pthread_t    tids[TEST_MT_THREADS];
    thread_ctx_t ctx[TEST_MT_THREADS];

    for(size_t i = 0; i < TEST_MT_THREADS; ++i)
    {
        ctx[i].buf   = all + (i * TEST_MT_UUIDS_PER_THREAD * UUID7_SIZE_BYTES);
        ctx[i].count = TEST_MT_UUIDS_PER_THREAD;
        ctx[i].error = 0;

        assert_int_equal(pthread_create(&tids[i], NULL, thread_generate, &ctx[i]), 0);
    }

    for(size_t i = 0; i < TEST_MT_THREADS; ++i)
    {
        pthread_join(tids[i], NULL);
        assert_int_equal(ctx[i].error, 0);
    }

    qsort(all, total, UUID7_SIZE_BYTES, cmp_uuid);
    for(size_t i = 1; i < total; ++i)
    {
        assert_true(memcmp(all + ((i - 1u) * UUID7_SIZE_BYTES), all + (i * UUID7_SIZE_BYTES), UUID7_SIZE_BYTES) != 0);
    }

    free(all);
}

/**
 * @brief Even if the wall clock moves backward, the packed monotonic state must keep increasing.
 *
 * @verifies UUID7-SRS-007 UUID7-SRS-009
 */
static void test_time_regression_monotonic(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x33u);
    uuid7_test_set_time_fn(time_regress_now);

    uint64_t prev = 0;

    for(size_t i = 0; i < TEST_TIME_REGRESSION_SAMPLE_COUNT; ++i)
    {
        uint8_t uuid[UUID7_SIZE_BYTES] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);

        const uint64_t packed = extract_packed_state(uuid);
        if(i > 0u)
        {
            assert_true(packed > prev);
        }

        prev = packed;
    }
}

/**
 * @brief Repeated sequence exhaustion must keep advancing the logical millisecond one step at a time.
 *
 * The sequence restarts from zero on each rollover.
 *
 * @verifies UUID7-SRS-010
 */
static void test_overflow_multiple_ms_advances(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    const size_t total = (TEST_SEQ_SPACE * 2u) + 1u;
    for(size_t i = 0; i < total; ++i)
    {
        uint8_t uuid[UUID7_SIZE_BYTES] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);

        const uint64_t ms  = extract_ms(uuid);
        const uint16_t seq = extract_seq(uuid);

        if(i == 0u)
        {
            assert_int_equal(ms, 1000u);
            assert_int_equal(seq, 0u);
        }

        if(i == (TEST_SEQ_SPACE - 1u))
        {
            assert_int_equal(ms, 1000u);
            assert_int_equal(seq, 0x0FFFu);
        }

        if(i == TEST_SEQ_SPACE)
        {
            assert_int_equal(ms, 1001u);
            assert_int_equal(seq, 0u);
        }

        if(i == ((TEST_SEQ_SPACE * 2u) - 1u))
        {
            assert_int_equal(ms, 1001u);
            assert_int_equal(seq, 0x0FFFu);
        }

        if(i == (TEST_SEQ_SPACE * 2u))
        {
            assert_int_equal(ms, 1002u);
            assert_int_equal(seq, 0u);
        }
    }
}

/**
 * @brief Stress the generator in one thread and confirm all produced UUIDs are unique after lexicographic sorting.
 *
 * @verifies UUID7-SRS-002 UUID7-SRS-003 UUID7-SRS-007
 */
static void test_heavy_single_thread_uniqueness(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng_func(fast_rng), 0);

    uint8_t* all = calloc(TEST_HEAVY_SINGLE_THREAD_UUIDS, UUID7_SIZE_BYTES);
    assert_non_null(all);

    for(size_t i = 0; i < TEST_HEAVY_SINGLE_THREAD_UUIDS; ++i)
    {
        uint8_t* uuid = all + (i * UUID7_SIZE_BYTES);
        assert_int_equal(uuid7_gen(uuid), 0);
        assert_uuid7_markers(uuid);
    }

    qsort(all, TEST_HEAVY_SINGLE_THREAD_UUIDS, UUID7_SIZE_BYTES, cmp_uuid);
    for(size_t i = 1; i < TEST_HEAVY_SINGLE_THREAD_UUIDS; ++i)
    {
        assert_true(memcmp(all + ((i - 1u) * UUID7_SIZE_BYTES), all + (i * UUID7_SIZE_BYTES), UUID7_SIZE_BYTES) != 0);
    }

    free(all);
}

/**
 * @brief uuid7_raise_floor() imports a persisted floor WITHOUT touching the RNG.
 *
 * The next generated id must sort strictly AFTER it even when the wall clock is older — the guarantee that keeps time-ordered DBI
 * appends sequential across a restart / snapshot restore / clock rollback. The tail still comes from the RNG installed before the
 * import. NULL is rejected.
 *
 * @verifies UUID7-SRS-014 UUID7-SRS-015 UUID7-SRS-032 UUID7-ICD-005
 */
static void test_raise_floor_orders_after_import(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u); /* wall clock is BEHIND the store's newest id */
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t last_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(last_uuid, 5000u, 7u);           /* newest id already persisted */

    assert_int_equal(uuid7_raise_floor(NULL), -1);     /* NULL rejected      */
    assert_int_equal(uuid7_raise_floor(last_uuid), 0); /* floor raised       */

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 5000u); /* not the older 1000 wall clock */
    assert_int_equal(extract_seq(uuid), 8u);   /* strictly after the floor      */
    for(size_t i = 9u; i < UUID7_SIZE_BYTES; ++i)
    {
        assert_int_equal(uuid[i], 0u);         /* the RNG installed before the import still fills the tail */
    }
}

/**
 * @brief Abuse bound: generation refuses to run more than UUID7_MAX_AHEAD_MS ahead of the wall clock.
 *
 * With the wall clock frozen, the generator may borrow logical milliseconds only up to UUID7_MAX_AHEAD_MS past it. Park the state
 * exactly at the bound (sequence one short of exhaustion), prove the last in-bound id is minted, then prove the next reservation —
 * which would need bound+1 — is refused with -3 and leaves the state untouched. Advancing the wall clock by one millisecond lifts the
 * bound by one and the refused reservation succeeds.
 *
 * @verifies UUID7-SRS-011 UUID7-SRS-008 UUID7-SRS-010 UUID7-ICD-007
 */
static void test_gen_refuses_beyond_ahead_bound(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    const uint64_t bound_ms = 1000u + (uint64_t)UUID7_MAX_AHEAD_MS;
    uint8_t        floor_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(floor_uuid, bound_ms, 0x0FFEu); /* exactly AT the bound: allowed */
    assert_int_equal(uuid7_raise_floor(floor_uuid), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0); /* last in-bound reservation      */
    assert_int_equal(extract_ms(uuid), bound_ms);
    assert_int_equal(extract_seq(uuid), 0x0FFFu);

    assert_int_equal(uuid7_gen(uuid), -3); /* would need bound+1: refused    */
    assert_int_equal(uuid7_gen(uuid), -3); /* state untouched: still refused */

    set_fake_time(1001u);                  /* real time catches up by 1 ms   */
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), bound_ms + 1u);
    assert_int_equal(extract_seq(uuid), 0u);
}

/**
 * @brief A persisted id dated more than UUID7_MAX_AHEAD_MS past the wall clock is refused.
 *
 * Such an id is a poisoned import (or a runaway clock): uuid7_raise_floor() and uuid7_init() must refuse it with -4 and leave the generator
 * exactly where it was. The 48-bit maximum timestamp is the canonical poison value.
 *
 * @verifies UUID7-SRS-018 UUID7-ICD-004 UUID7-ICD-005
 */
static void test_raise_floor_refuses_far_future(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t poison[UUID7_SIZE_BYTES];
    build_valid_uuid7(poison, 1000u + (uint64_t)UUID7_MAX_AHEAD_MS + 1u, 0u); /* one past the bound */
    assert_int_equal(uuid7_raise_floor(poison), -4);
    assert_int_equal(uuid7_init(NULL, poison), -4);

    build_valid_uuid7(poison, UINT64_C(0xFFFFFFFFFFFF), 0x0FFFu); /* 48-bit maximum      */
    assert_int_equal(uuid7_raise_floor(poison), -4);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0); /* state untouched     */
    assert_int_equal(extract_ms(uuid), 1000u);
    assert_int_equal(extract_seq(uuid), 0u);
}

/**
 * @brief The clock-failure metric stays zero on a healthy clock.
 *
 * It is a process-global monotonic tally; on a healthy system clock_gettime never fails.
 *
 * Pins the accessor + the healthy invariant (the failure itself isn't injectable through the test hooks, which bypass clock_gettime
 * entirely).
 *
 * @verifies UUID7-SRS-027 UUID7-ICD-006
 */
static void test_clock_failure_count_healthy_is_zero(void** state)
{
    (void)state;
    reset_state();
    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0); /* uses the real clock */
    assert_int_equal(uuid7_clock_failure_count(), 0u);
}

/**
 * @brief The public constants carry their documented values.
 *
 * @verifies UUID7-ICD-001 UUID7-ICD-002
 */
static void test_public_constants(void** state)
{
    (void)state;
    _Static_assert(UUID7_SIZE_BYTES == 16u, "UUID7_SIZE_BYTES is 16");
    assert_int_equal(UUID7_SIZE_BYTES, 16u);
    assert_int_equal(UUID7_MAX_AHEAD_MS, 10000u);
}

/*
 * realtime_ms()
 * -------------
 * The wall clock the library reads (CLOCK_REALTIME), in whole milliseconds, for bracketing a generation.
 */
static uint64_t realtime_ms(void)
{
    struct timespec ts;
    assert_int_equal(clock_gettime(CLOCK_REALTIME, &ts), 0);
    return ((uint64_t)ts.tv_sec * UINT64_C(1000)) + ((uint64_t)ts.tv_nsec / UINT64_C(1000000));
}

/**
 * @brief With the real clock, the encoded timestamp lies between two CLOCK_REALTIME reads taken around the call.
 *
 * @verifies UUID7-SRS-006
 */
static void test_timestamp_tracks_realtime(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);

    const uint64_t before                 = realtime_ms();
    uint8_t        uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    const uint64_t after = realtime_ms();

    assert_true(extract_ms(uuid) >= before);
    assert_true(extract_ms(uuid) <= after);
    assert_int_equal(extract_seq(uuid), 0u);
}

/**
 * @brief A wall-clock millisecond later than the last reservation restarts the sequence at zero.
 *
 * @verifies UUID7-SRS-008 UUID7-SRS-004
 */
static void test_new_millisecond_restarts_sequence(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 1000u);
    assert_int_equal(extract_seq(uuid), 0u);
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_seq(uuid), 1u);

    set_fake_time(1001u);
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 1001u);
    assert_int_equal(extract_seq(uuid), 0u);

    set_fake_time(5000u);
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 5000u);
    assert_int_equal(extract_seq(uuid), 0u);
}

/**
 * @brief A reservation that would leave the 48-bit timestamp field is refused with -3 and leaves the state untouched.
 *
 * @verifies UUID7-SRS-012 UUID7-ICD-007
 */
static void test_gen_refuses_48bit_limit(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);

    const uint64_t last_ms = (UINT64_C(1) << 48) - 1u; /* the last millisecond the field can hold */
    set_fake_time(last_ms);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t floor_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(floor_uuid, last_ms, 0x0FFEu);
    assert_int_equal(uuid7_raise_floor(floor_uuid), 0);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0); /* the last pair that fits */
    assert_int_equal(extract_ms(uuid), last_ms);
    assert_int_equal(extract_seq(uuid), 0x0FFFu);

    assert_int_equal(uuid7_gen(uuid), -3); /* the next one needs 2^48 */
    assert_int_equal(uuid7_gen(uuid), -3); /* state untouched */
}

/**
 * @brief Identifiers compare with memcmp() in generation order, across sequence rollovers and clock steps.
 *
 * @verifies UUID7-SRS-013
 */
static void test_ids_sort_in_generation_order(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(fast_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t prev[UUID7_SIZE_BYTES] = {0};
    uint8_t cur[UUID7_SIZE_BYTES]  = {0};
    for(size_t i = 0; i < (TEST_SEQ_SPACE * 2u) + 16u; ++i)
    {
        if(i == 3000u) set_fake_time(1500u);
        if(i == 6000u) set_fake_time(1200u);
        assert_int_equal(uuid7_gen(cur), 0);
        if(i > 0u)
        {
            assert_true(memcmp(prev, cur, UUID7_SIZE_BYTES) < 0);
        }
        memcpy(prev, cur, UUID7_SIZE_BYTES);
    }
}

/*
 * failing_rng()
 * -------------
 * A callback that always reports failure.
 */
static int failing_rng(void* buf, const size_t n)
{
    (void)buf;
    (void)n;
    return -1;
}

/**
 * @brief A failed RNG call returns -2, writes nothing to the output buffer and consumes no sequence value.
 *
 * @verifies UUID7-SRS-023 UUID7-SRS-024 UUID7-ICD-007
 */
static void test_rng_failure_consumes_no_sequence(void** state)
{
    (void)state;
    reset_state();
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_seq(uuid), 0u);

    assert_int_equal(uuid7_set_rng_func(failing_rng), 0);
    memset(uuid, 0xAA, sizeof(uuid));
    assert_int_equal(uuid7_gen(uuid), -2);
    for(size_t i = 0; i < UUID7_SIZE_BYTES; ++i)
    {
        assert_int_equal(uuid[i], 0xAAu); /* nothing written */
    }

    assert_int_equal(uuid7_set_rng_func(NULL), 0);
    assert_int_equal(uuid7_test_set_default_rng_fail(1), 0);
    assert_int_equal(uuid7_gen(uuid), -2);
    assert_int_equal(uuid7_test_set_default_rng_fail(0), 0);

    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 1000u);
    assert_int_equal(extract_seq(uuid), 1u); /* the two failures consumed nothing */
}

/**
 * @brief uuid7_init() installs the RNG before it validates the imported id, so a refused import still selects the RNG.
 *
 * @verifies UUID7-ICD-004 UUID7-SRS-020
 */
static void test_init_installs_rng_before_import(void** state)
{
    (void)state;
    reset_state();
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    const uint8_t script[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    rng_prepare_script(script, sizeof(script), 0x00u);

    uint8_t invalid_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(invalid_uuid, 900u, 1u);
    invalid_uuid[6] = (uint8_t)((0x4u << 4) | (invalid_uuid[6] & TEST_UUID_SEQ_HIGH_MASK));
    assert_int_equal(uuid7_init(scripted_rng, invalid_uuid), -2);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(uuid[9], script[1]);
    assert_int_equal(extract_ms(uuid), 1000u);
}

static _Atomic int g_swap_stop = 0;

/*
 * thread_swap_rng()
 * -----------------
 * Keeps changing the RNG and re-importing an old floor while the generator threads run.
 */
static void* thread_swap_rng(void* arg)
{
    (void)arg;
    uint8_t old_uuid[UUID7_SIZE_BYTES];
    build_valid_uuid7(old_uuid, 1u, 0u);

    for(unsigned int n = 0; !atomic_load_explicit(&g_swap_stop, memory_order_acquire); ++n)
    {
        (void)uuid7_set_rng_func(((n & 1u) != 0u) ? zero_rng : fast_rng);
        if((n % 64u) == 0u)
        {
            (void)uuid7_raise_floor(old_uuid);
            (void)uuid7_init(fast_rng, NULL);
        }
    }
    return NULL;
}

/**
 * @brief RNG changes and floor imports racing with generation never break uniqueness or per-thread order.
 *
 * @verifies UUID7-SRS-029 UUID7-SRS-028
 */
static void test_concurrent_rng_swap(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(fast_rng), 0);
    atomic_store_explicit(&g_swap_stop, 0, memory_order_release);

    enum
    {
        SWAP_GEN_THREADS = 4,
        SWAP_PER_THREAD  = 100000
    };
    const size_t total = (size_t)SWAP_GEN_THREADS * (size_t)SWAP_PER_THREAD;
    uint8_t*     all   = calloc(total, UUID7_SIZE_BYTES);
    assert_non_null(all);

    pthread_t    swapper;
    pthread_t    tids[SWAP_GEN_THREADS];
    thread_ctx_t ctx[SWAP_GEN_THREADS];
    assert_int_equal(pthread_create(&swapper, NULL, thread_swap_rng, NULL), 0);
    for(size_t i = 0; i < (size_t)SWAP_GEN_THREADS; ++i)
    {
        ctx[i].buf   = all + (i * (size_t)SWAP_PER_THREAD * UUID7_SIZE_BYTES);
        ctx[i].count = (size_t)SWAP_PER_THREAD;
        ctx[i].error = 0;
        assert_int_equal(pthread_create(&tids[i], NULL, thread_generate, &ctx[i]), 0);
    }
    for(size_t i = 0; i < (size_t)SWAP_GEN_THREADS; ++i)
    {
        pthread_join(tids[i], NULL);
        assert_int_equal(ctx[i].error, 0);
    }
    atomic_store_explicit(&g_swap_stop, 1, memory_order_release);
    pthread_join(swapper, NULL);

    qsort(all, total, UUID7_SIZE_BYTES, cmp_uuid);
    for(size_t i = 1; i < total; ++i)
    {
        assert_true(memcmp(all + ((i - 1u) * UUID7_SIZE_BYTES), all + (i * UUID7_SIZE_BYTES), UUID7_SIZE_BYTES) != 0);
    }
    free(all);
}

/**
 * @brief uuid7_raise_floor() refuses an identifier that is not version 7 (-2) or not variant 0b10 (-3) and keeps the state.
 *
 * @verifies UUID7-SRS-017 UUID7-ICD-005
 */
static void test_raise_floor_rejects_invalid(void** state)
{
    (void)state;
    reset_state();
    assert_int_equal(uuid7_set_rng_func(zero_rng), 0);
    set_fake_time(1000u);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t bad[UUID7_SIZE_BYTES];
    build_valid_uuid7(bad, 900u, 3u);
    bad[6] = (uint8_t)((0x4u << 4) | (bad[6] & TEST_UUID_SEQ_HIGH_MASK));
    assert_int_equal(uuid7_raise_floor(bad), -2);
    build_valid_uuid7(bad, 900u, 3u);
    bad[8] = 0xC0u;
    assert_int_equal(uuid7_raise_floor(bad), -3);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), 1000u);
    assert_int_equal(extract_seq(uuid), 0u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_raise_floor_orders_after_import),
        cmocka_unit_test(test_gen_refuses_beyond_ahead_bound),
        cmocka_unit_test(test_raise_floor_refuses_far_future),
        cmocka_unit_test(test_clock_failure_count_healthy_is_zero),
        cmocka_unit_test(test_default_rng_used_when_uninitialized),
        cmocka_unit_test(test_version_variant_and_tail_bytes),
        cmocka_unit_test(test_timestamp_matches_override),
        cmocka_unit_test(test_monotonic_non_decreasing_many),
        cmocka_unit_test(test_overflow_advances_logical_ms),
        cmocka_unit_test(test_null_buffer_rejected),
        cmocka_unit_test(test_rng_reset_to_default),
        cmocka_unit_test(test_init_accepts_custom_rng),
        cmocka_unit_test(test_init_null_uses_default_rng),
        cmocka_unit_test(test_init_raises_state_from_last_uuid),
        cmocka_unit_test(test_repeated_init_does_not_move_state_backward),
        cmocka_unit_test(test_init_rejects_invalid_import_version),
        cmocka_unit_test(test_init_rejects_invalid_import_variant),
        cmocka_unit_test(test_default_rng_failure_returns_minus2),
        cmocka_unit_test(test_rng_failure_ignored_for_custom_rng),
        cmocka_unit_test(test_multithreaded_uniqueness),
        cmocka_unit_test(test_time_regression_monotonic),
        cmocka_unit_test(test_overflow_multiple_ms_advances),
        cmocka_unit_test(test_heavy_single_thread_uniqueness),
        cmocka_unit_test(test_public_constants),
        cmocka_unit_test(test_timestamp_tracks_realtime),
        cmocka_unit_test(test_new_millisecond_restarts_sequence),
        cmocka_unit_test(test_gen_refuses_48bit_limit),
        cmocka_unit_test(test_ids_sort_in_generation_order),
        cmocka_unit_test(test_rng_failure_consumes_no_sequence),
        cmocka_unit_test(test_init_installs_rng_before_import),
        cmocka_unit_test(test_concurrent_rng_swap),
        cmocka_unit_test(test_raise_floor_rejects_invalid),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
