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

    for (size_t i = 0; i < n; i++) {
        /* stride by one cache line, wrap at region boundary */
        a[i] = WL_BASE_ADDR + ((i % (size_t)N_LINES) * WL_LINE_SIZE);
    }
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
