/*
 * main.c -- CPU Cache Replacement Simulator
 *
 *   : Trace-driven simulation demos.
 *   - Run simple.trace through all 3 policies and compare
 *   - Run valgrind.trace to show instruction vs data access breakdown
 *   - Run matrix.trace to illustrate spatial locality impact
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o simulator
 *       src/main.c src/trace.c src/simulator.c
 *       src/cache/lru.c src/cache/fifo.c src/cache/lfu.c
 *       src/cache/cache.c src/cache/set_cache.c
 *       -Isrc -Isrc/cache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simulator.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* Helper: build a SimConfig                                            */
/* ------------------------------------------------------------------ */
static SimConfig make_cfg(int sets, int ways, int line,
                          CachePolicy pol, const char *trace)
{
    SimConfig c;
    memset(&c, 0, sizeof(c));
    c.n_sets    = sets;
    c.n_ways    = ways;
    c.line_size = line;
    c.policy    = pol;
    c.trace_path = trace;
    return c;
}

/* ------------------------------------------------------------------ */
/* Demo 1: simple.trace -- LRU vs FIFO vs LFU                          */
/* ------------------------------------------------------------------ */
static void demo_policy_comparison(void)
{
    printf("\n==================================================\n");
    printf("  Demo 1: Policy comparison on simple.trace\n");
    printf("  Cache: 2-way, 4 sets, 64-byte lines (512 B)\n");
    printf("==================================================\n");

    CachePolicy pols[] = {POLICY_LRU, POLICY_FIFO, POLICY_LFU};
    SimResult   results[3];
    SimConfig   configs[3];

    for (int i = 0; i < 3; i++) {
        configs[i] = make_cfg(4, 2, 64, pols[i], "traces/simple.trace");
        results[i] = simulator_run(&configs[i]);
        if (!results[i].success) {
            printf("  [!] Could not run simulation for policy %d\n", i);
        }
    }

    simulator_print_comparison(results, configs, 3);
}

/* ------------------------------------------------------------------ */
/* Demo 2: valgrind.trace -- full report with access breakdown          */
/* ------------------------------------------------------------------ */
static void demo_valgrind_trace(void)
{
    printf("\n==================================================\n");
    printf("  Demo 2: Full report on valgrind.trace\n");
    printf("  Cache: 2-way, 4 sets, 64-byte lines (512 B)\n");
    printf("==================================================\n");

    SimConfig cfg = make_cfg(4, 2, 64, POLICY_LRU, "traces/valgrind.trace");
    SimResult r   = simulator_run(&cfg);
    simulator_print_report(&r, &cfg);
}

/* ------------------------------------------------------------------ */
/* Demo 3: Associativity impact on matrix.trace                        */
/* ------------------------------------------------------------------ */
static void demo_associativity_on_trace(void)
{
    printf("\n==================================================\n");
    printf("  Demo 3: Associativity impact on matrix.trace\n");
    printf("  1-way vs 2-way vs 4-way, 4 sets, 16-byte lines\n");
    printf("==================================================\n");

    int ways_arr[] = {1, 2, 4};
    int sets_arr[] = {4, 4, 4};   /* same number of sets */
    SimResult results[3];
    SimConfig configs[3];

    for (int i = 0; i < 3; i++) {
        configs[i] = make_cfg(sets_arr[i], ways_arr[i], 16,
                              POLICY_LRU, "traces/matrix.trace");
        results[i] = simulator_run(&configs[i]);
    }

    simulator_print_comparison(results, configs, 3);
}

/* ------------------------------------------------------------------ */
/* Demo 4: Peek at trace file contents (trace reader demo)             */
/* ------------------------------------------------------------------ */
static void demo_trace_reader(void)
{
    printf("\n==================================================\n");
    printf("  Demo 4: Trace reader -- first 5 records\n");
    printf("  File: traces/simple.trace\n");
    printf("==================================================\n");

    TraceReader *tr = trace_open("traces/simple.trace");
    if (!tr) { printf("  [!] Could not open trace file.\n"); return; }

    TraceRecord rec;
    int count = 0;
    printf("  %-4s | %-8s | %-6s | %s\n", "No.", "Address", "Size", "Type");
    printf("  ----------------------------------------\n");
    while (count < 5 && trace_next(tr, &rec) == 1) {
        printf("  %-4d | 0x%06llx | %-6d | %s\n",
               count + 1,
               (unsigned long long)rec.address,
               rec.size,
               trace_access_type_str(rec.type));
        count++;
    }

    printf("\n  Trace reader stats after reading 5 records:\n");
    printf("    Lines read   : %llu\n", (unsigned long long)tr->lines_read);
    printf("    Lines skipped: %llu\n", (unsigned long long)tr->lines_skipped);

    trace_close(tr);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("==================================================\n");
    printf("  CPU Cache Replacement Simulator -- Day 7\n");
    printf("  Trace-Driven Simulation\n");
    printf("==================================================\n");

    demo_trace_reader();
    demo_policy_comparison();
    demo_valgrind_trace();
    demo_associativity_on_trace();

    return EXIT_SUCCESS;
}
