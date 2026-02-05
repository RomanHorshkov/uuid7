#include "uuid7.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* Test-only hooks provided by uuid7.c when compiled with -DUUID7_TESTING. */
int uuid7_test_set_time_fn(uint64_t (*fn)(void));
int uuid7_test_set_default_rng_fail(int enable);
void uuid7_test_reset_state(void);

typedef struct scripted_rng_ctx
{
    uint8_t script[256];
    size_t script_len;
    size_t cursor;
    uint8_t fallback;
} scripted_rng_ctx_t;

static scripted_rng_ctx_t g_rng_ctx;
static _Atomic uint32_t g_fast_rng_state = 0x12345678u;
static _Atomic uint64_t g_fake_time_ms = 0;
static _Atomic uint32_t g_time_flip = 0;

static void scripted_rng(void* buf, const size_t n)
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
}

static void rng_load_script(const uint8_t* data, size_t len, uint8_t fallback_start)
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
    g_rng_ctx.cursor = 0;
    g_rng_ctx.fallback = fallback_start;
    uuid7_set_rng(scripted_rng);
}

static void fast_rng(void* buf, const size_t n)
{
    uint8_t* out = (uint8_t*)buf;
    for(size_t i = 0; i < n; ++i)
    {
        uint32_t v = atomic_fetch_add_explicit(&g_fast_rng_state, 1u, memory_order_relaxed);
        v = v * 1103515245u + 12345u;
        out[i] = (uint8_t)(v >> 24);
    }
}

static void zero_rng(void* buf, const size_t n)
{
    memset(buf, 0, n);
}

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

static uint64_t extract_ms(const uint8_t uuid[16])
{
    uint64_t ms = 0;
    for(size_t i = 0; i < 6; ++i)
    {
        ms = (ms << 8) | uuid[i];
    }
    return ms;
}

static uint16_t extract_seq(const uint8_t uuid[16])
{
    return (uint16_t)(((uint16_t)(uuid[6] & 0x0Fu) << 8) | uuid[7]);
}

static void reset_state(void)
{
    uuid7_test_reset_state();
    memset(&g_rng_ctx, 0, sizeof(g_rng_ctx));
    atomic_store_explicit(&g_fast_rng_state, 0x12345678u, memory_order_relaxed);
    atomic_store_explicit(&g_fake_time_ms, 0, memory_order_relaxed);
    atomic_store_explicit(&g_time_flip, 0, memory_order_relaxed);
}

static void test_default_rng_used_when_uninitialized(void** state)
{
    (void)state;
    reset_state();

    uint8_t uuid[16] = {0};
    const int rc = uuid7_gen(uuid);
    assert_true(rc == 0 || rc == -2);
    if(rc != 0) return;

    assert_int_equal((uuid[6] & 0xF0), 0x70);
    assert_int_equal((uuid[8] & 0xC0), 0x80);
}

static void test_sequence_never_zero(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x00, 0x00, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x11, 0x22};
    rng_load_script(script, sizeof(script), 0x80);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_not_equal(extract_seq(uuid), 0);
}

static void test_version_variant_and_tail_bytes(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x12, 0x34, 0xAA, 0xBC, 0xCD, 0xDE, 0xEF, 0x01, 0x23, 0x45};
    rng_load_script(script, sizeof(script), 0x10);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_int_equal((uuid[6] & 0xF0), 0x70);
    assert_int_equal((uuid[8] & 0xC0), 0x80);

    const uint8_t rb0 = script[2];
    const uint8_t expected_variant = (uint8_t)((rb0 & 0x3Fu) | 0x80u);
    assert_int_equal(uuid[8], expected_variant);

    for(size_t i = 0; i < 7; ++i)
    {
        assert_int_equal(uuid[9 + i], script[3 + i]);
    }
}

static void test_timestamp_matches_override(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x55);
    set_fake_time(0x010203040506ull);
    uuid7_test_set_time_fn(fake_time_now);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    const uint64_t ms = extract_ms(uuid);
    assert_int_equal(ms, 0x010203040506ull);
}

static void test_monotonic_non_decreasing_many(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x20);
    set_fake_time(42);
    uuid7_test_set_time_fn(fake_time_now);

    uint64_t prev = 0;
    for(size_t i = 0; i < 1000; ++i)
    {
        uint8_t uuid[16] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);
        const uint64_t ms = extract_ms(uuid);
        const uint16_t seq = extract_seq(uuid);
        const uint64_t packed = (ms << 12) | seq;
        if(i > 0)
        {
            assert_true(packed > prev);
        }
        prev = packed;
    }
}

