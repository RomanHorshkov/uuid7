/**
 * @file faults_test.c
 * @brief CMocka integration tests for the entropy and clock failure paths: getrandom(2) interrupted, short, empty or failing, the
 * /dev/urandom fallback unopenable, failing, interrupted or ending early, and clock_gettime(2) failing.
 *
 * The executable defines getrandom, open, read and clock_gettime itself; the library's calls resolve to these (the coverage objects and
 * the static archive at link time, the shared object through symbol interposition), and each forwards to the next definition (libc,
 * or a sanitizer's interceptor) unless a test has scripted a result. Only the public API and the UUID7_TESTING hooks are used.
 */

#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#undef _FORTIFY_SOURCE

#include "uuid7.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include <setjmp.h>
#include <stddef.h>

#include <cmocka.h>

#include "uuid7_test.h"

#define FAULT_STEPS_MAX    4u
#define FAULT_PATTERN_BYTE 0x5Au
#define FAULT_TIME_MS      5000u

/* One scripted result of an intercepted call: ret < 0 fails with err, ret >= 0 returns ret (filling ret pattern bytes). */
typedef struct fault_step
{
    ssize_t ret;
    int     err;
} fault_step_t;

typedef struct fault_script
{
    fault_step_t step[FAULT_STEPS_MAX];
    size_t       len;
    size_t       cursor;
    size_t       calls;
    size_t       last_len;
} fault_script_t;

static fault_script_t g_getrandom;
static fault_script_t g_read;
static int            g_open_errno;
static size_t         g_open_calls;
static int            g_open_fd = -1;
static char           g_open_path[64];
static int            g_clock_fail;

static ssize_t (*g_real_getrandom)(void*, size_t, unsigned int);
static ssize_t (*g_real_read)(int, void*, size_t);
static int (*g_real_open)(const char*, int, ...);
static int (*g_real_clock_gettime)(clockid_t, struct timespec*);

static void* next_symbol(const char* name)
{
    void* sym = dlsym(RTLD_NEXT, name);
    if(!sym) abort();
    return sym;
}

static void resolve_real(void)
{
    if(g_real_read) return;
    void* sym = next_symbol("getrandom");
    memcpy(&g_real_getrandom, &sym, sizeof(sym));
    sym = next_symbol("read");
    memcpy(&g_real_read, &sym, sizeof(sym));
    sym = next_symbol("open");
    memcpy(&g_real_open, &sym, sizeof(sym));
    sym = next_symbol("clock_gettime");
    memcpy(&g_real_clock_gettime, &sym, sizeof(sym));
}

/* The next scripted step of @p s, or NULL when the script is spent (the call goes to the real function). */
static const fault_step_t* script_next(fault_script_t* s, size_t len)
{
    s->calls++;
    s->last_len = len;
    if(s->cursor >= s->len) return NULL;
    return &s->step[s->cursor++];
}

static ssize_t script_apply(const fault_step_t* st, void* buf)
{
    if(st->ret < 0)
    {
        errno = st->err;
        return -1;
    }
    memset(buf, FAULT_PATTERN_BYTE, (size_t)st->ret);
    return st->ret;
}

ssize_t getrandom(void* buf, size_t len, unsigned int flags)
{
    resolve_real();
    const fault_step_t* st = script_next(&g_getrandom, len);
    if(st) return script_apply(st, buf);
    return g_real_getrandom(buf, len, flags);
}

ssize_t read(int fd, void* buf, size_t len)
{
    resolve_real();
    if(fd != g_open_fd || g_open_fd < 0) return g_real_read(fd, buf, len);
    const fault_step_t* st = script_next(&g_read, len);
    if(st) return script_apply(st, buf);
    return g_real_read(fd, buf, len);
}

int open(const char* path, int flags, ...)
{
    resolve_real();
    mode_t mode = 0;
    if(flags & O_CREAT)
    {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, unsigned int);
        va_end(ap);
    }
    g_open_calls++;
    size_t n = strlen(path);
    if(n >= sizeof(g_open_path)) n = sizeof(g_open_path) - 1u;
    memcpy(g_open_path, path, n);
    g_open_path[n] = '\0';
    if(g_open_errno)
    {
        errno = g_open_errno;
        return -1;
    }
    g_open_fd = g_real_open(path, flags, mode);
    return g_open_fd;
}

int clock_gettime(clockid_t clk, struct timespec* ts)
{
    resolve_real();
    if(g_clock_fail)
    {
        errno = EINVAL;
        return -1;
    }
    return g_real_clock_gettime(clk, ts);
}

static uint64_t fixed_time_now(void)
{
    return FAULT_TIME_MS;
}

