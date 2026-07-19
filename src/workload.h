/*
 * workload.h -- Synthetic Workload Generators for Cache Benchmarking
 *
 * Two access-pattern generators, each producing a flat array of
 * uint64_t memory addresses for set_cache_access().
 *
 *  sequential() -- linear scan of a memory region (tests spatial locality)
 *  random()     -- uniform random across the address space (worst case)
 *
 * Caller owns the returned array and must free() it.
 *
 * CPU Cache Replacement Simulator
 */

#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stdint.h>
#include <stddef.h>

/* Base address and region size for generated workloads */
#define WL_BASE_ADDR   0x100000ULL   /* 1 MB base address */
#define WL_REGION_SIZE (32 * 1024)   /* 32 KB -- small enough that 64KB cache fits all */
#define WL_LINE_SIZE   64            /* match cache line granularity */

/* ------------------------------------------------------------------ */
/* Generator functions                                                  */
/* Each returns a malloc'd array of `n` uint64_t addresses.           */
/* Caller must free() the returned pointer.                            */
/* Returns NULL on allocation failure.                                 */
/* ------------------------------------------------------------------ */

/**
 * Sequential: accesses addresses WL_BASE_ADDR, BASE+64, BASE+128, ...
 * After reaching end of region, wraps around.
 * Excellent spatial locality; easy for hardware prefetcher.
 * Hit rate should climb as cache grows -- shows working-set inflection.
 */
uint64_t *workload_sequential(size_t n);

/**
 * Random: uniform random addresses within [WL_BASE_ADDR, BASE+REGION).
 * Aligned to WL_LINE_SIZE boundaries.
 * Worst case for any replacement policy -- no temporal or spatial locality.
 */
uint64_t *workload_random(size_t n, unsigned int seed);



/* ------------------------------------------------------------------ */
/* Workload descriptor (for the benchmark engine)                      */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;           /* "Sequential", "Random", etc.   */
    uint64_t   *addrs;          /* malloc'd address array          */
    size_t      n;              /* number of accesses              */
} Workload;

/** Build 2 workloads (Sequential, Random). Returns array of 2 Workload structs.
 *  Caller must call workload_free_all() when done. */
Workload *workload_build_all(size_t n_accesses, unsigned int seed);

/** Free a workload array previously returned by workload_build_all(). */
void workload_free_all(Workload *wl, int count);

#endif /* WORKLOAD_H */
