/**
 * @file uuid7.h
 * @brief 
 *
 * @author  Roman Horshkov <roman.horshkov@gmail.com>
 * @date    2025
 * (c) 2025
 */

#ifndef UUID_H
#define UUID_H

#include <stdint.h>
#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * PUBLIC DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC STRUCTURED VARIABLES
 ****************************************************************************
*/
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
*/

/**
 * @brief Generate a UUIDv7 value.
 *
 * Produces a 16-byte RFC-v7 style UUID into the provided buffer. The caller
 * must supply a buffer of at least 16 bytes. The function is safe to call
 * concurrently from multiple threads (uses an atomic CAS to reserve a
 * monotonic (ms,seq) pair).
 *
 * @param[out] val  Output buffer, must be at least 16 bytes.
 * @return 0 on success, -1 if @p val is NULL.
 */
int uuid7_gen(uint8_t* val);

/**
 * @brief Type of RNG function used to fill random bytes in UUIDs.
 *
 * The function must fill @p n bytes into @p buf. The RNG function is
 * responsible for producing cryptographically secure random bytes when used
 * in production. For unit testing a deterministic RNG may be substituted via
 * `uuid_set_rng()`.
 *
 * @param[out] buf  Output buffer to fill with random bytes.
 * @param[in]  n    Number of bytes to generate.
 */
typedef void (*uuid_rng_fn_t)(void* buf, const size_t n);

/**
 * @brief Configure the RNG used by the UUID generator.
 *
 * If @p fn is non-NULL the UUID module will call this function to obtain
 * random bytes for sequence initialization and for the random tail. If
 * @p fn is NULL the module will reset to the built-in default RNG which
 * reads system entropy (getrandom(2) on Linux or /dev/urandom fallback).
 *
 * Thread-safety: this function is thread-safe and may be called at any time.
 * The implementation guarantees safe concurrent reads/writes of the RNG
 * function pointer.
 *
 * @param[in] fn  RNG function pointer to use, or NULL to reset to default.
 * @return 0 on success, negative on error.
 */
int uuid7_set_rng(uuid_rng_fn_t fn);

/**
 * @brief Explicitly initialize the UUID module and optionally configure the
 * RNG implementation.
 *
 * This function performs any module-local initialization that is required
 * before using `uuid7_gen()`. It also accepts an optional RNG function
 * pointer which will be used to generate cryptographically secure bytes.
 * If @p fn is NULL the module will install the built-in default RNG.
 *
 * The function is idempotent and thread-safe. Typical usage: call
 * `uuid_init(randombytes_buf)` after any global CSPRNG libraries (e.g.,
 * libsodium's `sodium_init()`) are initialized and before creating
 * application threads.
 *
 * @param[in] fn  Optional RNG function to use. If NULL, install default.
 * @return 0 on success, negative on error.
 */
int uuid7_init(uuid_rng_fn_t fn);

#ifdef __cplusplus
}
#endif

#endif  // UUID_H