static void script_set(fault_script_t* s, const fault_step_t* steps, size_t n)
{
    memset(s, 0, sizeof(*s));
    memcpy(s->step, steps, n * sizeof(*steps));
    s->len = n;
}

static int faults_reset(void** state)
{
    (void)state;
    uuid7_test_reset_state();
    memset(&g_getrandom, 0, sizeof(g_getrandom));
    memset(&g_read, 0, sizeof(g_read));
    memset(g_open_path, 0, sizeof(g_open_path));
    g_open_errno = 0;
    g_open_calls = 0;
    g_open_fd    = -1;
    g_clock_fail = 0;
    return 0;
}

static uint64_t extract_ms(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    uint64_t ms = 0;
    for(size_t i = 0; i < 6u; ++i)
        ms = (ms << 8) | uuid[i];
    return ms;
}

static uint16_t extract_seq(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    return (uint16_t)(((uint16_t)(uuid[6] & 0x0Fu) << 8) | uuid[7]);
}

static void assert_markers(const uint8_t uuid[UUID7_SIZE_BYTES])
{
    assert_int_equal(uuid[6] & 0xF0u, 0x70u);
    assert_int_equal(uuid[8] & 0xC0u, 0x80u);
}

/* The tail bytes 9..15 carry rand_b[1..7]: the first @p n random bytes came from the script, so they hold the pattern. */
static void assert_tail_starts_with_pattern(const uint8_t uuid[UUID7_SIZE_BYTES], size_t n)
{
    assert_int_equal(uuid[8], (FAULT_PATTERN_BYTE & 0x3Fu) | 0x80u);
    for(size_t i = 1u; i < n; ++i)
        assert_int_equal(uuid[8u + i], FAULT_PATTERN_BYTE);
}

static void assert_fd_closed(int fd)
{
    assert_true(fd >= 0);
    errno = 0;
    assert_int_equal(fcntl(fd, F_GETFD), -1);
    assert_int_equal(errno, EBADF);
}

static void test_getrandom_eintr_is_retried(void** state)
{
    (void)state;
    const fault_step_t steps[] = {{-1, EINTR}, {8, 0}};
    script_set(&g_getrandom, steps, 2u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(g_getrandom.calls, 2u);
    assert_int_equal(g_open_calls, 0u);
    assert_markers(uuid);
    assert_tail_starts_with_pattern(uuid, 8u);
}

static void test_getrandom_failure_falls_back_to_urandom(void** state)
{
    (void)state;
    const fault_step_t steps[] = {{-1, ENOSYS}};
    script_set(&g_getrandom, steps, 1u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(g_getrandom.calls, 1u);
    assert_int_equal(g_open_calls, 1u);
    assert_string_equal(g_open_path, "/dev/urandom");
    assert_int_equal(g_read.calls, 1u);
    assert_int_equal(g_read.last_len, 8u);
    assert_fd_closed(g_open_fd);
    assert_markers(uuid);
}

static void test_getrandom_empty_falls_back_to_urandom(void** state)
{
    (void)state;
    const fault_step_t steps[] = {{0, 0}};
    script_set(&g_getrandom, steps, 1u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(g_getrandom.calls, 1u);
    assert_int_equal(g_open_calls, 1u);
    assert_int_equal(g_read.last_len, 8u);
    assert_fd_closed(g_open_fd);
}

static void test_getrandom_short_then_failure_reads_only_the_rest(void** state)
{
    (void)state;
    const fault_step_t steps[] = {{3, 0}, {-1, EIO}};
    script_set(&g_getrandom, steps, 2u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(g_getrandom.calls, 2u);
    assert_int_equal(g_getrandom.last_len, 5u);
    assert_int_equal(g_open_calls, 1u);
    assert_int_equal(g_read.calls, 1u);
    assert_int_equal(g_read.last_len, 5u);
    assert_tail_starts_with_pattern(uuid, 3u);
    assert_fd_closed(g_open_fd);
}

static void test_urandom_unopenable_fails_without_burning_a_sequence(void** state)
{
    (void)state;
    uuid7_test_set_time_fn(fixed_time_now);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), FAULT_TIME_MS);
    assert_int_equal(extract_seq(uuid), 0u);

    const fault_step_t steps[] = {{-1, ENOSYS}};
    script_set(&g_getrandom, steps, 1u);
    g_open_errno = ENOENT;
    uint8_t refused[UUID7_SIZE_BYTES];
    memset(refused, 0xAA, sizeof(refused));
    assert_int_equal(uuid7_gen(refused), -2);
    assert_int_equal(g_open_calls, 1u);
    for(size_t i = 0; i < sizeof(refused); ++i)
        assert_int_equal(refused[i], 0xAAu);

    g_open_errno = 0;
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), FAULT_TIME_MS);
    assert_int_equal(extract_seq(uuid), 1u);
}

