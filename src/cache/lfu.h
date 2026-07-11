/*
 * lfu.h — LFU (Least Frequently Used) Cache Replacement Policy
 *
 * Implementation: Entry array + Hash Map + frequency-based eviction
 *   - Hash map      → O(1) lookup (is address in cache? → entry index)
 *   - Entry array   → stores {address, freq, last_access_time} per slot
 *   - Eviction      → O(N) linear scan to find minimum frequency
 *                     (tie-break: smallest last_access_time = LRU order)
 *
 * LFU weakness (important for interviews):
 *   Frequency counters NEVER reset → old popular data stays forever.
 *   A once-hot item that's now cold will never get evicted (cache pollution).
 *   Solution: aging (decay counts periodically) — not implemented here.
 *
 * CPU Cache Replacement Simulator
 */

#ifndef LFU_H
#define LFU_H

#include <stdint.h>

#ifndef CACHE_HIT
#define CACHE_HIT  1
#define CACHE_MISS 0
#endif

/* ─── One cache entry ────────────────────────────────────────────── */
typedef struct {
    uint64_t address;
    uint64_t freq;           /* Access count since insertion          */
    uint64_t last_access;   /* Monotonic timestamp for LRU tie-break  */
    int      valid;          /* 1 = occupied, 0 = empty slot           */
} LFUEntry;

/* ─── Hash map entry (chaining) ──────────────────────────────────── */
typedef struct LFUHashEntry {
    uint64_t          address;
    int               entry_idx;   /* Index into LFUCache.entries[]   */
    struct LFUHashEntry *next;
} LFUHashEntry;

/* ─── LFU cache struct ───────────────────────────────────────────── */
typedef struct {
    int       capacity;
    int       size;

    LFUEntry     *entries;    /* Array of capacity slots              */
    LFUHashEntry **table;     /* Hash map: address → entry index      */
    int            table_size;

    uint64_t access_clock;   /* Incremented on every access          */

    /* Statistics */
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} LFUCache;

/* ─── Public API ─────────────────────────────────────────────────── */

/** Create an LFU cache with the given capacity. Returns NULL on failure. */
LFUCache *lfu_create(int capacity);

/**
 * Access the cache with the given address.
 * Returns CACHE_HIT (1) or CACHE_MISS (0).
 * On hit:  freq++ for that entry.
 * On miss: evict min-frequency entry (LRU tie-break), insert new with freq=1.
 */
int lfu_access(LFUCache *cache, uint64_t address);

/** Print hit/miss/eviction statistics. */
void lfu_print_stats(const LFUCache *cache);

/** Print current cache contents sorted by frequency. */
void lfu_print(const LFUCache *cache);

/** Free all memory owned by the cache. */
void lfu_destroy(LFUCache *cache);

#endif /* LFU_H */