static void test_overflow_advances_ms(void** state)
{
    (void)state;
    reset_state();

    uuid7_set_rng(zero_rng);
    set_fake_time(1000);
    uuid7_test_set_time_fn(fake_time_now);

    uint64_t ms = 0;
    uint16_t seq = 0;

    for(size_t i = 0; i < 4096; ++i)
    {
        uint8_t uuid[16] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);
        ms = extract_ms(uuid);
        seq = extract_seq(uuid);

        if(i == 0)
        {
            assert_int_equal(ms, 1000);
            assert_int_equal(seq, 1);
        }
        if(i == 4094)
        {
            assert_int_equal(ms, 1000);
            assert_int_equal(seq, 0x0FFF);
        }
        if(i == 4095)
        {
            assert_int_equal(ms, 1001);
            assert_int_equal(seq, 1);
        }
    }
}

static void test_null_buffer_rejected(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x20);
    assert_int_equal(uuid7_gen(NULL), -1);
}

static void test_rng_reset_to_default(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x10, 0x20, 0x30, 0x40};
    rng_load_script(script, sizeof(script), 0x50);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_int_equal(uuid7_set_rng(NULL), 0);
    const int rc = uuid7_gen(uuid);
    assert_true(rc == 0 || rc == -2);
    if(rc != 0) return;

    assert_int_equal((uuid[8] & 0xC0), 0x80);
}

static void test_init_accepts_custom_rng(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x01, 0x02, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA};
    rng_load_script(script, sizeof(script), 0x60);
    assert_int_equal(uuid7_init(scripted_rng), 0);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(uuid[9], script[3]);
}

static void test_init_null_leaves_existing_rng(void** state)
{
    (void)state;
    reset_state();

    const uint8_t script[] = {0x22, 0x44, 0x66, 0x88, 0xAA, 0xCC, 0xEE, 0xFF};
    rng_load_script(script, sizeof(script), 0x70);
    assert_int_equal(uuid7_init(NULL), 0);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(uuid[9], script[3]);
}

static void test_default_rng_failure_returns_minus2(void** state)
{
    (void)state;
    reset_state();

    assert_int_equal(uuid7_set_rng(NULL), 0);
    assert_int_equal(uuid7_test_set_default_rng_fail(1), 0);

    uint8_t uuid[16];
    memset(uuid, 0xAA, sizeof(uuid));
    assert_int_equal(uuid7_gen(uuid), -2);

    assert_int_equal(uuid7_test_set_default_rng_fail(0), 0);
}

static void test_rng_failure_ignored_for_custom_rng(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x90);
    assert_int_equal(uuid7_test_set_default_rng_fail(1), 0);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_int_equal(uuid7_test_set_default_rng_fail(0), 0);
}

typedef struct thread_ctx
{
    uint8_t* buf;
    size_t count;
    int error;
} thread_ctx_t;

static void* thread_generate(void* arg)
{
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    uint64_t prev = 0;

    for(size_t i = 0; i < ctx->count; ++i)
    {
        uint8_t* uuid = ctx->buf + i * 16u;
        const int rc = uuid7_gen(uuid);
        if(rc != 0)
        {
            ctx->error = rc;
            return NULL;
        }

        const uint64_t ms = extract_ms(uuid);
        const uint16_t seq = extract_seq(uuid);
        const uint64_t packed = (ms << 12) | seq;
        if(i > 0 && packed <= prev)
        {
            ctx->error = -3;
            return NULL;
        }
        prev = packed;

        if((uuid[6] & 0xF0) != 0x70 || (uuid[8] & 0xC0) != 0x80)
        {
            ctx->error = -4;
            return NULL;
        }
    }

    return NULL;
}

static int cmp_uuid(const void* a, const void* b)
{
    return memcmp(a, b, 16);
}

