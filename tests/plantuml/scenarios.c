/**
 * @file scenarios.c
 * @brief Scenario tests: real CMocka tests that explain the library, each one recorded as a PlantUML sequence diagram.
 *
 * Every scenario asserts what it shows. The diagrams are written by the tracing layer (puml_trace.h) to `build/ecss/puml/<scenario>.puml`
 * and rendered by ecss/make.sh. The clock and the RNG are the trace's deterministic ones, so a diagram changes only when the library's
 * behaviour does.
 *
 * @author  Roman Horshkov <https://github.com/RomanHorshkov>
 * @date    october 2026
 */

#include "puml_trace.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

#include <cmocka.h>

/* A realistic wall clock (2026-10-10 12:00:00.000 UTC) keeps the diagrams readable and stable. */
#define SD_T0_MS UINT64_C(1791633600000)

/* Starts a scenario: clean generator, the trace's clock and RNG installed, the diagram open. */
static void sd_start(const char* name, const char* title, uint64_t clock_ms)
{
    uuid7_test_reset_state();
    atomic_store(&g_puml.rng_fail, 0);
    atomic_store(&g_puml.rng_counter, 0x10u);
    PUML_SET_CLOCK(clock_ms);
    uuid7_test_set_time_fn(puml_clock);
    assert_int_equal(PUML_BEGIN(name, title), 0);
}

/* Builds a valid floor identifier (version 7, variant 0b10, zero tail) for (ms, seq). */
static void sd_floor(uint8_t id[UUID7_SIZE_BYTES], uint64_t ms, unsigned seq)
{
    memset(id, 0, UUID7_SIZE_BYTES);
    for(size_t i = 0; i < 6u; ++i)
    {
        id[i] = (uint8_t)((ms >> (8u * (5u - i))) & 0xFFu);
    }
    id[6] = (uint8_t)(0x70u | ((seq >> 8) & 0x0Fu));
    id[7] = (uint8_t)(seq & 0xFFu);
    id[8] = 0x80u;
}

/**
 * @brief Generate one identifier: the RNG is called before the clock, and the fields land where RFC 9562 puts them.
 *
 * @verifies UUID7-SRS-001 UUID7-SRS-002 UUID7-SRS-003 UUID7-SRS-004 UUID7-SRS-005 UUID7-SRS-023 UUID7-ICD-007
 */
static void sd_generate_one(void** state)
{
    (void)state;
    sd_start("sd_generate_one", "Generate one identifier", SD_T0_MS);

    assert_int_equal(PUML_INIT(puml_rng, NULL), 0);
    uint8_t id[UUID7_SIZE_BYTES];
    assert_int_equal(PUML_GEN(id), 0);
    assert_true(g_puml.last_rng_at < g_puml.last_clock_at); /* entropy first, then the reservation */

    assert_int_equal(puml_id_ms(id), SD_T0_MS);
    assert_int_equal(puml_id_seq(id), 0u);
    assert_int_equal(id[6] & 0xF0u, 0x70u);
    assert_int_equal(id[8] & 0xC0u, 0x80u);
    assert_int_equal(id[8] & 0x3Fu, 0x10u & 0x3Fu);
    assert_int_equal(id[9], 0x11u);
    assert_int_equal(id[15], 0x17u);

    PUML_NOTE("C",
              "bytes 0..5  unix_ts_ms = %llu (big-endian)\n"
              "byte 6      version 7 | rand_a[11:8]\n"
              "byte 7      rand_a[7:0] = sequence %u\n"
              "byte 8      variant 0b10 | rand_b[61:56]\n"
              "bytes 9..15 rand_b[55:0] from the RNG\n"
              "(the text form is the caller's)",
              (unsigned long long)puml_id_ms(id), puml_id_seq(id));
    PUML_END();
}

/**
 * @brief Many identifiers in one millisecond: the sequence counts, and 4096 of them move the logical clock one millisecond ahead.
 *
 * @verifies UUID7-SRS-004 UUID7-SRS-007 UUID7-SRS-010 UUID7-SRS-030
 */
