/*
 * multi_cache.c -- Multi-Level Cache Hierarchy Implementation
 *
 * Access flow (inclusive policy):
 *
 *   multi_cache_access(addr):
 *     if L1 HIT  -> l1_hits++,  return 1
 *     if L2 HIT  -> l2_hits++,  load into L1, return 2
 *     if L3 HIT  -> l3_hits++,  load into L2 and L1, return 3
 *     else       -> ram_accesses++, load into all 3 levels, return 0
 *
 * "Load into" = call set_cache_access on lower levels.
 * Because set_cache_access inserts on miss and is a no-op on hit,
 * calling it on L1/L2 after a deeper-level hit correctly installs
 * the line without needing a separate "insert" API.
 *
 * AMAT formula:
 *   AMAT = L1_lat + (1-HR_L1) * [L2_lat + (1-HR_L2) * [L3_lat + (1-HR_L3) * RAM_lat]]
 *
 * where HR_Lx = hit rate AT that level (hits / accesses_that_reach_it).
 *
 * Day 8 -- CPU Cache Replacement Simulator
 */

#include "multi_cache.h"
#include "trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Default configs (realistic Intel Skylake desktop)                   */
/* ------------------------------------------------------------------ */

const CacheLevelConfig DEFAULT_L1 = {
    .n_sets      = 64,          /* 32KB / (8 * 64) = 64 sets  */
    .n_ways      = 8,
    .line_size   = 64,
    .hit_latency = LATENCY_L1_HIT,
    .policy      = POLICY_LRU,
    .name        = "L1"
};

const CacheLevelConfig DEFAULT_L2 = {
    .n_sets      = 512,         /* 256KB / (8 * 64) = 512 sets */
    .n_ways      = 8,
    .line_size   = 64,
    .hit_latency = LATENCY_L2_HIT,
    .policy      = POLICY_LRU,
    .name        = "L2"
};

const CacheLevelConfig DEFAULT_L3 = {
    .n_sets      = 2048,        /* 8MB / (64 * 64) = 2048 sets */
    .n_ways      = 64,
    .line_size   = 64,
    .hit_latency = LATENCY_L3_HIT,
    .policy      = POLICY_LRU,
    .name        = "L3"
};

