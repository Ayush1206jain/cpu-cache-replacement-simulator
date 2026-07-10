/*
 * lru.c — LRU (Least Recently Used) Cache Replacement Policy
 *
 * Data structure: Doubly Linked List + Hash Map
 *
 *   Hash Map  →  O(1) lookup (address → node)
 *   Linked List →  O(1) move-to-front on hit, O(1) evict-tail on miss
 *
 *  List layout (after an access to address A):
 *
 *    HEAD <-> [A (most recent)] <-> [...] <-> [X (least recent)] <-> TAIL
 *
 *   On HIT:  find node via hash map, unlink it, re-insert at head.
 *   On MISS: if full → evict tail node; create new node at head.
 *
 * Day 4 — CPU Cache Replacement Simulator
 */

#include "lru.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Internal constants ─────────────────────────────────────────── */

/* Hash table size — prime slightly larger than expected capacity.     */
/* Using a prime reduces clustering for the simple modulo hash.        */
#define DEFAULT_TABLE_SIZE 1031   /* ~1024, prime */

/* ─── Internal helpers ───────────────────────────────────────────── */

/**
 * Simple multiplicative hash for 64-bit addresses.
 * Knuth's multiplicative constant: 2^64 / φ (golden ratio).
 */
static inline int hash_addr(uint64_t address, int table_size)
{
    uint64_t key = address * 11400714819323198485ULL;
    return (int)(key % (uint64_t)table_size);
}

/* ─── Doubly linked list operations ─────────────────────────────── */

/** Unlink node from wherever it sits in the list (does NOT free it). */
static void list_unlink(LRUCache *c, LRUNode *node)
{
    if (node->prev) node->prev->next = node->next;
    else            c->head          = node->next;   /* was head */

    if (node->next) node->next->prev = node->prev;
    else            c->tail          = node->prev;   /* was tail */

    node->prev = node->next = NULL;
}

/** Insert node at the HEAD of the list (most-recently-used position). */
static void list_push_front(LRUCache *c, LRUNode *node)
{
    node->prev = NULL;
    node->next = c->head;

    if (c->head) c->head->prev = node;
    c->head = node;

    if (!c->tail) c->tail = node;   /* first insertion */
}

/* ─── Hash map operations ────────────────────────────────────────── */

/** Look up an address in the hash map. Returns NULL if not present. */
static HashEntry *hashmap_find(LRUCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    HashEntry *entry = c->table[idx];
    while (entry) {
        if (entry->address == address) return entry;
        entry = entry->next;
    }
    return NULL;
}

/**
 * Insert a new (address → node) mapping into the hash map.
 * Caller must guarantee the address is NOT already present.
 */
static void hashmap_insert(LRUCache *c, uint64_t address, LRUNode *node)
{
    int idx = hash_addr(address, c->table_size);

    HashEntry *entry = (HashEntry *)malloc(sizeof(HashEntry));
    if (!entry) {
        fprintf(stderr, "[LRU] hashmap_insert: malloc failed\n");
        exit(EXIT_FAILURE);
    }
    entry->address = address;
    entry->node    = node;
    entry->next    = c->table[idx];
    c->table[idx]  = entry;
}

/**
 * Remove the mapping for address from the hash map.
 * Does NOT free the LRUNode — caller is responsible.
 */
static void hashmap_remove(LRUCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    HashEntry *entry = c->table[idx];
    HashEntry *prev  = NULL;

    while (entry) {
        if (entry->address == address) {
            if (prev) prev->next      = entry->next;
            else      c->table[idx]   = entry->next;
            free(entry);
            return;
        }
        prev  = entry;
        entry = entry->next;
    }
    /* address not found — should never happen in correct usage */
    fprintf(stderr, "[LRU] hashmap_remove: address 0x%llx not found\n",
            (unsigned long long)address);
}

/* ─── Public API implementation ──────────────────────────────────── */