static void sd_same_millisecond(void** state)
{
    (void)state;
    sd_start("sd_same_millisecond", "Many identifiers in one millisecond", SD_T0_MS);
    assert_int_equal(PUML_SET_RNG(puml_rng), 0);

    uint8_t id[UUID7_SIZE_BYTES];
    for(unsigned i = 0; i < 3u; ++i)
    {
        assert_int_equal(PUML_GEN(id), 0);
        assert_int_equal(puml_id_ms(id), SD_T0_MS);
        assert_int_equal(puml_id_seq(id), i);
    }

    PUML_GROUP("loop", "4093 more calls, clock frozen (not traced)");
    PUML_QUIET(1);
    uint8_t prev[UUID7_SIZE_BYTES];
    memcpy(prev, id, sizeof(id));
    for(unsigned i = 3; i < 4096u; ++i)
    {
        assert_int_equal(uuid7_gen(id), 0);
        assert_true(memcmp(prev, id, UUID7_SIZE_BYTES) < 0);
        memcpy(prev, id, sizeof(id));
    }
    PUML_QUIET(0);
    PUML_NOTE("L", "last one: (ms=%llu, seq=%u)\nthe 12-bit sequence is exhausted", (unsigned long long)puml_id_ms(id), puml_id_seq(id));
    PUML_GROUP_END();
    assert_int_equal(puml_id_seq(id), 4095u);

    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_ms(id), SD_T0_MS + 1u);
    assert_int_equal(puml_id_seq(id), 0u);
    PUML_NOTE("L", "no wait for the wall clock:\nthe logical clock runs 1 ms ahead (bounded by UUID7_MAX_AHEAD_MS)");
    PUML_END();
}

/**
 * @brief The wall clock stalls or steps back: the generator continues from its state, and resumes the clock once it is ahead again.
 *
 * @verifies UUID7-SRS-007 UUID7-SRS-008 UUID7-SRS-009
 */
static void sd_clock_backwards(void** state)
{
    (void)state;
    sd_start("sd_clock_backwards", "The wall clock stalls and steps back", SD_T0_MS);
    assert_int_equal(PUML_SET_RNG(puml_rng), 0);

    uint8_t id[UUID7_SIZE_BYTES];
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_seq(id), 0u);

    PUML_DIVIDER("NTP steps the clock back 500 ms");
    PUML_SET_CLOCK(SD_T0_MS - 500u);
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_ms(id), SD_T0_MS);
    assert_int_equal(puml_id_seq(id), 1u);
    PUML_NOTE("L", "clock behind the state:\nsame millisecond, next sequence");

    PUML_DIVIDER("the clock catches up and passes the state");
    PUML_SET_CLOCK(SD_T0_MS + 1u);
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_ms(id), SD_T0_MS + 1u);
    assert_int_equal(puml_id_seq(id), 0u);
    PUML_NOTE("L", "new millisecond: sequence restarts at 0");
    PUML_END();
}

/**
 * @brief The RNG fails: -2, nothing written, no clock read, no sequence consumed; the next success takes the next sequence.
 *
 * @verifies UUID7-SRS-023 UUID7-SRS-024 UUID7-ICD-007
 */
static void sd_entropy_failure(void** state)
{
    (void)state;
    sd_start("sd_entropy_failure", "The entropy source fails", SD_T0_MS);
    assert_int_equal(PUML_SET_RNG(puml_rng), 0);

    uint8_t id[UUID7_SIZE_BYTES];
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_seq(id), 0u);

    PUML_DIVIDER("the entropy source fails");
    atomic_store(&g_puml.rng_fail, 1);
    memset(id, 0xAA, sizeof(id));
    const uint32_t clock_reads = g_puml.last_clock_at;
    assert_int_equal(PUML_GEN(id), -2);
    assert_int_equal(g_puml.last_clock_at, clock_reads); /* no reservation attempted */
    for(size_t i = 0; i < sizeof(id); ++i)
    {
        assert_int_equal(id[i], 0xAAu);
    }
    PUML_NOTE("L", "no clock read, no reservation:\nthe buffer is untouched, no sequence value is consumed");

    PUML_DIVIDER("the entropy source recovers");
    atomic_store(&g_puml.rng_fail, 0);
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_seq(id), 1u);
    PUML_END();
}

/**
 * @brief Persist the newest identifier, restart behind the clock, import it as the floor: new identifiers still sort after it.
 *
 * @verifies UUID7-SRS-014 UUID7-SRS-015 UUID7-SRS-016 UUID7-ICD-005
 */
