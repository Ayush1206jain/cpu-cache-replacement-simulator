/*
 * workload.c -- Synthetic Workload Generator Implementations
 *
 * Day 11 -- CPU Cache Replacement Simulator
 */

#include "workload.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Number of distinct cache-line-aligned addresses in the region */
#define N_LINES (WL_REGION_SIZE / WL_LINE_SIZE)

/* ------------------------------------------------------------------ */
/* Sequential                                                           */
/* ------------------------------------------------------------------ */
uint64_t *workload_sequential(size_t n)
{
    uint64_t *a = (uint64_t *)malloc(n * sizeof(uint64_t));
    if (!a) return NULL;

    /*
     * Use word-stride (8B) over a SMALL 3KB scan window (48 cache lines).
     *
     * Why 3KB?  With 4-way 64B-line caches:
     *   1KB  → 4  sets × 4 ways = 16 lines capacity  (48 > 16 → THRASH)
     *   4KB  → 16 sets × 4 ways = 64 lines capacity  (48 < 64 → FITS!)
     *   16KB → 64 sets × 4 ways = 256 lines capacity (48 << 256 → FITS)
     *   64KB → 256 sets × 4 ways                    (FITS)
     *
     * Expected hit rates:
     *   1KB  : ~87.5%  (intra-line only, thrashes between passes)
     *   4KB+ : ~99%    (all 48 lines cached after pass 1, inter-pass hits)
     *
     * Each cache line is accessed 8 times consecutively (word stride):
     *   7/8 = 87.5% intra-line spatial-locality hits always present.
     *   Inter-pass temporal hits add on top when working set fits.
     */
    const size_t WORD_SIZE    = 8;
    const size_t SEQ_BYTES    = 3 * 1024;            /* 3KB hot window       */
    const size_t n_words      = SEQ_BYTES / WORD_SIZE; /* 384 words, 48 lines */

    for (size_t i = 0; i < n; i++)
        a[i] = WL_BASE_ADDR + (i % n_words) * WORD_SIZE;

    return a;
}



/* ------------------------------------------------------------------ */
/* Random (uniform)                                                     */
/* ------------------------------------------------------------------ */
uint64_t *workload_random(size_t n, unsigned int seed)
{
    uint64_t *a = (uint64_t *)malloc(n * sizeof(uint64_t));
    if (!a) return NULL;

    /* Use a simple xorshift32 so results are reproducible per seed */
    uint32_t state = (seed == 0) ? 123456789u : seed;
    for (size_t i = 0; i < n; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        uint32_t line_idx = state % (uint32_t)N_LINES;
        a[i] = WL_BASE_ADDR + (uint64_t)line_idx * WL_LINE_SIZE;
    }
    return a;
}

/* ------------------------------------------------------------------ */
/* Build 2 workloads at once (Sequential + Random)                      */
/* ------------------------------------------------------------------ */
Workload *workload_build_all(size_t n_accesses, unsigned int seed)
{
    Workload *wl = (Workload *)calloc(2, sizeof(Workload));
    if (!wl) return NULL;

    wl[0].name  = "Sequential";
    wl[0].addrs = workload_sequential(n_accesses);
    wl[0].n     = n_accesses;

    wl[1].name  = "Random";
    wl[1].addrs = workload_random(n_accesses, seed);
    wl[1].n     = n_accesses;

    /* Check for allocation failures */
    for (int i = 0; i < 2; i++) {
        if (!wl[i].addrs) {
            fprintf(stderr, "[workload] Allocation failed for '%s'\n", wl[i].name);
            workload_free_all(wl, 2);
            return NULL;
        }
    }

    return wl;
}

void workload_free_all(Workload *wl, int count)
{
    if (!wl) return;
    for (int i = 0; i < count; i++)
        free(wl[i].addrs);
    free(wl);
}
