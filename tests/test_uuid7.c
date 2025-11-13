#include "uuid7.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

typedef struct scripted_rng_ctx
{
    uint8_t script[128];
    size_t script_len;
    size_t cursor;
    uint8_t fallback;
} scripted_rng_ctx_t;

static scripted_rng_ctx_t g_rng_ctx;

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

static void test_default_rng_used_when_uninitialized(void** state)
{
    (void)state;
    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal((uuid[6] & 0xF0), 0x70);
    assert_int_equal((uuid[8] & 0xC0), 0x80);
}

static void test_sequence_never_zero(void** state)
{
    (void)state;
    const uint8_t script[] = {0x00, 0x00, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x11, 0x22};
    rng_load_script(script, sizeof(script), 0x80);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_not_equal(extract_seq(uuid), 0);
}

static void test_version_and_variant_bits(void** state)
{
    (void)state;
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

static void test_monotonic_non_decreasing(void** state)
{
    (void)state;
    rng_load_script(NULL, 0, 0x40);

    uint8_t first[16] = {0};
    uint8_t second[16] = {0};

    assert_int_equal(uuid7_gen(first), 0);
    assert_int_equal(uuid7_gen(second), 0);

    const uint64_t ms_a = extract_ms(first);
    const uint64_t ms_b = extract_ms(second);
    const uint16_t seq_a = extract_seq(first);
    const uint16_t seq_b = extract_seq(second);

    const bool monotonic = (ms_b > ms_a) || (ms_b == ms_a && seq_b > seq_a);
    assert_true(monotonic);
}

static void test_null_buffer_rejected(void** state)
{
    (void)state;
    rng_load_script(NULL, 0, 0x20);
    assert_int_equal(uuid7_gen(NULL), -1);
}

static void test_set_rng_can_reset_to_default(void** state)
{
    (void)state;
    const uint8_t script[] = {0x10, 0x20, 0x30, 0x40};
    rng_load_script(script, sizeof(script), 0x50);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);

    assert_int_equal(uuid7_set_rng(NULL), 0);
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal((uuid[8] & 0xC0), 0x80);
}

static void test_init_accepts_custom_rng(void** state)
{
    (void)state;
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
    const uint8_t script[] = {0x22, 0x44, 0x66, 0x88, 0xAA, 0xCC, 0xEE, 0xFF};
    rng_load_script(script, sizeof(script), 0x70);
    assert_int_equal(uuid7_init(NULL), 0);

    uint8_t uuid[16] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(uuid[9], script[3]);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_rng_used_when_uninitialized),
        cmocka_unit_test(test_sequence_never_zero),
        cmocka_unit_test(test_version_and_variant_bits),
        cmocka_unit_test(test_monotonic_non_decreasing),
        cmocka_unit_test(test_null_buffer_rejected),
        cmocka_unit_test(test_set_rng_can_reset_to_default),
        cmocka_unit_test(test_init_accepts_custom_rng),
        cmocka_unit_test(test_init_null_leaves_existing_rng),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