static void sd_restart_with_floor(void** state)
{
    (void)state;
    sd_start("sd_restart_with_floor", "Persist, restart, import the floor", SD_T0_MS);
    assert_int_equal(PUML_INIT(puml_rng, NULL), 0);

    uint8_t id[UUID7_SIZE_BYTES];
    uint8_t stored[UUID7_SIZE_BYTES];
    for(unsigned i = 0; i < 3u; ++i)
    {
        assert_int_equal(PUML_GEN(id), 0);
    }
    memcpy(stored, id, sizeof(id));
    PUML_NOTE("C", "the caller stores the newest key\n(ms=%llu, seq=%u)", (unsigned long long)puml_id_ms(stored), puml_id_seq(stored));

    PUML_DIVIDER("process restart; the restored host clock is 2 s behind");
    puml_line("C -> L : <<test seam>> uuid7_test_reset_state()");
    uuid7_test_reset_state();
    uuid7_test_set_time_fn(puml_clock);
    PUML_SET_CLOCK(SD_T0_MS - 2000u);

    assert_int_equal(PUML_INIT(puml_rng, NULL), 0);
    assert_int_equal(PUML_RAISE_FLOOR(stored), 0);
    PUML_NOTE("L", "state raised to the stored pair (raise only)");

    uint8_t older[UUID7_SIZE_BYTES];
    sd_floor(older, SD_T0_MS - 60000u, 0u);
    assert_int_equal(PUML_RAISE_FLOOR(older), 0);
    PUML_NOTE("L", "an older floor changes nothing");

    assert_int_equal(PUML_GEN(id), 0);
    assert_true(memcmp(stored, id, UUID7_SIZE_BYTES) < 0);
    assert_int_equal(puml_id_ms(id), SD_T0_MS);
    assert_int_equal(puml_id_seq(id), 3u);
    PUML_NOTE("C", "sorts after the stored key\nalthough the clock is behind");
    PUML_END();
}

/**
 * @brief A poisoned floor is refused with -4; borrowing time stops at UUID7_MAX_AHEAD_MS with -3 until the clock catches up.
 *
 * @verifies UUID7-SRS-011 UUID7-SRS-018 UUID7-ICD-005 UUID7-ICD-007
 */
static void sd_abuse_bounds(void** state)
{
    (void)state;
    sd_start("sd_abuse_bounds", "A poisoned floor and the ahead-of-clock bound", SD_T0_MS);
    assert_int_equal(PUML_SET_RNG(puml_rng), 0);

    uint8_t floor_id[UUID7_SIZE_BYTES];
    sd_floor(floor_id, SD_T0_MS + (uint64_t)UUID7_MAX_AHEAD_MS + 1u, 0u);
    assert_int_equal(PUML_RAISE_FLOOR(floor_id), -4);
    PUML_NOTE("L", "dated beyond now + UUID7_MAX_AHEAD_MS (%u ms):\nrefused, state untouched", (unsigned)UUID7_MAX_AHEAD_MS);

    sd_floor(floor_id, SD_T0_MS + (uint64_t)UUID7_MAX_AHEAD_MS, 4094u);
    assert_int_equal(PUML_RAISE_FLOOR(floor_id), 0);

    uint8_t id[UUID7_SIZE_BYTES];
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_ms(id), SD_T0_MS + (uint64_t)UUID7_MAX_AHEAD_MS);
    assert_int_equal(puml_id_seq(id), 4095u);
    assert_int_equal(PUML_GEN(id), -3);
    PUML_NOTE("L", "the next pair needs now + bound + 1:\nrefused, state untouched; the caller backs off");

    PUML_DIVIDER("1 ms later");
    PUML_SET_CLOCK(SD_T0_MS + 1u);
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(puml_id_ms(id), SD_T0_MS + (uint64_t)UUID7_MAX_AHEAD_MS + 1u);
    assert_int_equal(puml_id_seq(id), 0u);
    PUML_END();
}

/** @brief One worker of sd_concurrent_generation(). */
typedef struct sd_worker
{
    uint8_t* ids;   /**< This worker's slice. */
    size_t   count; /**< Identifiers to generate. */
    int      error; /**< First failure: a return code, or 1 for an order violation. */
} sd_worker_t;

static void* sd_worker_main(void* arg)
{
    sd_worker_t* w = (sd_worker_t*)arg;
    for(size_t i = 0; i < w->count; ++i)
    {
        uint8_t*  id = w->ids + (i * UUID7_SIZE_BYTES);
        const int rc = uuid7_gen(id);
        if(rc != 0)
        {
            w->error = rc;
            return NULL;
        }
        if(i > 0u && memcmp(id - UUID7_SIZE_BYTES, id, UUID7_SIZE_BYTES) >= 0)
        {
            w->error = 1;
            return NULL;
        }
    }
    return NULL;
}

static int sd_cmp(const void* a, const void* b)
{
    return memcmp(a, b, UUID7_SIZE_BYTES);
}

/**
 * @brief Four threads generate at once: each sees its own strictly increasing sequence, and no identifier repeats across threads.
 *
 * @verifies UUID7-SRS-028 UUID7-SRS-007
 */
