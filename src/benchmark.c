/*
 * benchmark.c -- Cache Benchmarking Engine Implementation
 *
 * Core loop:
 *   for each (policy, workload, cache_size):
 *     cache = set_cache_create(n_sets, n_ways, line_size, policy)
 *     for each address in workload:
 *         set_cache_access(cache, addr)
 *     record hits, misses, hit_rate
 *
 * Day 11 -- CPU Cache Replacement Simulator
 */

#include "benchmark.h"
#include "workload.h"
#include "set_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Internal: run one (cache_size_kb, n_ways, policy, workload) combo  */
/* ------------------------------------------------------------------ */
static BenchResult run_one(int size_kb, int n_ways,
                           CachePolicy policy, const Workload *wl)
{
    BenchResult r;
    memset(&r, 0, sizeof(r));

    /* Compute n_sets from total cache size */
    int total_bytes = size_kb * 1024;
    int n_sets = total_bytes / (n_ways * BENCH_LINE_SIZE);

    /* n_sets must be >= 1 and a power of 2 */
    if (n_sets < 1) n_sets = 1;

    /* Round down to nearest power of 2 */
    int p2 = 1;
    while (p2 * 2 <= n_sets) p2 <<= 1;
    n_sets = p2;

    SetAssocCache *cache = set_cache_create(n_sets, n_ways,
                                            BENCH_LINE_SIZE, policy);
    if (!cache) return r;

    for (size_t i = 0; i < wl->n; i++)
        set_cache_access(cache, wl->addrs[i]);

    r.hits      = cache->hits;
    r.misses    = cache->misses;
    r.total     = cache->hits + cache->misses;
    r.hit_rate  = (r.total > 0)
                  ? 100.0 * (double)r.hits / (double)r.total
                  : 0.0;

    set_cache_destroy(cache);
    return r;
}

/* ------------------------------------------------------------------ */
/* benchmark_run                                                         */
/* ------------------------------------------------------------------ */
BenchReport benchmark_run(int default_size_kb)
{
    BenchReport rep;
    memset(&rep, 0, sizeof(rep));
    rep.default_size_kb = default_size_kb;
    rep.default_ways    = BENCH_N_WAYS;
    rep.n_accesses      = BENCH_N_ACCESSES;
    rep.seed            = BENCH_SEED;

    /* Build all 4 workloads once -- reuse across all policy/size runs */
    Workload *wl = workload_build_all(BENCH_N_ACCESSES, BENCH_SEED);
    if (!wl) {
        fprintf(stderr, "[bench] workload_build_all failed\n");
        return rep;
    }

    static const CachePolicy policies[BENCH_N_POLICIES] = {
        POLICY_LRU, POLICY_FIFO, POLICY_LFU
    };

    /* ---- 4x4 Policy matrix (fixed default size, all policies) ---- */
    printf("[bench] Running policy matrix (%dKB, %d-way) ...\n",
           default_size_kb, BENCH_N_WAYS);
    for (int p = 0; p < BENCH_N_POLICIES; p++) {
        for (int w = 0; w < BENCH_N_WORKLOADS; w++) {
            rep.policy_matrix[p][w] = run_one(default_size_kb,
                                              BENCH_N_WAYS,
                                              policies[p], &wl[w]);
        }
    }

    /* ---- 4x4 Size matrix (LRU only, all workloads) ---- */
    printf("[bench] Running size sweep (LRU) ...\n");
    for (int s = 0; s < BENCH_N_SIZES; s++) {
        int kb = BENCH_SIZES_KB[s];
        for (int w = 0; w < BENCH_N_WORKLOADS; w++) {
            rep.size_matrix[s][w] = run_one(kb, BENCH_N_WAYS,
                                            POLICY_LRU, &wl[w]);
        }
    }

    workload_free_all(wl, 4);
    return rep;
}

/* ------------------------------------------------------------------ */
/* benchmark_print_policy_matrix                                        */
/* ------------------------------------------------------------------ */
void benchmark_print_policy_matrix(const BenchReport *r)
{
    static const char *pnames[BENCH_N_POLICIES] = {"LRU", "FIFO", "LFU"};
    static const char *wnames[BENCH_N_WORKLOADS] = {
        "Sequential", "Random", "Zipfian", "Mixed"
    };

    printf("\n");
    printf("=================================================================\n");
    printf("  Policy vs Workload Hit Rate (%%)                              \n");
    printf("  Cache: %dKB, %d-way, %dB lines, %d accesses                  \n",
           r->default_size_kb, r->default_ways,
           BENCH_LINE_SIZE, (int)r->n_accesses);
    printf("=================================================================\n");
    printf("  %-6s | %-12s | %-8s | %-8s | %-8s\n",
           "Policy", "Sequential", "Random", "Zipfian", "Mixed");
    printf("  -------------------------------------------------------\n");

    for (int p = 0; p < BENCH_N_POLICIES; p++) {
        printf("  %-6s | %11.2f%% | %7.2f%% | %7.2f%% | %7.2f%%\n",
               pnames[p],
               r->policy_matrix[p][0].hit_rate,
               r->policy_matrix[p][1].hit_rate,
               r->policy_matrix[p][2].hit_rate,
               r->policy_matrix[p][3].hit_rate);
    }
    printf("=================================================================\n");
    (void)wnames;
}

