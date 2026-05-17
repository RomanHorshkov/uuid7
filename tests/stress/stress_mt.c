#include "stress_common.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define STRESS_MT_THREADS                  10u
#define STRESS_MT_UUIDS_PER_THREAD         100000u
#define STRESS_MT_WARMUP_UUIDS_PER_THREAD  5000u
#define STRESS_MT_RUNS                     5u

typedef struct start_gate
{
    pthread_mutex_t mutex;
    pthread_cond_t  ready_cond;
    pthread_cond_t  start_cond;
    size_t          ready_count;
    int             start_flag;
} start_gate_t;

typedef struct worker_ctx
{
    start_gate_t* gate;
    uint8_t*      out;
    size_t        uuid_count;
    size_t        thread_index;
    uint64_t      elapsed_ns;
    int           error;
} worker_ctx_t;

static void start_gate_init(start_gate_t* gate)
{
    if(pthread_mutex_init(&gate->mutex, NULL) != 0)
    {
        perror("pthread_mutex_init");
        exit(EXIT_FAILURE);
    }

    if(pthread_cond_init(&gate->ready_cond, NULL) != 0)
    {
        perror("pthread_cond_init");
        exit(EXIT_FAILURE);
    }

    if(pthread_cond_init(&gate->start_cond, NULL) != 0)
    {
        perror("pthread_cond_init");
        exit(EXIT_FAILURE);
    }

    gate->ready_count = 0u;
    gate->start_flag  = 0;
}

static void start_gate_destroy(start_gate_t* gate)
{
    pthread_cond_destroy(&gate->ready_cond);
    pthread_cond_destroy(&gate->start_cond);
    pthread_mutex_destroy(&gate->mutex);
}

static void* stress_worker(void* arg)
{
    worker_ctx_t* ctx = (worker_ctx_t*)arg;

    if(pthread_mutex_lock(&ctx->gate->mutex) != 0)
    {
        ctx->error = -1;
        return NULL;
    }

    ctx->gate->ready_count += 1u;
    if(pthread_cond_signal(&ctx->gate->ready_cond) != 0)
    {
        pthread_mutex_unlock(&ctx->gate->mutex);
        ctx->error = -1;
        return NULL;
    }

    while(!ctx->gate->start_flag)
    {
        if(pthread_cond_wait(&ctx->gate->start_cond, &ctx->gate->mutex) != 0)
        {
            pthread_mutex_unlock(&ctx->gate->mutex);
            ctx->error = -1;
            return NULL;
        }
    }

    if(pthread_mutex_unlock(&ctx->gate->mutex) != 0)
    {
        ctx->error = -1;
        return NULL;
    }

    int rc = 0;
    const uint64_t start_ns = stress_now_ns();
    for(size_t i = 0; i < ctx->uuid_count; ++i)
    {
        rc = uuid7_gen(ctx->out + (i * UUID7_SIZE_BYTES));
        if(rc != 0) break;
    }
    const uint64_t end_ns = stress_now_ns();

    ctx->elapsed_ns = end_ns - start_ns;
    ctx->error = rc;
    return NULL;
}

