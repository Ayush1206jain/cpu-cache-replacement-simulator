/*
 * workload.c -- Synthetic Workload Generator Implementations
 *
 * Day 11 -- CPU Cache Replacement Simulator
 */

#include "workload.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
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
/* Zipfian (power-law)                                                  */
/*                                                                      */
/* Inverse-CDF method:                                                  */
/*   1. Pre-compute CDF: cdf[k] = sum_{j=1}^{k} (1/j^alpha) / H_n    */
/*      where H_n = sum_{j=1}^{N_LINES} 1/j^alpha  (normalising const)*/
/*   2. Draw uniform u in [0,1)                                        */
/*   3. Binary-search CDF to find rank k                               */
/*   4. Map rank k -> address: address[k] = BASE + k * LINE_SIZE       */
/*                                                                      */
/* With alpha=1.0: rank-1 address gets 1/H_n fraction of all accesses  */
/* ------------------------------------------------------------------ */
uint64_t *workload_zipfian(size_t n, unsigned int seed, double alpha)
{
    uint64_t *a = (uint64_t *)malloc(n * sizeof(uint64_t));
    if (!a) return NULL;

    /* Limit vocabulary size for speed (top 1024 lines is representative) */
    int vocab = (N_LINES < 1024) ? N_LINES : 1024;

    /* Build CDF table */
    double *cdf = (double *)malloc((size_t)vocab * sizeof(double));
    if (!cdf) { free(a); return NULL; }

    double sum = 0.0;
    for (int k = 1; k <= vocab; k++)
        sum += 1.0 / pow((double)k, alpha);

    double running = 0.0;
    for (int k = 0; k < vocab; k++) {
        running += 1.0 / pow((double)(k + 1), alpha);
        cdf[k] = running / sum;
    }

    /* xorshift32 for reproducible randomness */
    uint32_t state = (seed == 0) ? 987654321u : seed ^ 0xDEADBEEFu;

    for (size_t i = 0; i < n; i++) {
        /* Generate uniform float in [0,1) */
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        double u = (double)(state & 0x7FFFFFFFu) / (double)0x80000000u;

        /* Binary search for rank */
        int lo = 0, hi = vocab - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (cdf[mid] < u) lo = mid + 1;
            else hi = mid;
        }
        a[i] = WL_BASE_ADDR + (uint64_t)lo * WL_LINE_SIZE;
    }

    free(cdf);
    return a;
}

/* ------------------------------------------------------------------ */
/* Mixed (60% sequential + 20% random + 20% zipfian)                   */
/* ------------------------------------------------------------------ */
uint64_t *workload_mixed(size_t n, unsigned int seed)
{
    /* Generate the three component workloads */
    size_t n_seq  = (size_t)(n * 0.60);
    size_t n_rand = (size_t)(n * 0.20);
    size_t n_zip  = n - n_seq - n_rand;

    uint64_t *seq  = workload_sequential(n_seq);
    uint64_t *rnd  = workload_random(n_rand, seed);
    uint64_t *zip  = workload_zipfian(n_zip, seed ^ 0xCAFEu, 1.0);

    if (!seq || !rnd || !zip) {
        free(seq); free(rnd); free(zip);
        return NULL;
    }

    /* Interleave: for every 10 accesses -- 6 seq, 2 rnd, 2 zip */
    uint64_t *a = (uint64_t *)malloc(n * sizeof(uint64_t));
    if (!a) { free(seq); free(rnd); free(zip); return NULL; }

    size_t si = 0, ri = 0, zi = 0, ai = 0;
    while (ai < n) {
        /* 6 sequential */
        for (int k = 0; k < 6 && ai < n && si < n_seq; k++)
            a[ai++] = seq[si++];
        /* 2 random */
        for (int k = 0; k < 2 && ai < n && ri < n_rand; k++)
            a[ai++] = rnd[ri++];
        /* 2 zipfian */
        for (int k = 0; k < 2 && ai < n && zi < n_zip; k++)
            a[ai++] = zip[zi++];
    }

    free(seq); free(rnd); free(zip);
    return a;
}

/* ------------------------------------------------------------------ */
/* Build all 4 workloads at once                                        */
/* ------------------------------------------------------------------ */
Workload *workload_build_all(size_t n_accesses, unsigned int seed)
{
    Workload *wl = (Workload *)calloc(4, sizeof(Workload));
    if (!wl) return NULL;

    wl[0].name  = "Sequential";
    wl[0].addrs = workload_sequential(n_accesses);
    wl[0].n     = n_accesses;

    wl[1].name  = "Random";
    wl[1].addrs = workload_random(n_accesses, seed);
    wl[1].n     = n_accesses;

    wl[2].name  = "Zipfian";
    wl[2].addrs = workload_zipfian(n_accesses, seed, 1.0);
    wl[2].n     = n_accesses;

    wl[3].name  = "Mixed";
    wl[3].addrs = workload_mixed(n_accesses, seed);
    wl[3].n     = n_accesses;

    /* Check for allocation failures */
    for (int i = 0; i < 4; i++) {
        if (!wl[i].addrs) {
            fprintf(stderr, "[workload] Allocation failed for '%s'\n", wl[i].name);
            workload_free_all(wl, 4);
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
