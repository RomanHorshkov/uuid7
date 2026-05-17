#include "stress_common.h"

#include <stdio.h>
#include <stdlib.h>

#define STRESS_UUIDS_PER_RUN   1000000u
#define STRESS_WARMUP_UUIDS    10000u
#define STRESS_RUNS            10u

int main(void)
{
    uint8_t* out = malloc((size_t)STRESS_UUIDS_PER_RUN * UUID7_SIZE_BYTES);
    if(!out)
    {
        perror("malloc");
        return EXIT_FAILURE;
    }

    if(uuid7_init(bench_rng, NULL) != 0)
    {
        fprintf(stderr, "uuid7_init failed\n");
        free(out);
        return EXIT_FAILURE;
    }

    /* Warm caches, branch predictors, allocator pages, and the benchmark RNG. */
    for(size_t i = 0; i < STRESS_WARMUP_UUIDS; ++i)
    {
        if(uuid7_gen(out + (i * UUID7_SIZE_BYTES)) != 0)
        {
            fprintf(stderr, "uuid7_gen failed during warmup\n");
            free(out);
            return EXIT_FAILURE;
        }
    }

    double elapsed_ns[STRESS_RUNS];
    double ns_uuid[STRESS_RUNS];
    double throughput[STRESS_RUNS];

    printf("uuid7 single-thread stress benchmark\n");
    printf("configuration\n");
    printf("  measured runs:          %u\n", STRESS_RUNS);
    printf("  uuids per run:          %u\n", STRESS_UUIDS_PER_RUN);
    printf("  warmup uuids:           %u\n", STRESS_WARMUP_UUIDS);
    printf("  bytes per uuid:         %u\n", UUID7_SIZE_BYTES);
    printf("  rng mode:               thread-local benchmark RNG\n");
    printf("  measured region:        loop containing only uuid7_gen() calls\n");

    for(size_t run = 0; run < STRESS_RUNS; ++run)
    {
        int rc = 0;

        const uint64_t start_ns = stress_now_ns();
        for(size_t i = 0; i < STRESS_UUIDS_PER_RUN; ++i)
        {
            rc = uuid7_gen(out + (i * UUID7_SIZE_BYTES));
            if(rc != 0) break;
        }
        const uint64_t end_ns = stress_now_ns();

        if(rc != 0)
        {
            fprintf(stderr, "uuid7_gen failed during measured run %zu with rc=%d\n",
                    run + 1u, rc);
            free(out);
            return EXIT_FAILURE;
        }

        const uint64_t delta_ns = end_ns - start_ns;
        elapsed_ns[run] = (double)delta_ns;
        ns_uuid[run]    = ns_per_uuid(STRESS_UUIDS_PER_RUN, delta_ns);
        throughput[run] = uuids_per_second(STRESS_UUIDS_PER_RUN, delta_ns);

        printf("run %2zu  elapsed: %12.0f ns  ns/uuid: %9.3f  uuid/s: %12.3f\n",
               run + 1u, elapsed_ns[run], ns_uuid[run], throughput[run]);
    }

    sample_summary_t elapsed_summary;
    sample_summary_t ns_uuid_summary;
    sample_summary_t throughput_summary;

    compute_sample_summary(elapsed_ns, STRESS_RUNS, &elapsed_summary);
    compute_sample_summary(ns_uuid, STRESS_RUNS, &ns_uuid_summary);
    compute_sample_summary(throughput, STRESS_RUNS, &throughput_summary);

    printf("\nsummary\n");
    print_summary(stdout, "elapsed time per run", "ns", &elapsed_summary);
    print_summary(stdout, "cost per uuid", "ns/uuid", &ns_uuid_summary);
    print_summary(stdout, "throughput", "uuid/s", &throughput_summary);

    free(out);
    return EXIT_SUCCESS;
}
