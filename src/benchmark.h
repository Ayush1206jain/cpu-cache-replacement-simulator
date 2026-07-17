/*
 * benchmark.h -- Cache Benchmarking Engine
 *
 * Runs all 4 policies across all 4 workloads and all 4 cache sizes.
 * Produces:
 *   1. 4x4 policy-vs-workload hit-rate matrix (stdout table)
 *   2. 4x4 size-vs-workload hit-rate matrix for LRU (stdout table)
 *   3. results.csv with ALL data for Python plotting
 *
 * Day 11 -- CPU Cache Replacement Simulator
 */

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include <stddef.h>
#include "workload.h"
#include "cache.h"

/* ------------------------------------------------------------------ */
/* Benchmark configuration                                              */
/* ------------------------------------------------------------------ */
#define BENCH_N_ACCESSES   10000   /* accesses per run (keep fast)    */
#define BENCH_SEED         42      /* reproducible random seed         */
#define BENCH_N_POLICIES   3       /* LRU, FIFO, LFU                  */
#define BENCH_N_WORKLOADS  4       /* Sequential, Random, Zipf, Mixed  */
#define BENCH_N_SIZES      4       /* 1KB, 4KB, 16KB, 64KB            */
#define BENCH_N_WAYS       4       /* associativity for all runs       */
#define BENCH_LINE_SIZE    64      /* cache line size in bytes         */

/* Cache sizes to sweep (in bytes) */
static const int BENCH_SIZES_KB[] = {1, 4, 16, 64};   /* KB */

/* ------------------------------------------------------------------ */
/* One benchmark result cell                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    double   hit_rate;    /* 0.0 to 100.0 */
    uint64_t hits;
    uint64_t misses;
    uint64_t total;
} BenchResult;

/* ------------------------------------------------------------------ */
/* Full benchmark results                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    /* 4x4 policy vs workload matrix [policy][workload] at default size */
    BenchResult policy_matrix[BENCH_N_POLICIES][BENCH_N_WORKLOADS];

    /* 4x4 cache size vs workload matrix [size][workload] for LRU */
    BenchResult size_matrix[BENCH_N_SIZES][BENCH_N_WORKLOADS];

    /* Metadata */
    int    default_size_kb;    /* cache size used for policy matrix */
    int    default_ways;
    size_t n_accesses;
    unsigned int seed;
} BenchReport;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * Run all benchmarks and return a filled BenchReport.
 * default_size_kb: cache size (in KB) for the policy comparison matrix.
 */
BenchReport benchmark_run(int default_size_kb);

/** Print the 4x4 policy-vs-workload matrix to stdout. */
void benchmark_print_policy_matrix(const BenchReport *r);

/** Print the 4x4 size-vs-workload matrix (LRU only) to stdout. */
void benchmark_print_size_matrix(const BenchReport *r);

/** Write all results to results/results.csv for Python plotting. */
void benchmark_write_csv(const BenchReport *r, const char *path);

/** Print + write CSV in one call. */
void benchmark_run_and_report(int default_size_kb, const char *csv_path);

#endif /* BENCHMARK_H */
