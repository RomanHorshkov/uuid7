/**
 * @file uuid7_test.h
 * @brief Test-only hooks for uuid7 (present only under UUID7_TESTING).
 *
 * Shared declarations for the deterministic-clock and RNG-fault-injection hooks
 * so the implementation and the test binaries agree on the prototypes — without
 * these, the definitions in uuid7.c draw -Wmissing-prototypes.
 */
#ifndef UUID7_TEST_H
#define UUID7_TEST_H

#ifdef UUID7_TESTING

#    include <stdint.h>

#    ifdef __cplusplus
extern "C"
{
#    endif

/** @brief Deterministic clock hook: the ms timestamp uuid7_gen() should use. */
typedef uint64_t (*uuid7_time_fn_t)(void);

/** @brief Install (or clear, with NULL) the test clock. @return 0. */
int uuid7_test_set_time_fn(uuid7_time_fn_t fn);

/** @brief Force the built-in default RNG to fail (enable != 0) or succeed. @return 0. */
int uuid7_test_set_default_rng_fail(int enable);

/** @brief Reset generator state + all test hooks to first-run defaults. */
void uuid7_test_reset_state(void);

#    ifdef __cplusplus
}
#    endif

#endif /* UUID7_TESTING */
#endif /* UUID7_TEST_H */
