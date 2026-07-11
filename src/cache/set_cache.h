/*
 * set_cache.h — N-Way Set-Associative Cache
 *
 * Models a realistic CPU cache with:
 *   - Multiple sets (indexed by address bits)
 *   - Multiple ways per set (associativity)
 *   - Per-set replacement using LRU, FIFO, or LFU
 *   - Tag/index/offset address decomposition
 *
 * Address bit layout (for line_size=64, n_sets=N):
 *
 *   [63 ............. tag ............. | index | offset]
 *    63            (6+log2N)  (log2N+6)  log2N    6     0
 *
 * Example — 4KB, 4-way, 64B lines:
 *   n_sets = 4096 / (4 * 64) = 16 sets
 *   offset_bits = 6   (log2 64)
 *   index_bits  = 4   (log2 16)
 *   tag_bits    = 54  (64 - 6 - 4)
 *
 * Day 6 — CPU Cache Replacement Simulator
 */

#ifndef SET_CACHE_H
#define SET_CACHE_H

#include <stdint.h>
#include "cache.h"      /* CachePolicy enum */

/* Result codes (guard against redefinition from lru.h) */
#ifndef CACHE_HIT
#define CACHE_HIT  1
#define CACHE_MISS 0
#endif

/* ─── One cache line (one way in one set) ───────────────────────── */
typedef struct {
    uint64_t tag;
    uint8_t  valid;
    uint8_t  dirty;   /* for write-back simulation */
} CacheLine;

/* ─── N-Way Set-Associative Cache ───────────────────────────────── */
typedef struct {
    /* Configuration */
    int n_sets;           /* Number of sets                         */
    int n_ways;           /* Ways per set (associativity)           */
    int line_size;        /* Bytes per cache line (must be power-2) */
    int offset_bits;      /* log2(line_size)                        */
    int index_bits;       /* log2(n_sets)                           */

    /* Cache data: flat 2-D arrays [n_sets × n_ways] */
    CacheLine *lines;     /* lines[set * n_ways + way]              */

    /* Per-way replacement state [n_sets × n_ways] */
    uint64_t *order;      /* recency or insert timestamp per way    */
    uint64_t *freq;       /* access frequency per way (LFU)         */
    uint64_t  clock;      /* monotonic counter                      */

    CachePolicy policy;
    const char *policy_name;

    /* Statistics */
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} SetAssocCache;

/* ─── Public API ────────────────────────────────────────────────── */

/**
 * Create an N-way set-associative cache.
 *
 * @param n_sets     Number of sets (must be power of 2)
 * @param n_ways     Ways per set: 1=direct-mapped, 2,4,8=set-associative
 * @param line_size  Cache line size in bytes (must be power of 2, e.g. 64)
 * @param policy     LRU, FIFO, or LFU replacement within each set
 *
 * Total cache size = n_sets * n_ways * line_size bytes.
 */
SetAssocCache *set_cache_create(int n_sets, int n_ways,
                                int line_size, CachePolicy policy);

/**
 * Access the cache with a memory address.
 * Returns CACHE_HIT (1) or CACHE_MISS (0).
 */
int set_cache_access(SetAssocCache *cache, uint64_t address);

/**
 * Decode an address into its set index and tag.
 * Useful for debugging and tests.
 */
void set_cache_decode(const SetAssocCache *cache, uint64_t address,
                      uint64_t *out_tag, int *out_set_index, int *out_offset);

/** Print cache configuration and hit/miss statistics. */
void set_cache_print_stats(const SetAssocCache *cache);

/** Print contents of every set (tag, valid, way number). */
void set_cache_print_contents(const SetAssocCache *cache);

/** Free all memory. */
void set_cache_destroy(SetAssocCache *cache);

#endif /* SET_CACHE_H */
