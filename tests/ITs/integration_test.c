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

/* Test-only hooks provided by uuid7.c when compiled with -DUUID7_TESTING. */
int  uuid7_test_set_time_fn(uint64_t (*fn)(void));
int  uuid7_test_set_default_rng_fail(int enable);
void uuid7_test_reset_state(void);

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

/*
 * test_default_rng_used_when_uninitialized()
 * ------------------------------------------
 * uuid7_gen() must remain usable even if the caller never explicitly calls uuid7_init() or uuid7_set_rng_func().
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

/*
 * test_version_variant_and_tail_bytes()
 * -------------------------------------
 * Verify the exact byte mapping of the UUID tail:
 * - byte 8 stores variant bits overlaid onto scripted random byte 0
 * - bytes 9..15 store scripted random bytes 1..7 unchanged
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

/*
 * test_timestamp_matches_override()
 * ---------------------------------
 * The encoded 48-bit timestamp must match the test-time override exactly.
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

/*
 * test_monotonic_non_decreasing_many()
 * ------------------------------------
 * With a fixed millisecond, packed (ms, seq) state must be strictly increasing for every generated UUID.
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

/*
 * test_overflow_advances_logical_ms()
 * -----------------------------------
 * A single logical millisecond can hold TEST_SEQ_SPACE UUIDs because the sequence field is 12 bits wide (0..4095). The next UUID must roll
 * into the next logical millisecond and restart the sequence at zero.
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

/*
 * test_null_buffer_rejected()
 * ---------------------------
 * The generator must reject NULL output buffers deterministically.
 */
static void test_null_buffer_rejected(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x20u);
    assert_int_equal(uuid7_gen(NULL), -1);
}

/*
 * test_rng_reset_to_default()
 * ---------------------------
 * uuid7_set_rng_func(NULL) must restore the built-in default RNG.
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

/*
 * test_init_accepts_custom_rng()
 * ------------------------------
 * uuid7_init(fn, NULL) must install the supplied RNG callback.
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

/*
 * test_init_null_uses_default_rng()
 * ---------------------------------
 * Passing NULL as the RNG in uuid7_init() must actively restore the default RNG, not leave a previously installed custom RNG in place.
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

/*
 * test_init_raises_state_from_last_uuid()
 * ---------------------------------------
 * Importing a previous UUID through uuid7_init() must raise the internal monotonic floor so the next generated UUID is strictly newer.
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

/*
 * test_repeated_init_does_not_move_state_backward()
 * -------------------------------------------------
 * Re-running uuid7_init() with an older imported UUID must never rewind the generator's monotonic state.
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

/*
 * test_init_rejects_invalid_import_version()
 * ------------------------------------------
 * Imported state must be rejected when the version nibble is not 7.
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

/*
 * test_init_rejects_invalid_import_variant()
 * ------------------------------------------
 * Imported state must be rejected when the RFC variant bits are not 10xxxxxx.
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

/*
 * test_default_rng_failure_returns_minus2()
 * -----------------------------------------
 * The public error contract for RNG failure is -2.
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

/*
 * test_rng_failure_ignored_for_custom_rng()
 * -----------------------------------------
 * The default-RNG failure hook must not affect callers that install a custom RNG implementation.
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

/*
 * test_multithreaded_uniqueness()
 * -------------------------------
 * Concurrent callers must never receive duplicate UUIDs.
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

/*
 * test_time_regression_monotonic()
 * --------------------------------
 * Even if the wall clock moves backward, the packed monotonic state must keep increasing.
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

/*
 * test_overflow_multiple_ms_advances()
 * ------------------------------------
 * Repeated sequence exhaustion must keep advancing the logical millisecond one step at a time with the sequence restarting from zero on
 * each rollover.
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

/*
 * test_heavy_single_thread_uniqueness()
 * -------------------------------------
 * Stress the generator in one thread and confirm all produced UUIDs are unique after lexicographic sorting.
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

int main(void)
{
    const struct CMUnitTest tests[] = {
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
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