static void test_urandom_read_error_fails_and_closes(void** state)
{
    (void)state;
    const fault_step_t gr[] = {{-1, ENOSYS}};
    const fault_step_t rd[] = {{-1, EIO}};
    script_set(&g_getrandom, gr, 1u);
    script_set(&g_read, rd, 1u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), -2);
    assert_int_equal(g_read.calls, 1u);
    assert_fd_closed(g_open_fd);
}

static void test_urandom_eof_fails_and_closes(void** state)
{
    (void)state;
    const fault_step_t gr[] = {{-1, ENOSYS}};
    const fault_step_t rd[] = {{4, 0}, {0, 0}};
    script_set(&g_getrandom, gr, 1u);
    script_set(&g_read, rd, 2u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), -2);
    assert_int_equal(g_read.calls, 2u);
    assert_int_equal(g_read.last_len, 4u);
    assert_fd_closed(g_open_fd);
}

static void test_urandom_eintr_and_short_reads_are_completed(void** state)
{
    (void)state;
    const fault_step_t gr[] = {{-1, ENOSYS}};
    const fault_step_t rd[] = {{-1, EINTR}, {2, 0}};
    script_set(&g_getrandom, gr, 1u);
    script_set(&g_read, rd, 2u);

    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(g_read.calls, 3u);
    assert_int_equal(g_read.last_len, 6u);
    assert_tail_starts_with_pattern(uuid, 2u);
    assert_fd_closed(g_open_fd);
}

static void test_clock_failure_still_yields_ordered_ids_and_is_counted(void** state)
{
    (void)state;
    const uint64_t before = uuid7_clock_failure_count();
    g_clock_fail          = 1;

    uint8_t first[UUID7_SIZE_BYTES]  = {0};
    uint8_t second[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(first), 0);
    assert_int_equal(uuid7_gen(second), 0);
    assert_int_equal(uuid7_clock_failure_count(), before + 2u);
    assert_int_equal(extract_ms(first), 0u);
    assert_int_equal(extract_ms(second), 0u);
    assert_true(extract_seq(second) > extract_seq(first));
    assert_markers(second);

    g_clock_fail = 0;
    assert_int_equal(uuid7_gen(second), 0);
    assert_true(extract_ms(second) > 0u);
    assert_int_equal(uuid7_clock_failure_count(), before + 2u);
}

static void test_clock_failure_skips_the_ahead_bound(void** state)
{
    (void)state;
    uint8_t        far[UUID7_SIZE_BYTES] = {0};
    const uint64_t far_ms                = UINT64_C(0xF00000000000);
    for(size_t i = 0; i < 6u; ++i)
        far[i] = (uint8_t)(far_ms >> (8u * (5u - i)));
    far[6] = 0x70u;
    far[8] = 0x80u;

    assert_int_equal(uuid7_raise_floor(far), -4);

    g_clock_fail = 1;
    assert_int_equal(uuid7_raise_floor(far), 0);
    uint8_t uuid[UUID7_SIZE_BYTES] = {0};
    assert_int_equal(uuid7_gen(uuid), 0);
    assert_int_equal(extract_ms(uuid), far_ms);
    assert_int_equal(extract_seq(uuid), 1u);

    g_clock_fail = 0;
    assert_int_equal(uuid7_gen(uuid), -3);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_getrandom_eintr_is_retried, faults_reset),
        cmocka_unit_test_setup(test_getrandom_failure_falls_back_to_urandom, faults_reset),
        cmocka_unit_test_setup(test_getrandom_empty_falls_back_to_urandom, faults_reset),
        cmocka_unit_test_setup(test_getrandom_short_then_failure_reads_only_the_rest, faults_reset),
        cmocka_unit_test_setup(test_urandom_unopenable_fails_without_burning_a_sequence, faults_reset),
        cmocka_unit_test_setup(test_urandom_read_error_fails_and_closes, faults_reset),
        cmocka_unit_test_setup(test_urandom_eof_fails_and_closes, faults_reset),
        cmocka_unit_test_setup(test_urandom_eintr_and_short_reads_are_completed, faults_reset),
        cmocka_unit_test_setup(test_clock_failure_still_yields_ordered_ids_and_is_counted, faults_reset),
        cmocka_unit_test_setup(test_clock_failure_skips_the_ahead_bound, faults_reset),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
