/*
 * lru.h — LRU (Least Recently Used) Cache Replacement Policy
 *
 * Implementation: Doubly Linked List + Hash Map
 *   - O(1) access (hit moves node to front)
 *   - O(1) eviction (remove from tail)
 *
 * Day 4 of CPU Cache Replacement Simulator project.
 */

#ifndef LRU_H
#define LRU_H

#include <stdint.h>
#include <stddef.h>

/* ─── Result codes ─────────────────────────────────────────────── */
#define CACHE_HIT  1
#define CACHE_MISS 0

/* ─── Doubly linked list node ──────────────────────────────────── */
typedef struct LRUNode {
    uint64_t        address;   /* Memory address (the cache "key") */
    struct LRUNode *prev;
    struct LRUNode *next;
} LRUNode;

/* ─── Hash map bucket (chaining) ──────────────────────────────── */
typedef struct HashEntry {
    uint64_t         address;
    LRUNode         *node;     /* Points into the linked list       */
    struct HashEntry *next;    /* Collision chain                   */
} HashEntry;

/* ─── Main LRU cache struct ────────────────────────────────────── */
typedef struct {
    int       capacity;        /* Max number of entries            */
    int       size;            /* Current number of entries        */

    /* Doubly linked list: head = most recent, tail = least recent */
    LRUNode  *head;
    LRUNode  *tail;

    /* Hash map for O(1) lookup */
    HashEntry **table;
    int         table_size;   /* Number of hash buckets            */

    /* Statistics */
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} LRUCache;

/* ─── Public API ───────────────────────────────────────────────── */

/**
 * Create an LRU cache with the given capacity.
 * Returns NULL on allocation failure.
 */
LRUCache *lru_create(int capacity);

/**
 * Access the cache with the given memory address.
 * Returns CACHE_HIT (1) or CACHE_MISS (0).
 * On miss, the address is loaded; if full, the LRU entry is evicted first.
 */
int lru_access(LRUCache *cache, uint64_t address);

/**
 * Print the current cache contents (MRU → LRU order).
 */
void lru_print(const LRUCache *cache);

/**
 * Print hit/miss/eviction statistics.
 */
void lru_print_stats(const LRUCache *cache);

/**
 * Free all memory owned by the cache.
 */
void lru_destroy(LRUCache *cache);

#endif /* LRU_H */