static void sd_concurrent_generation(void** state)
{
    (void)state;
    enum
    {
        SD_WORKERS    = 4,
        SD_PER_WORKER = 10000
    };
    sd_start("sd_concurrent_generation", "Four threads generate at once", SD_T0_MS);
    puml_line("participant \"Worker 1..4\\n(pthreads)\" as W");
    assert_int_equal(PUML_SET_RNG(puml_rng), 0);

    uint8_t* all = calloc((size_t)SD_WORKERS * (size_t)SD_PER_WORKER, UUID7_SIZE_BYTES);
    assert_non_null(all);
    pthread_t   tid[SD_WORKERS];
    sd_worker_t w[SD_WORKERS];

    puml_line("C -> W : pthread_create() x %d", SD_WORKERS);
    PUML_GROUP("par", "each worker, concurrently (callbacks not traced)");
    puml_line("loop %d times", SD_PER_WORKER);
    puml_line("W -> L : uuid7_gen(id)");
    puml_line("L -> L : compare-and-swap the (ms, seq) word;\\nretry only if another thread won");
    puml_line("L --> W : 0");
    PUML_GROUP_END();
    PUML_GROUP_END();

    PUML_QUIET(1);
    for(size_t i = 0; i < (size_t)SD_WORKERS; ++i)
    {
        w[i].ids   = all + (i * (size_t)SD_PER_WORKER * UUID7_SIZE_BYTES);
        w[i].count = (size_t)SD_PER_WORKER;
        w[i].error = 0;
        assert_int_equal(pthread_create(&tid[i], NULL, sd_worker_main, &w[i]), 0);
    }
    for(size_t i = 0; i < (size_t)SD_WORKERS; ++i)
    {
        assert_int_equal(pthread_join(tid[i], NULL), 0);
        assert_int_equal(w[i].error, 0);
    }
    PUML_QUIET(0);
    puml_line("W --> C : joined: every call returned 0,\\neach worker's identifiers strictly increase");

    const size_t total = (size_t)SD_WORKERS * (size_t)SD_PER_WORKER;
    qsort(all, total, UUID7_SIZE_BYTES, sd_cmp);
    size_t distinct = 1;
    for(size_t i = 1; i < total; ++i)
    {
        if(memcmp(all + ((i - 1u) * UUID7_SIZE_BYTES), all + (i * UUID7_SIZE_BYTES), UUID7_SIZE_BYTES) != 0) distinct++;
    }
    free(all);
    assert_int_equal(distinct, total);
    PUML_NOTE("C", "%zu identifiers, %zu distinct\n(the clock is frozen: the shared sequence\nand the logical clock keep them apart)",
              total, distinct);
    PUML_END();
}

/**
 * @brief Configuration: the RNG is replaceable at any time, NULL restores the built-in one, the clock-failure count stays 0.
 *
 * @verifies UUID7-SRS-020 UUID7-SRS-021 UUID7-SRS-027 UUID7-ICD-006 UUID7-ICD-008
 */
static void sd_configuration(void** state)
{
    (void)state;
    sd_start("sd_configuration", "Choosing the entropy source", SD_T0_MS);

    uint8_t id[UUID7_SIZE_BYTES];
    PUML_NOTE("C", "no initialisation call at all");
    puml_line("C -> L : uuid7_gen(id)");
    puml_line("activate L");
    puml_line("L -> L : no RNG installed: install the built-in one\\n(compare-and-swap from NULL)");
    puml_line("L -> L : getrandom(2) fills the tail\\n(/dev/urandom if it fails)");
    assert_int_equal(uuid7_gen(id), 0); /* the built-in RNG's bytes are random: the diagram shows the fields, not the bytes */
    puml_line("L --> C : 0  (ms=%llu, seq=%u, random tail)", (unsigned long long)puml_id_ms(id), puml_id_seq(id));
    puml_line("deactivate L");
    assert_int_equal(puml_id_ms(id), SD_T0_MS);
    assert_int_equal(puml_id_seq(id), 0u);

    assert_int_equal(PUML_SET_RNG(puml_rng), 0);
    assert_int_equal(PUML_GEN(id), 0);
    assert_int_equal(id[9], 0x11u); /* the trace RNG's bytes */

    assert_int_equal(PUML_SET_RNG(NULL), 0);
    PUML_NOTE("L", "NULL restores the built-in RNG");
    assert_int_equal(PUML_CLOCK_FAILURES(), 0u);
    PUML_END();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(sd_generate_one),          cmocka_unit_test(sd_same_millisecond),   cmocka_unit_test(sd_clock_backwards),
        cmocka_unit_test(sd_entropy_failure),       cmocka_unit_test(sd_restart_with_floor), cmocka_unit_test(sd_abuse_bounds),
        cmocka_unit_test(sd_concurrent_generation), cmocka_unit_test(sd_configuration),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
