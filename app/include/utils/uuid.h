/**
 * @file uuid.h
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
int uuid_gen(uint8_t* val);

/**
 * @brief Type of RNG function used to fill random bytes in UUIDs.
 * The function must fill @p n bytes into @p buf.
 * 
 * @param buf  Output buffer.
 * @param n    Number of bytes to fill.
 */
typedef void (*uuid_rng_fn_t)(void* buf, const size_t n);

/**
 * @brief Set the RNG function used to fill random bytes in UUIDs.
 *
 * If @p fn is NULL, resets to the default RNG (reads from /dev/urandom
 * or uses libsodium's `randombytes_buf` if available).
 *
 * @param[in] fn  RNG function pointer or NULL to reset to default.
 * @return 0 on success, -1 if @p fn is NULL (reset to default).
 */
int uuid_set_rng(uuid_rng_fn_t fn);

#ifdef __cplusplus
}
#endif

#endif  // UUID_H