static void run_round(size_t uuid_count,
                      uint8_t* buffers[STRESS_MT_THREADS],
                      double thread_elapsed_ns[STRESS_MT_THREADS],
                      uint64_t* wall_elapsed_ns_out)
{
    start_gate_t gate;
    pthread_t    tids[STRESS_MT_THREADS];
    worker_ctx_t ctx[STRESS_MT_THREADS];

    start_gate_init(&gate);

    for(size_t i = 0; i < STRESS_MT_THREADS; ++i)
    {
        ctx[i].gate         = &gate;
        ctx[i].out          = buffers[i];
        ctx[i].uuid_count   = uuid_count;
        ctx[i].thread_index = i;
        ctx[i].elapsed_ns   = 0u;
        ctx[i].error        = 0;

        if(pthread_create(&tids[i], NULL, stress_worker, &ctx[i]) != 0)
        {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    if(pthread_mutex_lock(&gate.mutex) != 0)
    {
        perror("pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    while(gate.ready_count < STRESS_MT_THREADS)
    {
        if(pthread_cond_wait(&gate.ready_cond, &gate.mutex) != 0)
        {
            perror("pthread_cond_wait");
            exit(EXIT_FAILURE);
        }
    }

    const uint64_t wall_start_ns = stress_now_ns();
    gate.start_flag = 1;
    if(pthread_cond_broadcast(&gate.start_cond) != 0)
    {
        perror("pthread_cond_broadcast");
        exit(EXIT_FAILURE);
    }

    if(pthread_mutex_unlock(&gate.mutex) != 0)
    {
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }

    for(size_t i = 0; i < STRESS_MT_THREADS; ++i)
    {
        pthread_join(tids[i], NULL);
        if(ctx[i].error != 0)
        {
            fprintf(stderr, "worker %zu failed with rc=%d\n", i, ctx[i].error);
            exit(EXIT_FAILURE);
        }

        thread_elapsed_ns[i] = (double)ctx[i].elapsed_ns;
    }

    *wall_elapsed_ns_out = stress_now_ns() - wall_start_ns;
    start_gate_destroy(&gate);
}

int main(void)
{
    uint8_t* buffers[STRESS_MT_THREADS];

    for(size_t i = 0; i < STRESS_MT_THREADS; ++i)
    {
        buffers[i] = malloc((size_t)STRESS_MT_UUIDS_PER_THREAD * UUID7_SIZE_BYTES);
        if(!buffers[i])
        {
            perror("malloc");
            return EXIT_FAILURE;
        }
    }

    if(uuid7_init(bench_rng, NULL) != 0)
    {
        fprintf(stderr, "uuid7_init failed\n");
        return EXIT_FAILURE;
    }

    /*
     * Warmup round: initialize thread-local RNG state and touch destination
     * buffers so the measured rounds focus on steady-state generation.
     */
    double   warmup_elapsed_ns[STRESS_MT_THREADS];
    uint64_t warmup_wall_ns = 0u;
    run_round(STRESS_MT_WARMUP_UUIDS_PER_THREAD, buffers,
              warmup_elapsed_ns, &warmup_wall_ns);

    double thread_elapsed_runs[STRESS_MT_RUNS][STRESS_MT_THREADS];
    double wall_elapsed_runs[STRESS_MT_RUNS];
    double aggregate_throughput_runs[STRESS_MT_RUNS];
    double per_thread_ns_per_uuid_all[STRESS_MT_RUNS * STRESS_MT_THREADS];

    printf("uuid7 multi-thread stress benchmark\n");
    printf("configuration\n");
    printf("  measured runs:               %u\n", STRESS_MT_RUNS);
    printf("  threads:                     %u\n", STRESS_MT_THREADS);
    printf("  uuids per thread per run:    %u\n", STRESS_MT_UUIDS_PER_THREAD);
    printf("  warmup uuids per thread:     %u\n", STRESS_MT_WARMUP_UUIDS_PER_THREAD);
    printf("  total uuids per measured run:%u\n",
           STRESS_MT_THREADS * STRESS_MT_UUIDS_PER_THREAD);
    printf("  bytes per uuid:              %u\n", UUID7_SIZE_BYTES);
    printf("  rng mode:                    thread-local benchmark RNG\n");
    printf("  measured region:             each thread's loop of uuid7_gen() calls only\n");

    for(size_t run = 0; run < STRESS_MT_RUNS; ++run)
    {
        uint64_t wall_ns = 0u;
        run_round(STRESS_MT_UUIDS_PER_THREAD, buffers,
                  thread_elapsed_runs[run], &wall_ns);

        wall_elapsed_runs[run] = (double)wall_ns;
        aggregate_throughput_runs[run] =
            uuids_per_second((size_t)STRESS_MT_THREADS * STRESS_MT_UUIDS_PER_THREAD,
                             wall_ns);

        printf("run %zu\n", run + 1u);
        for(size_t thread = 0; thread < STRESS_MT_THREADS; ++thread)
        {
            const double elapsed = thread_elapsed_runs[run][thread];
            const double cost = elapsed / (double)STRESS_MT_UUIDS_PER_THREAD;
            const double rate =
                ((double)STRESS_MT_UUIDS_PER_THREAD * 1e9) / elapsed;

            per_thread_ns_per_uuid_all[(run * STRESS_MT_THREADS) + thread] = cost;

            printf("  thread %2zu  elapsed: %12.0f ns  ns/uuid: %9.3f  uuid/s: %12.3f\n",
                   thread, elapsed, cost, rate);
        }

        sample_summary_t round_thread_summary;
        compute_sample_summary(thread_elapsed_runs[run], STRESS_MT_THREADS,
                               &round_thread_summary);

        printf("  wall-clock elapsed: %.0f ns\n", wall_elapsed_runs[run]);
        printf("  aggregate uuid/s:   %.3f\n", aggregate_throughput_runs[run]);
        printf("  per-thread summary\n");
        printf("    mean elapsed:     %.3f ns\n", round_thread_summary.mean);
        printf("    stddev elapsed:   %.3f ns\n", round_thread_summary.stddev);
        printf("    min elapsed:      %.3f ns\n", round_thread_summary.min);
        printf("    max elapsed:      %.3f ns\n", round_thread_summary.max);
    }

    double per_thread_mean_elapsed[STRESS_MT_THREADS];
    double per_thread_mean_ns_uuid[STRESS_MT_THREADS];
    double per_thread_mean_rate[STRESS_MT_THREADS];

    for(size_t thread = 0; thread < STRESS_MT_THREADS; ++thread)
    {
        double elapsed_sum = 0.0;
        double cost_sum = 0.0;
        double rate_sum = 0.0;

        for(size_t run = 0; run < STRESS_MT_RUNS; ++run)
        {
            const double elapsed = thread_elapsed_runs[run][thread];
            const double cost = elapsed / (double)STRESS_MT_UUIDS_PER_THREAD;
            const double rate =
                ((double)STRESS_MT_UUIDS_PER_THREAD * 1e9) / elapsed;

            elapsed_sum += elapsed;
            cost_sum += cost;
            rate_sum += rate;
        }

        per_thread_mean_elapsed[thread] = elapsed_sum / (double)STRESS_MT_RUNS;
        per_thread_mean_ns_uuid[thread] = cost_sum / (double)STRESS_MT_RUNS;
        per_thread_mean_rate[thread]    = rate_sum / (double)STRESS_MT_RUNS;
    }

    sample_summary_t wall_summary;
    sample_summary_t aggregate_rate_summary;
    sample_summary_t all_thread_cost_summary;

    compute_sample_summary(wall_elapsed_runs, STRESS_MT_RUNS, &wall_summary);
    compute_sample_summary(aggregate_throughput_runs, STRESS_MT_RUNS,
                           &aggregate_rate_summary);
    compute_sample_summary(per_thread_ns_per_uuid_all,
                           STRESS_MT_RUNS * STRESS_MT_THREADS,
                           &all_thread_cost_summary);

    printf("\nper-thread means across measured runs\n");
    for(size_t thread = 0; thread < STRESS_MT_THREADS; ++thread)
    {
        printf("  thread %2zu  mean elapsed: %12.3f ns  mean ns/uuid: %9.3f  mean uuid/s: %12.3f\n",
               thread,
               per_thread_mean_elapsed[thread],
               per_thread_mean_ns_uuid[thread],
               per_thread_mean_rate[thread]);
    }

    printf("\nsummary\n");
    print_summary(stdout, "wall-clock elapsed per run", "ns", &wall_summary);
    print_summary(stdout, "aggregate throughput per run", "uuid/s",
                  &aggregate_rate_summary);
    print_summary(stdout, "all per-thread cost samples", "ns/uuid",
                  &all_thread_cost_summary);

    for(size_t i = 0; i < STRESS_MT_THREADS; ++i)
    {
        free(buffers[i]);
    }

    return EXIT_SUCCESS;
}
