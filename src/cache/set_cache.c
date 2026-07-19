/*
 * set_cache.c — N-Way Set-Associative Cache Implementation
 *
 * Core design:
 *   lines[]  — flat 2-D array [n_sets × n_ways], each element is a CacheLine
 *   order[]  — flat 2-D array [n_sets × n_ways], replacement counter per way
 *   freq[]   — flat 2-D array [n_sets × n_ways], frequency counter (LFU only)
 *   clock    — global monotonic counter, incremented on every access
 *
 * Replacement policy operates PER SET (not globally):
 *   LRU  : order[way] = clock on EVERY access → evict min order (oldest access)
 *   FIFO : order[way] = clock on INSERT only  → evict min order (oldest insert)
 *   LFU  : freq[way]++ on every access; evict min freq, LRU tie-break via order
 *
 * Address decomposition (from LSB to MSB):
 *   [offset_bits] → byte offset within the line   (not used for lookup)
 *   [index_bits]  → selects which set to look in
 *   [remaining]   → tag, stored in the cache line for comparison
 *
 *  CPU Cache Replacement Simulator
 */

#include "set_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Internal helpers ───────────────────────────────────────────── */

/** Integer log2 for powers of 2 (e.g. ilog2(64) = 6). */
static int ilog2(int n)
{
    int bits = 0;
    while ((1 << bits) < n) bits++;
    return bits;
}

/** Validate that n is a power of 2. */
static int is_power_of_two(int n)
{
    return (n > 0) && ((n & (n - 1)) == 0);
}

/* Convenience macros for flat 2-D indexing */
#define LINE(c, s, w)  ((c)->lines [(s) * (c)->n_ways + (w)])
#define ORDER(c, s, w) ((c)->order [(s) * (c)->n_ways + (w)])
#define FREQ(c, s, w)  ((c)->freq  [(s) * (c)->n_ways + (w)])

/* ─── Address decomposition ──────────────────────────────────────── */

void set_cache_decode(const SetAssocCache *cache, uint64_t address,
                      uint64_t *out_tag, int *out_set_index, int *out_offset)
{
    uint64_t offset_mask = (uint64_t)((1 << cache->offset_bits) - 1);
    uint64_t index_mask  = (uint64_t)((1 << cache->index_bits)  - 1);

    if (out_offset)    *out_offset    = (int)(address & offset_mask);
    if (out_set_index) *out_set_index = (int)((address >> cache->offset_bits) & index_mask);
    if (out_tag)       *out_tag       = address >> (cache->offset_bits + cache->index_bits);
}

/* ─── Per-set replacement logic ──────────────────────────────────── */

/**
 * Find which way to use for a new entry in the given set.
 * Priority: invalid (empty) way first → then apply policy to occupied ways.
 * Returns the way index [0, n_ways).
 */
static int find_victim_way(SetAssocCache *c, int set_idx)
{
    /* Prefer any invalid way — no eviction needed */
    for (int w = 0; w < c->n_ways; w++) {
        if (!LINE(c, set_idx, w).valid) return w;
    }

    /* All ways occupied — apply replacement policy */
    int victim = 0;

    switch (c->policy) {

        case POLICY_LRU:
        case POLICY_FIFO: {
            /* LRU: smallest order = least recently accessed
             * FIFO: smallest order = earliest inserted */
            uint64_t min_val = ORDER(c, set_idx, 0);
            for (int w = 1; w < c->n_ways; w++) {
                if (ORDER(c, set_idx, w) < min_val) {
                    min_val = ORDER(c, set_idx, w);
                    victim  = w;
                }
            }
            break;
        }

        case POLICY_LFU: {
            /* Evict minimum frequency; tie-break by minimum order (LRU) */
            uint64_t min_freq = FREQ(c,  set_idx, 0);
            uint64_t min_ord  = ORDER(c, set_idx, 0);
            for (int w = 1; w < c->n_ways; w++) {
                uint64_t f = FREQ(c,  set_idx, w);
                uint64_t o = ORDER(c, set_idx, w);
                if (f < min_freq || (f == min_freq && o < min_ord)) {
                    min_freq = f;
                    min_ord  = o;
                    victim   = w;
                }
            }
            break;
        }

        default:
            victim = 0;
    }

    return victim;
}

/**
 * Update replacement state after accessing way `w` in set `set_idx`.
 * `is_new_insert` = 1 on a miss (new line loaded), 0 on a hit.
 */
static void update_replacement_state(SetAssocCache *c, int set_idx,
                                     int way, int is_new_insert)
{
    switch (c->policy) {

        case POLICY_LRU:
            /* Update recency on EVERY access (hit or miss) */
            ORDER(c, set_idx, way) = ++c->clock;
            break;

        case POLICY_FIFO:
            /* Update only on new insertion — hits don't change FIFO order */
            if (is_new_insert)
                ORDER(c, set_idx, way) = ++c->clock;
            break;

        case POLICY_LFU:
            if (is_new_insert) {
                FREQ(c,  set_idx, way) = 1;           /* fresh entry */
                ORDER(c, set_idx, way) = ++c->clock;  /* insert time */
            } else {
                FREQ(c,  set_idx, way)++;              /* increment freq */
                ORDER(c, set_idx, way) = ++c->clock;  /* update recency for tie-break */
            }
            break;

        default:
            break;
    }
}

/* ─── Public API ─────────────────────────────────────────────────── */