/* ------------------------------------------------------------------ */
/* Internal: create one cache level                                     */
/* ------------------------------------------------------------------ */
static SetAssocCache *make_level(const CacheLevelConfig *cfg)
{
    SetAssocCache *c = set_cache_create(cfg->n_sets, cfg->n_ways,
                                        cfg->line_size, cfg->policy);
    if (!c) {
        fprintf(stderr, "[multi_cache] Failed to create %s cache.\n", cfg->name);
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

MultiLevelCache *multi_cache_create(const CacheLevelConfig *l1,
                                    const CacheLevelConfig *l2,
                                    const CacheLevelConfig *l3)
{
    MultiLevelCache *mc = (MultiLevelCache *)calloc(1, sizeof(MultiLevelCache));
    if (!mc) return NULL;

    mc->cfg_l1 = l1 ? *l1 : DEFAULT_L1;
    mc->cfg_l2 = l2 ? *l2 : DEFAULT_L2;
    mc->cfg_l3 = l3 ? *l3 : DEFAULT_L3;

    /* Ensure level names are set */
    if (!mc->cfg_l1.name) mc->cfg_l1.name = "L1";
    if (!mc->cfg_l2.name) mc->cfg_l2.name = "L2";
    if (!mc->cfg_l3.name) mc->cfg_l3.name = "L3";

    mc->l1 = make_level(&mc->cfg_l1);
    mc->l2 = make_level(&mc->cfg_l2);
    mc->l3 = make_level(&mc->cfg_l3);

    if (!mc->l1 || !mc->l2 || !mc->l3) {
        multi_cache_destroy(mc);
        return NULL;
    }

    return mc;
}

int multi_cache_access(MultiLevelCache *mc, uint64_t address)
{
    mc->total_accesses++;

    /* ---- Level 1 ---- */
    if (set_cache_access(mc->l1, address) == CACHE_HIT) {
        mc->l1_hits++;
        return 1;   /* L1 hit */
    }
    mc->l1_misses++;

    /* ---- Level 2 ---- */
    if (set_cache_access(mc->l2, address) == CACHE_HIT) {
        mc->l2_hits++;
        /* Install into L1 (inclusive: L1 miss but L2 had it -> bring to L1) */
        set_cache_access(mc->l1, address);
        return 2;   /* L2 hit */
    }
    mc->l2_misses++;

    /* ---- Level 3 ---- */
    if (set_cache_access(mc->l3, address) == CACHE_HIT) {
        mc->l3_hits++;
        /* Install into L2 and L1 */
        set_cache_access(mc->l2, address);
        set_cache_access(mc->l1, address);
        return 3;   /* L3 hit */
    }
    mc->l3_misses++;

    /* ---- RAM fetch ---- */
    mc->ram_accesses++;
    /* Install into all 3 levels (memory fetch brings line into whole hierarchy) */
    set_cache_access(mc->l3, address);
    set_cache_access(mc->l2, address);
    set_cache_access(mc->l1, address);

    return 0;   /* RAM access */
}

double multi_cache_compute_amat(MultiLevelCache *mc)
{
    /*
     * Compute per-level hit rates:
     *   l1_hit_rate = l1_hits / total_accesses
     *   l2_hit_rate = l2_hits / l1_misses   (only L1 misses reach L2)
     *   l3_hit_rate = l3_hits / l2_misses   (only L2 misses reach L3)
     */
    double hr1 = (mc->total_accesses > 0)
                 ? (double)mc->l1_hits / (double)mc->total_accesses
                 : 0.0;
    double hr2 = (mc->l1_misses > 0)
                 ? (double)mc->l2_hits / (double)mc->l1_misses
                 : 0.0;
    double hr3 = (mc->l2_misses > 0)
                 ? (double)mc->l3_hits / (double)mc->l2_misses
                 : 0.0;

    mc->l1_hit_rate = hr1;
    mc->l2_hit_rate = hr2;
    mc->l3_hit_rate = hr3;

    double l1_lat  = (double)mc->cfg_l1.hit_latency;
    double l2_lat  = (double)mc->cfg_l2.hit_latency;
    double l3_lat  = (double)mc->cfg_l3.hit_latency;
    double ram_lat = (double)LATENCY_RAM;

    /*
     * AMAT = L1_lat + (1-HR1) * [L2_lat + (1-HR2) * [L3_lat + (1-HR3) * RAM_lat]]
     */
    double mr1 = 1.0 - hr1;
    double mr2 = 1.0 - hr2;
    double mr3 = 1.0 - hr3;

    mc->amat = l1_lat
             + mr1 * (l2_lat
             + mr2 * (l3_lat
             + mr3 * ram_lat));

    return mc->amat;
}

void multi_cache_print_stats(const MultiLevelCache *mc)
{
    /* Cast away const to call compute -- stats are read-only here */
    multi_cache_compute_amat((MultiLevelCache *)mc);

    uint64_t total = mc->total_accesses;

    printf("==============================================\n");
    printf("  Multi-Level Cache Hierarchy Statistics\n");
    printf("==============================================\n");
    printf("  Total accesses : %llu\n", (unsigned long long)total);
    printf("\n");

    /* Per-level table */
    printf("  %-4s | %-8s | %-6s | %-6s | %-9s | Config\n",
           "Lvl", "Accesses", "Hits", "Misses", "Hit Rate");
    printf("  -----------------------------------------------------------------\n");

    uint64_t l1_acc = total;
    uint64_t l2_acc = mc->l1_misses;
    uint64_t l3_acc = mc->l2_misses;

#define PRINT_LEVEL_ROW(name, accesses, hits, misses, cfg_ptr)             \
    do {                                                                    \
        double hr = ((accesses) > 0)                                       \
                    ? 100.0*(double)(hits)/(double)(accesses) : 0.0;       \
        int sz_kb = (cfg_ptr)->n_sets*(cfg_ptr)->n_ways*(cfg_ptr)->line_size/1024; \
        printf("  %-4s | %-8llu | %-6llu | %-6llu | %8.2f%% | "          \
               "%d-way %dsets %dKB\n",                                     \
               (name),                                                     \
               (unsigned long long)(accesses),                             \
               (unsigned long long)(hits),                                 \
               (unsigned long long)(misses),                               \
               hr, (cfg_ptr)->n_ways, (cfg_ptr)->n_sets, sz_kb);          \
    } while(0)

    PRINT_LEVEL_ROW("L1", l1_acc, mc->l1_hits, mc->l1_misses, &mc->cfg_l1);
    PRINT_LEVEL_ROW("L2", l2_acc, mc->l2_hits, mc->l2_misses, &mc->cfg_l2);
    PRINT_LEVEL_ROW("L3", l3_acc, mc->l3_hits, mc->l3_misses, &mc->cfg_l3);

#undef PRINT_LEVEL_ROW

    printf("  %-4s | %-8llu | %-6s | %-6llu | %-9s | DRAM\n",
           "RAM", (unsigned long long)l3_acc,
           "-",
           (unsigned long long)mc->ram_accesses,
           "-");

    printf("\n");
    printf("  Latencies (cycles) : L1=%d  L2=%d  L3=%d  RAM=%d\n",
           mc->cfg_l1.hit_latency, mc->cfg_l2.hit_latency,
           mc->cfg_l3.hit_latency, LATENCY_RAM);
    printf("\n");
    printf("  AMAT Calculation:\n");
    printf("    AMAT = %d + (1-%.4f)*[%d + (1-%.4f)*[%d + (1-%.4f)*%d]]\n",
           mc->cfg_l1.hit_latency, mc->l1_hit_rate,
           mc->cfg_l2.hit_latency, mc->l2_hit_rate,
           mc->cfg_l3.hit_latency, mc->l3_hit_rate,
           LATENCY_RAM);
    printf("    AMAT = %.4f cycles\n", mc->amat);
    printf("==============================================\n");
}

void multi_cache_destroy(MultiLevelCache *mc)
{
    if (!mc) return;
    set_cache_destroy(mc->l1);
    set_cache_destroy(mc->l2);
    set_cache_destroy(mc->l3);
    free(mc);
}

/* ------------------------------------------------------------------ */
/* Trace-driven simulation                                             */
/* ------------------------------------------------------------------ */

MultiSimResult multi_sim_run(const MultiSimConfig *cfg)
{
    MultiSimResult r;
    memset(&r, 0, sizeof(r));

    MultiLevelCache *mc = multi_cache_create(&cfg->l1, &cfg->l2, &cfg->l3);
    if (!mc) { r.success = 0; return r; }

    TraceReader *tr = trace_open(cfg->trace_path);
    if (!tr) {
        multi_cache_destroy(mc);
        r.success = 0;
        return r;
    }

    TraceRecord rec;
    while (trace_next(tr, &rec) == 1) {
        multi_cache_access(mc, rec.address);
        if (rec.type == ACCESS_MODIFY)
            multi_cache_access(mc, rec.address);  /* write access */
    }

    multi_cache_compute_amat(mc);

    r.total_accesses = mc->total_accesses;
    r.l1_hits        = mc->l1_hits;
    r.l2_hits        = mc->l2_hits;
    r.l3_hits        = mc->l3_hits;
    r.ram_accesses   = mc->ram_accesses;
    r.l1_hit_rate    = mc->l1_hit_rate;
    r.l2_hit_rate    = mc->l2_hit_rate;
    r.l3_hit_rate    = mc->l3_hit_rate;
    r.amat           = mc->amat;
    r.success        = 1;

    trace_close(tr);
    multi_cache_destroy(mc);
    return r;
}

void multi_sim_print_report(const MultiSimResult *r, const MultiSimConfig *cfg)
{
    if (!r || !r->success) {
        printf("[multi_sim] Simulation failed.\n");
        return;
    }

    printf("==============================================\n");
    printf("  Multi-Level Simulation Report\n");
    printf("==============================================\n");
    printf("  Trace : %s\n", cfg->trace_path);
    printf("  L1    : %d-way %d sets %dB lines (%dKB) %s\n",
           cfg->l1.n_ways, cfg->l1.n_sets, cfg->l1.line_size,
           cfg->l1.n_sets * cfg->l1.n_ways * cfg->l1.line_size / 1024,
           cfg->l1.name ? cfg->l1.name : "");
    printf("  L2    : %d-way %d sets %dB lines (%dKB) %s\n",
           cfg->l2.n_ways, cfg->l2.n_sets, cfg->l2.line_size,
           cfg->l2.n_sets * cfg->l2.n_ways * cfg->l2.line_size / 1024,
           cfg->l2.name ? cfg->l2.name : "");
    printf("  L3    : %d-way %d sets %dB lines (%dKB) %s\n",
           cfg->l3.n_ways, cfg->l3.n_sets, cfg->l3.line_size,
           cfg->l3.n_sets * cfg->l3.n_ways * cfg->l3.line_size / 1024,
           cfg->l3.name ? cfg->l3.name : "");
    printf("----------------------------------------------\n");
    printf("  Total accesses : %llu\n", (unsigned long long)r->total_accesses);
    printf("  L1 hits        : %llu  (%.2f%%)\n",
           (unsigned long long)r->l1_hits,  r->l1_hit_rate * 100.0);
    printf("  L2 hits        : %llu  (%.2f%% of L1 misses)\n",
           (unsigned long long)r->l2_hits,  r->l2_hit_rate * 100.0);
    printf("  L3 hits        : %llu  (%.2f%% of L2 misses)\n",
           (unsigned long long)r->l3_hits,  r->l3_hit_rate * 100.0);
    printf("  RAM accesses   : %llu\n",
           (unsigned long long)r->ram_accesses);
    printf("----------------------------------------------\n");
    printf("  AMAT           : %.4f cycles\n", r->amat);
    printf("==============================================\n");
}
