/*
 * fifo.h — FIFO (First In, First Out) Cache Replacement Policy
 *
 * Implementation: Circular Queue (ring buffer) + Hash Set
 *   - Circular queue  → O(1) enqueue (insert) and dequeue (evict oldest)
 *   - Hash set        → O(1) membership check (is address in cache?)
 *
 * Key property: HIT does NOT change eviction order.
 * The oldest LOADED line is evicted, regardless of how many times it was hit.
 * This is FIFO's fundamental weakness vs LRU.
 *
 *  CPU Cache Replacement Simulator
 */

#ifndef FIFO_H
#define FIFO_H

#include <stdint.h>

#define CACHE_HIT  1
#define CACHE_MISS 0

/* ─── Hash set entry (chaining) ──────────────────────────────────── */
typedef struct FIFOHashEntry {
    uint64_t              address;
    struct FIFOHashEntry *next;
} FIFOHashEntry;

/* ─── FIFO cache struct ───────────────────────────────────────────── */
typedef struct {
    int       capacity;      /* Max entries                           */
    int       size;          /* Current entries                       */

    /* Circular queue — stores insertion order */
    uint64_t *queue;         /* Ring buffer of addresses              */
    int       head;          /* Index of oldest entry (evict here)    */
    int       tail;          /* Index of next free slot (insert here) */

    /* Hash set — O(1) membership check */
    FIFOHashEntry **table;
    int             table_size;

    /* Statistics */
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} FIFOCache;

/* ─── Public API ─────────────────────────────────────────────────── */

/** Create a FIFO cache with the given capacity. Returns NULL on failure. */
FIFOCache *fifo_create(int capacity);

/**
 * Access the cache with the given address.
 * Returns CACHE_HIT (1) or CACHE_MISS (0).
 * On miss: if full, evict the oldest-inserted entry first.
 */
int fifo_access(FIFOCache *cache, uint64_t address);

/** Print hit/miss/eviction statistics. */
void fifo_print_stats(const FIFOCache *cache);

/** Print current cache contents (oldest → newest). */
void fifo_print(const FIFOCache *cache);

/** Free all memory owned by the cache. */
void fifo_destroy(FIFOCache *cache);

#endif /* FIFO_H */
