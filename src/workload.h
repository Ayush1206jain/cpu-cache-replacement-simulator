/*
 * workload.h -- Synthetic Workload Generators for Cache Benchmarking
 *
 * Four access-pattern generators, each producing a flat array of
 * uint64_t memory addresses that can be fed directly into set_cache_access().
 *
 *  sequential() -- linear scan of a memory region (tests spatial locality)
 *  random_wl()  -- uniform random across the address space (worst case)
 *  zipfian()    -- power-law distribution (models real-world hot/cold data)
 *  mixed()      -- 60% sequential + 20% random + 20% zipfian (realistic)
 *
 * Caller owns the returned array and must free() it.
 *
 * Day 11 -- CPU Cache Replacement Simulator
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

/**
 * Zipfian (power-law): a small "hot set" of addresses gets most accesses.
 * 80/20 rule: 20% of addresses receive 80% of accesses.
 * Models: database buffer pool, web cache, OS page cache.
 *
 * Generated using the inverse-CDF method:
 *   P(rank k) ∝ 1/k^alpha   (alpha = 1.0 is standard Zipf)
 */
uint64_t *workload_zipfian(size_t n, unsigned int seed, double alpha);

/**
 * Mixed: 60% sequential + 20% random + 20% zipfian, interleaved.
 * Models a realistic application with hot loops, cold lookups, random I/O.
 */
uint64_t *workload_mixed(size_t n, unsigned int seed);

/* ------------------------------------------------------------------ */
/* Workload descriptor (for the benchmark engine)                      */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;           /* "Sequential", "Random", etc.   */
    uint64_t   *addrs;          /* malloc'd address array          */
    size_t      n;              /* number of accesses              */
} Workload;

/** Build all 4 workloads. Returns array of 4 Workload structs.
 *  Caller must call workload_free_all() when done. */
Workload *workload_build_all(size_t n_accesses, unsigned int seed);

/** Free a workload array previously returned by workload_build_all(). */
void workload_free_all(Workload *wl, int count);

#endif /* WORKLOAD_H */
