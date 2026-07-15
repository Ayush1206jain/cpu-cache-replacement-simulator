/*
 * multi_cache.h -- Multi-Level Cache Hierarchy (L1 + L2 + L3)
 *
 * Models a realistic 3-level cache hierarchy:
 *
 *   CPU --> [L1] --> [L2] --> [L3] --> RAM
 *
 * Access flow (inclusive hierarchy):
 *   1. Check L1. HIT -> done.
 *   2. L1 MISS -> check L2. HIT -> load into L1, done.
 *   3. L2 MISS -> check L3. HIT -> load into L2 and L1, done.
 *   4. L3 MISS -> fetch from RAM, load into all 3 levels.
 *
 * Each level is an independent SetAssocCache with its own
 * capacity, associativity, and replacement policy.
 *
 * AMAT (Average Memory Access Time):
 *   AMAT = L1_hit_time
 *        + L1_miss_rate * (L2_hit_time
 *        + L2_miss_rate * (L3_hit_time
 *        + L3_miss_rate * RAM_penalty))
 *
 * Typical real-world cycle counts (Intel Skylake):
 *   L1  : 4  cycles
 *   L2  : 12 cycles
 *   L3  : 40 cycles
 *   RAM : 200 cycles
 *
 * Day 8 -- CPU Cache Replacement Simulator
 */

#ifndef MULTI_CACHE_H
#define MULTI_CACHE_H

#include <stdint.h>
#include "cache.h"        /* CachePolicy enum */
#include "set_cache.h"    /* SetAssocCache    */

/* ------------------------------------------------------------------ */
/* Latency constants (cycles) -- realistic Intel Skylake values         */
/* ------------------------------------------------------------------ */
#define LATENCY_L1_HIT   4
#define LATENCY_L2_HIT   12
#define LATENCY_L3_HIT   40
#define LATENCY_RAM      200

/* ------------------------------------------------------------------ */
/* Per-level configuration                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    int         n_sets;
    int         n_ways;
    int         line_size;
    int         hit_latency;   /* cycles for a hit at this level */
    CachePolicy policy;
    const char *name;          /* "L1", "L2", "L3" */
} CacheLevelConfig;

/* ------------------------------------------------------------------ */
/* Per-level statistics                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t accesses;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    double   hit_rate;
    double   miss_rate;
} CacheLevelStats;

/* ------------------------------------------------------------------ */
/* Multi-level cache hierarchy                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    SetAssocCache *l1;
    SetAssocCache *l2;
    SetAssocCache *l3;

    CacheLevelConfig cfg_l1;
    CacheLevelConfig cfg_l2;
    CacheLevelConfig cfg_l3;

    /* Per-level hit/miss counters */
    uint64_t l1_hits,  l1_misses;
    uint64_t l2_hits,  l2_misses;
    uint64_t l3_hits,  l3_misses;
    uint64_t ram_accesses;

    uint64_t total_accesses;

    /* AMAT components (computed by multi_cache_compute_amat) */
    double amat;           /* overall AMAT in cycles   */
    double l1_hit_rate;
    double l2_hit_rate;    /* hit rate among L1 misses */
    double l3_hit_rate;    /* hit rate among L2 misses */
} MultiLevelCache;

/* ------------------------------------------------------------------ */
/* Predefined realistic configs (convenience)                          */
/* ------------------------------------------------------------------ */

/* Intel-like desktop L1: 32KB, 8-way, 64B lines */
extern const CacheLevelConfig DEFAULT_L1;

/* Intel-like desktop L2: 256KB, 4-way, 64B lines */
extern const CacheLevelConfig DEFAULT_L2;

/* Intel-like desktop L3: 8MB, 16-way, 64B lines  */
extern const CacheLevelConfig DEFAULT_L3;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * Create a 3-level cache hierarchy.
 * Pass NULL for any level config to use the defaults above.
 */
MultiLevelCache *multi_cache_create(const CacheLevelConfig *l1,
                                    const CacheLevelConfig *l2,
                                    const CacheLevelConfig *l3);

/**
 * Access an address through the hierarchy.
 * Returns the level where the hit occurred:
 *   1 = L1 hit, 2 = L2 hit, 3 = L3 hit, 0 = RAM access
 */
int multi_cache_access(MultiLevelCache *mc, uint64_t address);

/**
 * Compute AMAT from accumulated statistics.
 * Uses the latency values in each level's config.
 * Stores result in mc->amat.
 */
double multi_cache_compute_amat(MultiLevelCache *mc);

/** Print per-level statistics and AMAT. */
void multi_cache_print_stats(const MultiLevelCache *mc);

/** Free all memory. */
void multi_cache_destroy(MultiLevelCache *mc);

/* ------------------------------------------------------------------ */
/* Trace-driven multi-level simulation (convenience wrapper)           */
/* ------------------------------------------------------------------ */

typedef struct {
    CacheLevelConfig l1, l2, l3;
    const char      *trace_path;
} MultiSimConfig;

typedef struct {
    uint64_t total_accesses;
    uint64_t l1_hits, l2_hits, l3_hits, ram_accesses;
    double   l1_hit_rate, l2_hit_rate, l3_hit_rate;
    double   amat;
    int      success;
} MultiSimResult;

/** Run a full trace through a 3-level hierarchy. */
MultiSimResult multi_sim_run(const MultiSimConfig *cfg);

/** Print a full multi-level simulation report. */
void multi_sim_print_report(const MultiSimResult *r,
                             const MultiSimConfig *cfg);

#endif /* MULTI_CACHE_H */