/* ------------------------------------------------------------------ */
/* benchmark_print_size_matrix                                          */
/* ------------------------------------------------------------------ */
void benchmark_print_size_matrix(const BenchReport *r)
{
    printf("\n");
    printf("=================================================================\n");
    printf("  Cache Size vs Workload Hit Rate (%%) -- LRU Policy            \n");
    printf("  %d-way, %dB lines, %d accesses                               \n",
           r->default_ways, BENCH_LINE_SIZE, (int)r->n_accesses);
    printf("=================================================================\n");
    printf("  %-8s | %-12s | %-8s | %-8s | %-8s\n",
           "Size", "Sequential", "Random", "Zipfian", "Mixed");
    printf("  ---------------------------------------------------------\n");

    for (int s = 0; s < BENCH_N_SIZES; s++) {
        printf("  %-7dKB | %11.2f%% | %7.2f%% | %7.2f%% | %7.2f%%\n",
               BENCH_SIZES_KB[s],
               r->size_matrix[s][0].hit_rate,
               r->size_matrix[s][1].hit_rate,
               r->size_matrix[s][2].hit_rate,
               r->size_matrix[s][3].hit_rate);
    }
    printf("=================================================================\n");
}

/* ------------------------------------------------------------------ */
/* benchmark_write_csv                                                   */
/* ------------------------------------------------------------------ */
void benchmark_write_csv(const BenchReport *r, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[bench] Cannot open '%s': %s\n",
                path, strerror(errno));
        return;
    }

    static const char *pnames[BENCH_N_POLICIES] = {"LRU", "FIFO", "LFU"};
    static const char *wnames[BENCH_N_WORKLOADS] = {
        "Sequential", "Random", "Zipfian", "Mixed"
    };

    /* --- Section 1: Policy matrix --- */
    fprintf(f, "# Section 1: Policy vs Workload (cache=%dKB %d-way)\n",
            r->default_size_kb, r->default_ways);
    fprintf(f, "section,policy,workload,hits,misses,total,hit_rate\n");

    for (int p = 0; p < BENCH_N_POLICIES; p++) {
        for (int w = 0; w < BENCH_N_WORKLOADS; w++) {
            const BenchResult *br = &r->policy_matrix[p][w];
            fprintf(f, "policy_matrix,%s,%s,%llu,%llu,%llu,%.4f\n",
                    pnames[p], wnames[w],
                    (unsigned long long)br->hits,
                    (unsigned long long)br->misses,
                    (unsigned long long)br->total,
                    br->hit_rate);
        }
    }

    /* --- Section 2: Size sweep (LRU) --- */
    fprintf(f, "\n# Section 2: LRU Cache Size Sweep\n");
    fprintf(f, "section,policy,workload,size_kb,hits,misses,total,hit_rate\n");

    for (int s = 0; s < BENCH_N_SIZES; s++) {
        for (int w = 0; w < BENCH_N_WORKLOADS; w++) {
            const BenchResult *br = &r->size_matrix[s][w];
            fprintf(f, "size_sweep,LRU,%s,%d,%llu,%llu,%llu,%.4f\n",
                    wnames[w],
                    BENCH_SIZES_KB[s],
                    (unsigned long long)br->hits,
                    (unsigned long long)br->misses,
                    (unsigned long long)br->total,
                    br->hit_rate);
        }
    }

    fclose(f);
    printf("[bench] Results written to '%s'\n", path);
}

/* ------------------------------------------------------------------ */
/* benchmark_run_and_report                                             */
/* ------------------------------------------------------------------ */
void benchmark_run_and_report(int default_size_kb, const char *csv_path)
{
    BenchReport r = benchmark_run(default_size_kb);
    benchmark_print_policy_matrix(&r);
    benchmark_print_size_matrix(&r);
    benchmark_write_csv(&r, csv_path);
}