static void test_multithreaded_uniqueness(void** state)
{
    (void)state;
    reset_state();

    uuid7_set_rng(fast_rng);

    const size_t threads = 16u;
    const size_t per_thread = 100000u;
    const size_t total = threads * per_thread;

    uint8_t* all = calloc(total, 16u);
    assert_non_null(all);

    pthread_t tids[threads];
    thread_ctx_t ctx[threads];

    for(size_t i = 0; i < threads; ++i)
    {
        ctx[i].buf = all + (i * per_thread * 16u);
        ctx[i].count = per_thread;
        ctx[i].error = 0;
        assert_int_equal(pthread_create(&tids[i], NULL, thread_generate, &ctx[i]), 0);
    }

    for(size_t i = 0; i < threads; ++i)
    {
        pthread_join(tids[i], NULL);
        assert_int_equal(ctx[i].error, 0);
    }

    qsort(all, total, 16u, cmp_uuid);
    for(size_t i = 1; i < total; ++i)
    {
        assert_true(memcmp(all + (i - 1) * 16u, all + i * 16u, 16u) != 0);
    }

    free(all);
}

static void test_time_regression_monotonic(void** state)
{
    (void)state;
    reset_state();

    rng_load_script(NULL, 0, 0x33);
    uuid7_test_set_time_fn(time_regress_now);

    uint64_t prev = 0;
    for(size_t i = 0; i < 2000; ++i)
    {
        uint8_t uuid[16] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);
        const uint64_t ms = extract_ms(uuid);
        const uint16_t seq = extract_seq(uuid);
        const uint64_t packed = (ms << 12) | seq;
        if(i > 0)
        {
            assert_true(packed > prev);
        }
        prev = packed;
    }
}

static void test_overflow_multiple_ms_advances(void** state)
{
    (void)state;
    reset_state();

    uuid7_set_rng(zero_rng);
    set_fake_time(1000);
    uuid7_test_set_time_fn(fake_time_now);

    const size_t total = 4095u * 3u;
    for(size_t i = 0; i < total; ++i)
    {
        uint8_t uuid[16] = {0};
        assert_int_equal(uuid7_gen(uuid), 0);

        const uint64_t ms = extract_ms(uuid);
        const uint16_t seq = extract_seq(uuid);

        if(i == 0)
        {
            assert_int_equal(ms, 1000);
            assert_int_equal(seq, 1);
        }
        if(i == 4094)
        {
            assert_int_equal(ms, 1000);
            assert_int_equal(seq, 0x0FFF);
        }
        if(i == 4095)
        {
            assert_int_equal(ms, 1001);
            assert_int_equal(seq, 1);
        }
        if(i == 8189)
        {
            assert_int_equal(ms, 1001);
            assert_int_equal(seq, 0x0FFF);
        }
        if(i == 8190)
        {
            assert_int_equal(ms, 1002);
            assert_int_equal(seq, 1);
        }
    }
}

static void test_heavy_single_thread_uniqueness(void** state)
{
    (void)state;
    reset_state();

    uuid7_set_rng(fast_rng);

    const size_t total = 1000000u;
    uint8_t* all = calloc(total, 16u);
    assert_non_null(all);

    for(size_t i = 0; i < total; ++i)
    {
        uint8_t* uuid = all + i * 16u;
        assert_int_equal(uuid7_gen(uuid), 0);
        assert_int_equal((uuid[6] & 0xF0), 0x70);
        assert_int_equal((uuid[8] & 0xC0), 0x80);
    }

    qsort(all, total, 16u, cmp_uuid);
    for(size_t i = 1; i < total; ++i)
    {
        assert_true(memcmp(all + (i - 1) * 16u, all + i * 16u, 16u) != 0);
    }

    free(all);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_rng_used_when_uninitialized),
        cmocka_unit_test(test_sequence_never_zero),
        cmocka_unit_test(test_version_variant_and_tail_bytes),
        cmocka_unit_test(test_timestamp_matches_override),
        cmocka_unit_test(test_monotonic_non_decreasing_many),
        cmocka_unit_test(test_overflow_advances_ms),
        cmocka_unit_test(test_null_buffer_rejected),
        cmocka_unit_test(test_rng_reset_to_default),
        cmocka_unit_test(test_init_accepts_custom_rng),
        cmocka_unit_test(test_init_null_leaves_existing_rng),
        cmocka_unit_test(test_default_rng_failure_returns_minus2),
        cmocka_unit_test(test_rng_failure_ignored_for_custom_rng),
        cmocka_unit_test(test_multithreaded_uniqueness),
        cmocka_unit_test(test_time_regression_monotonic),
        cmocka_unit_test(test_overflow_multiple_ms_advances),
        cmocka_unit_test(test_heavy_single_thread_uniqueness),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