LRUCache *lru_create(int capacity)
{
    if (capacity <= 0) {
        fprintf(stderr, "[LRU] lru_create: capacity must be > 0\n");
        return NULL;
    }

    LRUCache *cache = (LRUCache *)calloc(1, sizeof(LRUCache));
    if (!cache) return NULL;

    cache->capacity   = capacity;
    cache->size       = 0;
    cache->head       = NULL;
    cache->tail       = NULL;
    cache->hits       = 0;
    cache->misses     = 0;
    cache->evictions  = 0;

    /* Choose table size: smallest prime > 2 * capacity */
    cache->table_size = DEFAULT_TABLE_SIZE;
    if (capacity * 2 >= DEFAULT_TABLE_SIZE) {
        /* Grow table for very large caches — simple heuristic */
        cache->table_size = capacity * 2 + 3;
    }

    cache->table = (HashEntry **)calloc((size_t)cache->table_size,
                                        sizeof(HashEntry *));
    if (!cache->table) {
        free(cache);
        return NULL;
    }

    return cache;
}

int lru_access(LRUCache *cache, uint64_t address)
{
    /* ── HIT path ── */
    HashEntry *entry = hashmap_find(cache, address);
    if (entry) {
        /* Move the accessed node to the MRU position (front of list) */
        list_unlink(cache, entry->node);
        list_push_front(cache, entry->node);
        cache->hits++;
        return CACHE_HIT;
    }

    /* ── MISS path ── */
    cache->misses++;

    if (cache->size == cache->capacity) {
        /* Evict the LRU node (tail of the list) */
        LRUNode *victim = cache->tail;
        hashmap_remove(cache, victim->address);
        list_unlink(cache, victim);
        free(victim);
        cache->size--;
        cache->evictions++;
    }

    /* Load new address into the cache at MRU position */
    LRUNode *node = (LRUNode *)malloc(sizeof(LRUNode));
    if (!node) {
        fprintf(stderr, "[LRU] lru_access: malloc failed for new node\n");
        exit(EXIT_FAILURE);
    }
    node->address = address;
    node->prev    = NULL;
    node->next    = NULL;

    list_push_front(cache, node);
    hashmap_insert(cache, address, node);
    cache->size++;

    return CACHE_MISS;
}

void lru_print(const LRUCache *cache)
{
    printf("LRU Cache (size=%d / capacity=%d) [MRU → LRU]:\n",
           cache->size, cache->capacity);

    LRUNode *cur = cache->head;
    int pos = 0;
    while (cur) {
        printf("  [%d] 0x%016llx\n", pos++,
               (unsigned long long)cur->address);
        cur = cur->next;
    }
    if (pos == 0) printf("  (empty)\n");
}

void lru_print_stats(const LRUCache *cache)
{
    uint64_t total = cache->hits + cache->misses;
    double   hr    = (total > 0)
                     ? (100.0 * (double)cache->hits / (double)total)
                     : 0.0;

    printf("──────────────────────────────────\n");
    printf("LRU Cache Statistics\n");
    printf("──────────────────────────────────\n");
    printf("  Capacity  : %d\n",      cache->capacity);
    printf("  Accesses  : %llu\n",    (unsigned long long)total);
    printf("  Hits      : %llu\n",    (unsigned long long)cache->hits);
    printf("  Misses    : %llu\n",    (unsigned long long)cache->misses);
    printf("  Evictions : %llu\n",    (unsigned long long)cache->evictions);
    printf("  Hit Rate  : %.2f%%\n",  hr);
    printf("──────────────────────────────────\n");
}

void lru_destroy(LRUCache *cache)
{
    if (!cache) return;

    /* Free linked list nodes */
    LRUNode *cur = cache->head;
    while (cur) {
        LRUNode *next = cur->next;
        free(cur);
        cur = next;
    }

    /* Free hash table entries */
    for (int i = 0; i < cache->table_size; i++) {
        HashEntry *entry = cache->table[i];
        while (entry) {
            HashEntry *nxt = entry->next;
            free(entry);
            entry = nxt;
        }
    }

    free(cache->table);
    free(cache);
}
