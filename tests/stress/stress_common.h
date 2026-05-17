#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif

#ifndef UUID7_STRESS_COMMON_H
#define UUID7_STRESS_COMMON_H

#include "uuid7.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct sample_summary
{
    double min;
    double max;
    double mean;
    double median;
    double stddev;
    double coeff_var_pct;
    double ci95_half_width;
} sample_summary_t;

static _Atomic uint64_t g_bench_seed_counter = UINT64_C(0x9E3779B97F4A7C15);

static inline uint64_t stress_now_ns(void)
{
    struct timespec ts;

    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return ((uint64_t)ts.tv_sec * UINT64_C(1000000000)) + (uint64_t)ts.tv_nsec;
}

static inline double ns_per_uuid(size_t uuid_count, uint64_t elapsed_ns)
{
    return (double)elapsed_ns / (double)uuid_count;
}

static inline double uuids_per_second(size_t uuid_count, uint64_t elapsed_ns)
{
    return ((double)uuid_count * 1e9) / (double)elapsed_ns;
}

static inline uint64_t splitmix64_next(uint64_t* state)
{
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/*
 * bench_rng()
 * -----------
 * A fast thread-local RNG used by the stress programs so the benchmark is
 * dominated by uuid7_gen() itself rather than kernel entropy syscalls.
 */
static inline int bench_rng(void* buf, size_t n)
{
    static _Thread_local uint64_t tls_state = 0;

    if(tls_state == 0)
    {
        tls_state = atomic_fetch_add_explicit(&g_bench_seed_counter,
                                              UINT64_C(0x9E3779B97F4A7C15),
                                              memory_order_relaxed);
        if(tls_state == 0)
        {
            tls_state = UINT64_C(0xD1B54A32D192ED03);
        }
    }

    uint8_t* out = (uint8_t*)buf;
    size_t off = 0;

    while(off < n)
    {
        uint64_t word = splitmix64_next(&tls_state);

        for(size_t i = 0; i < sizeof(word) && off < n; ++i, ++off)
        {
            out[off] = (uint8_t)(word >> (8u * i));
        }
    }

    return 0;
}

static inline int compare_double(const void* a, const void* b)
{
    const double da = *(const double*)a;
    const double db = *(const double*)b;

    return (da > db) - (da < db);
}

static inline void compute_sample_summary(const double* samples,
                                          size_t sample_count,
                                          sample_summary_t* out)
{
    if(sample_count == 0u)
    {
        fprintf(stderr, "compute_sample_summary requires at least one sample\n");
        exit(EXIT_FAILURE);
    }

    double* sorted = malloc(sample_count * sizeof(*sorted));
    if(!sorted)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    double sum = 0.0;
    out->min = samples[0];
    out->max = samples[0];

    for(size_t i = 0; i < sample_count; ++i)
    {
        const double value = samples[i];

        sorted[i] = value;
        sum += value;

        if(value < out->min) out->min = value;
        if(value > out->max) out->max = value;
    }

    out->mean = sum / (double)sample_count;

    double variance_acc = 0.0;
    for(size_t i = 0; i < sample_count; ++i)
    {
        const double delta = samples[i] - out->mean;
        variance_acc += delta * delta;
    }

    if(sample_count > 1u)
    {
        out->stddev = sqrt(variance_acc / (double)(sample_count - 1u));
        out->ci95_half_width = 1.96 * (out->stddev / sqrt((double)sample_count));
    }
    else
    {
        out->stddev = 0.0;
        out->ci95_half_width = 0.0;
    }

    out->coeff_var_pct =
        (fabs(out->mean) > DBL_EPSILON) ? ((out->stddev / out->mean) * 100.0) : 0.0;

    qsort(sorted, sample_count, sizeof(*sorted), compare_double);
    if((sample_count & 1u) != 0u)
    {
        out->median = sorted[sample_count / 2u];
    }
    else
    {
        const size_t hi = sample_count / 2u;
        const size_t lo = hi - 1u;
        out->median = (sorted[lo] + sorted[hi]) / 2.0;
    }

    free(sorted);
}

static inline void print_summary(FILE* out,
                                 const char* label,
                                 const char* unit,
                                 const sample_summary_t* summary)
{
    fprintf(out, "%s\n", label);
    fprintf(out, "  min:            %.3f %s\n", summary->min, unit);
    fprintf(out, "  max:            %.3f %s\n", summary->max, unit);
    fprintf(out, "  mean:           %.3f %s\n", summary->mean, unit);
    fprintf(out, "  median:         %.3f %s\n", summary->median, unit);
    fprintf(out, "  stddev:         %.3f %s\n", summary->stddev, unit);
    fprintf(out, "  coeff var:      %.3f %%\n", summary->coeff_var_pct);
    fprintf(out, "  95%% CI +/-:     %.3f %s\n", summary->ci95_half_width, unit);
}

#endif /* UUID7_STRESS_COMMON_H */