SetAssocCache *set_cache_create(int n_sets, int n_ways,
                                int line_size, CachePolicy policy)
{
    /* Validate parameters:
     * n_sets and line_size MUST be powers of 2 (used in bit-shift address decode).
     * n_ways can be any positive integer (no address bit dependency). */
    if (!is_power_of_two(n_sets) || !is_power_of_two(line_size)) {
        fprintf(stderr, "[SetCache] n_sets and line_size must be powers of 2.\n");
        return NULL;
    }
    if (n_sets < 1 || n_ways < 1 || line_size < 1) {
        fprintf(stderr, "[SetCache] All parameters must be >= 1.\n");
        return NULL;
    }
    if (policy >= POLICY_COUNT) {
        fprintf(stderr, "[SetCache] Unknown policy.\n");
        return NULL;
    }

    SetAssocCache *c = (SetAssocCache *)calloc(1, sizeof(SetAssocCache));
    if (!c) return NULL;

    c->n_sets      = n_sets;
    c->n_ways      = n_ways;
    c->line_size   = line_size;
    c->offset_bits = ilog2(line_size);
    c->index_bits  = ilog2(n_sets);
    c->policy      = policy;
    c->clock       = 0;
    c->hits        = 0;
    c->misses      = 0;
    c->evictions   = 0;

    static const char *pnames[] = {"LRU", "FIFO", "LFU"};
    c->policy_name = pnames[policy];

    size_t total_ways = (size_t)(n_sets * n_ways);

    c->lines = (CacheLine *)calloc(total_ways, sizeof(CacheLine));
    c->order = (uint64_t  *)calloc(total_ways, sizeof(uint64_t));
    c->freq  = (uint64_t  *)calloc(total_ways, sizeof(uint64_t));

    if (!c->lines || !c->order || !c->freq) {
        free(c->lines); free(c->order); free(c->freq); free(c);
        return NULL;
    }

    return c;
}

int set_cache_access(SetAssocCache *cache, uint64_t address)
{
    uint64_t tag;
    int      set_idx, offset;
    set_cache_decode(cache, address, &tag, &set_idx, &offset);
    (void)offset;   /* not needed for hit/miss logic */

    /* ── Tag search: check all ways in the set ── */
    for (int w = 0; w < cache->n_ways; w++) {
        CacheLine *line = &LINE(cache, set_idx, w);
        if (line->valid && line->tag == tag) {
            /* HIT */
            update_replacement_state(cache, set_idx, w, 0 /* not new */);
            cache->hits++;
            return CACHE_HIT;
        }
    }

    /* ── MISS ── */
    cache->misses++;

    int victim_way = find_victim_way(cache, set_idx);
    CacheLine *victim = &LINE(cache, set_idx, victim_way);

    if (victim->valid) {
        /* Evicting an occupied way */
        cache->evictions++;
    }

    /* Load new line into victim way */
    victim->tag   = tag;
    victim->valid = 1;
    victim->dirty = 0;

    /* Reset freq on new insert (LFU handled inside update_replacement_state) */
    FREQ(cache, set_idx, victim_way) = 0;
    update_replacement_state(cache, set_idx, victim_way, 1 /* new insert */);

    return CACHE_MISS;
}

void set_cache_print_stats(const SetAssocCache *cache)
{
    uint64_t total = cache->hits + cache->misses;
    double   hr    = (total > 0)
                     ? (100.0 * (double)cache->hits / (double)total)
                     : 0.0;

    int total_size_kb = (cache->n_sets * cache->n_ways * cache->line_size) / 1024;

    printf("----------------------------------------------\n");
    printf("Set-Associative Cache - %s Policy\n", cache->policy_name);
    printf("----------------------------------------------\n");
    printf("  Config     : %d-way, %d sets, %d-byte lines\n",
           cache->n_ways, cache->n_sets, cache->line_size);
    printf("  Total size : %d KB  (%d bytes)\n",
           total_size_kb,
           cache->n_sets * cache->n_ways * cache->line_size);
    printf("  Addr bits  : offset=%d, index=%d, tag=%d\n",
           cache->offset_bits, cache->index_bits,
           64 - cache->offset_bits - cache->index_bits);
    printf("  Accesses   : %llu\n",   (unsigned long long)total);
    printf("  Hits       : %llu\n",   (unsigned long long)cache->hits);
    printf("  Misses     : %llu\n",   (unsigned long long)cache->misses);
    printf("  Evictions  : %llu\n",   (unsigned long long)cache->evictions);
    printf("  Hit Rate   : %.2f%%\n", hr);
    printf("----------------------------------------------\n");
}

void set_cache_print_contents(const SetAssocCache *cache)
{
    printf("Cache contents (%d sets × %d ways):\n",
           cache->n_sets, cache->n_ways);
    for (int s = 0; s < cache->n_sets; s++) {
        printf("  Set %2d: ", s);
        int any_valid = 0;
        for (int w = 0; w < cache->n_ways; w++) {
            CacheLine *line = &LINE(cache, s, w);
            if (line->valid) {
                printf("[Way%d tag=0x%llx] ", w,
                       (unsigned long long)line->tag);
                any_valid = 1;
            } else {
                printf("[Way%d ---] ", w);
            }
        }
        if (!any_valid) printf("(empty)");
        printf("\n");
    }
}

void set_cache_destroy(SetAssocCache *cache)
{
    if (!cache) return;
    free(cache->lines);
    free(cache->order);
    free(cache->freq);
    free(cache);
}
