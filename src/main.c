/*
 * main.c -- CPU Cache Replacement Simulator
 *
 * Day 8: Multi-Level Cache Hierarchy demos.
 *   Demo 1: Hit waterfall -- L1->L2->L3->RAM access trace
 *   Demo 2: AMAT for different L1 hit rates
 *   Demo 3: Trace-driven multi-level simulation (simple.trace)
 *   Demo 4: Size impact -- small vs realistic L1 on same trace
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o simulator
 *       src/main.c src/multi_cache.c src/trace.c src/simulator.c
 *       src/cache/lru.c src/cache/fifo.c src/cache/lfu.c
 *       src/cache/cache.c src/cache/set_cache.c
 *       -Isrc -Isrc/cache -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "multi_cache.h"
#include "simulator.h"

static CacheLevelConfig lvl(int sets, int ways, int lat,
                             CachePolicy pol, const char *name)
{
    CacheLevelConfig c;
    memset(&c, 0, sizeof(c));
    c.n_sets = sets; c.n_ways = ways; c.line_size = 64;
    c.hit_latency = lat; c.policy = pol; c.name = name;
    return c;
}

/* ------------------------------------------------------------------ */
/* Demo 1: Hit waterfall                                                */
/* ------------------------------------------------------------------ */
static void demo_hit_waterfall(void)
{
    printf("\n==================================================\n");
    printf("  Demo 1: Hit Waterfall -- L1->L2->L3->RAM\n");
    printf("  Tiny caches (1-way) to force each level\n");
    printf("==================================================\n");

    /* 1-way, 1-set caches -> capacity 1 line each (64B) */
    CacheLevelConfig l1 = lvl(1,  1, LATENCY_L1_HIT, POLICY_LRU, "L1");
    CacheLevelConfig l2 = lvl(2,  1, LATENCY_L2_HIT, POLICY_LRU, "L2");
    CacheLevelConfig l3 = lvl(4,  1, LATENCY_L3_HIT, POLICY_LRU, "L3");
    MultiLevelCache *mc = multi_cache_create(&l1, &l2, &l3);

    static const char *lvlname[] = {"RAM", "L1 ", "L2 ", "L3 "};
    uint64_t addrs[] = {0x000, 0x040, 0x000, 0x080, 0x000, 0x040, 0x000};
    const char *labels[] = {"A","B","A","C","A","B","A"};
    int n = 7;

    printf("  %-6s | %-8s | %s\n", "Step", "Access", "Served by");
    printf("  --------------------------\n");
    for (int i = 0; i < n; i++) {
        int r = multi_cache_access(mc, addrs[i]);
        printf("  %-6d | %-8s | %s\n", i+1, labels[i], lvlname[r < 0 ? 0 : r]);
    }

    multi_cache_print_stats(mc);
    multi_cache_destroy(mc);
}

/* ------------------------------------------------------------------ */
/* Demo 2: AMAT sensitivity to L1 hit rate                             */
/* ------------------------------------------------------------------ */
static void demo_amat_sensitivity(void)
{
    printf("\n==================================================\n");
    printf("  Demo 2: AMAT vs L1 Hit Rate\n");
    printf("  L1=4, L2=12, L3=40, RAM=200 cycles\n");
    printf("  L2 HR fixed 80%%, L3 HR fixed 60%%\n");
    printf("==================================================\n");

    double l2_hr = 0.80, l3_hr = 0.60;
    double l1_lats[] = {4, 4, 4, 4, 4};
    double l1_hrs[]  = {0.50, 0.70, 0.90, 0.95, 0.99};

    printf("  %-8s | %-12s | %s\n", "L1 HR", "AMAT (cycles)", "Speedup vs 50%");
    printf("  -----------------------------------------------\n");

    double base_amat = -1;
    for (int i = 0; i < 5; i++) {
        double hr1 = l1_hrs[i];
        double amat = l1_lats[i]
                    + (1-hr1)*(12 + (1-l2_hr)*(40 + (1-l3_hr)*200));
        if (base_amat < 0) base_amat = amat;
        printf("  %-8.0f%% | %-13.2f | %.2fx\n",
               hr1*100, amat, base_amat/amat);
    }
}

/* ------------------------------------------------------------------ */
/* Demo 3: Trace-driven multi-level (simple.trace)                     */
/* ------------------------------------------------------------------ */
static void demo_trace_multilevel(void)
{
    printf("\n==================================================\n");
    printf("  Demo 3: Trace-Driven Multi-Level Simulation\n");
    printf("  File: traces/simple.trace\n");
    printf("==================================================\n");

    /* Realistic-ish but small configs */
    MultiSimConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.l1 = lvl(4, 2, LATENCY_L1_HIT, POLICY_LRU, "L1");
    cfg.l2 = lvl(8, 2, LATENCY_L2_HIT, POLICY_LRU, "L2");
    cfg.l3 = lvl(16,2, LATENCY_L3_HIT, POLICY_LRU, "L3");
    cfg.trace_path = "traces/simple.trace";

    MultiSimResult r = multi_sim_run(&cfg);
    multi_sim_print_report(&r, &cfg);
}

/* ------------------------------------------------------------------ */
/* Demo 4: Larger L1 = lower AMAT on trace                            */
/* ------------------------------------------------------------------ */
static void demo_l1_size_impact(void)
{
    printf("\n==================================================\n");
    printf("  Demo 4: L1 Size Impact on AMAT\n");
    printf("  Same trace, L2/L3 fixed, L1 grows\n");
    printf("==================================================\n");

    int l1_sets[] = {1, 2, 4, 8};
    int n = 4;

    printf("  %-14s | %-12s | %-8s | %s\n",
           "L1 size", "L1 Hit Rate", "RAM acc", "AMAT");
    printf("  -------------------------------------------------------\n");

    for (int i = 0; i < n; i++) {
        MultiSimConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.l1 = lvl(l1_sets[i], 2, LATENCY_L1_HIT, POLICY_LRU, "L1");
        cfg.l2 = lvl(8, 2, LATENCY_L2_HIT, POLICY_LRU, "L2");
        cfg.l3 = lvl(16,2, LATENCY_L3_HIT, POLICY_LRU, "L3");
        cfg.trace_path = "traces/simple.trace";

        MultiSimResult r = multi_sim_run(&cfg);
        int kb = l1_sets[i] * 2 * 64;
        printf("  %-3d sets (%3dB)  | %-11.2f%% | %-8llu | %.2f\n",
               l1_sets[i], kb, r.l1_hit_rate*100,
               (unsigned long long)r.ram_accesses, r.amat);
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("==================================================\n");
    printf("  CPU Cache Replacement Simulator -- Day 8\n");
    printf("  Multi-Level Cache Hierarchy + AMAT\n");
    printf("==================================================\n");

    demo_hit_waterfall();
    demo_amat_sensitivity();
    demo_trace_multilevel();
    demo_l1_size_impact();

    return EXIT_SUCCESS;
}
