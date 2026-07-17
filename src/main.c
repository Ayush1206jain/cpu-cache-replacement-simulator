/*
 * main.c -- CPU Cache Replacement Simulator -- Day 11
 *
 * Runs the full benchmark suite and prints results to stdout + CSV.
 *
 * Build (all Days):
 *   gcc -Wall -Wextra -std=c11 -O2 -o simulator
 *       src/main.c src/workload.c src/benchmark.c
 *       src/mesi.c src/multi_cache.c src/trace.c src/simulator.c
 *       src/cache/lru.c src/cache/fifo.c src/cache/lfu.c
 *       src/cache/cache.c src/cache/set_cache.c
 *       -Isrc -Isrc/cache -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "benchmark.h"
#include "workload.h"

int main(void)
{
    /* Create results/ directory (best-effort on Windows) */
    system("mkdir results 2>nul");

    printf("==================================================\n");
    printf("  CPU Cache Replacement Simulator -- Day 11\n");
    printf("  Benchmarking: Policy x Workload x Cache Size\n");
    printf("==================================================\n\n");
    printf("  Workload region : %d KB (%d cache lines)\n",
           WL_REGION_SIZE / 1024, WL_REGION_SIZE / WL_LINE_SIZE);
    printf("  Accesses/run    : %d\n", BENCH_N_ACCESSES);
    printf("  Associativity   : %d-way\n", BENCH_N_WAYS);
    printf("  Line size       : %d bytes\n\n", BENCH_LINE_SIZE);

    /* Run all benchmarks, print tables, write CSV */
    benchmark_run_and_report(4, "results/results.csv");

    printf("\nDone. Open results/results.csv for Python plotting.\n");
    return EXIT_SUCCESS;
}
