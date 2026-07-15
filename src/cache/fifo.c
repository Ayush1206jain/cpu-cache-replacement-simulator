/*
 * fifo.c — FIFO (First In, First Out) Cache Replacement Policy
 *
 * Data structures:
 *   Circular queue  → uint64_t queue[capacity], head & tail indices
 *   Hash set        → chaining hash table for O(1) membership check
 *
 * Queue layout:
 *   head → [oldest] [  ...  ] [newest] ← tail (next insert)
 *
 *   On MISS (not full): queue[tail] = addr; tail = (tail+1) % capacity
 *   On MISS (full):     evict queue[head]; head = (head+1) % capacity
 *   On HIT:             nothing changes — FIFO ignores recency
 *
 * CPU Cache Replacement Simulator
 */

#include "fifo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Internal constants ─────────────────────────────────────────── */
#define DEFAULT_TABLE_SIZE 1031

/* ─── Hash function ──────────────────────────────────────────────── */
static inline int hash_addr(uint64_t address, int table_size)
{
    uint64_t key = address * 11400714819323198485ULL;
    return (int)(key % (uint64_t)table_size);
}

/* ─── Hash set operations ────────────────────────────────────────── */

static int hashset_contains(FIFOCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    FIFOHashEntry *e = c->table[idx];
    while (e) {
        if (e->address == address) return 1;
        e = e->next;
    }
    return 0;
}

static void hashset_insert(FIFOCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    FIFOHashEntry *e = (FIFOHashEntry *)malloc(sizeof(FIFOHashEntry));
    if (!e) { fprintf(stderr, "[FIFO] malloc failed\n"); exit(EXIT_FAILURE); }
    e->address  = address;
    e->next     = c->table[idx];
    c->table[idx] = e;
}

static void hashset_remove(FIFOCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    FIFOHashEntry *e    = c->table[idx];
    FIFOHashEntry *prev = NULL;
    while (e) {
        if (e->address == address) {
            if (prev) prev->next    = e->next;
            else      c->table[idx] = e->next;
            free(e);
            return;
        }
        prev = e;
        e    = e->next;
    }
}

/* ─── Public API ─────────────────────────────────────────────────── */

FIFOCache *fifo_create(int capacity)
{
    if (capacity <= 0) {
        fprintf(stderr, "[FIFO] capacity must be > 0\n");
        return NULL;
    }

    FIFOCache *cache = (FIFOCache *)calloc(1, sizeof(FIFOCache));
    if (!cache) return NULL;

    cache->capacity   = capacity;
    cache->size       = 0;
    cache->head       = 0;
    cache->tail       = 0;
    cache->hits       = 0;
    cache->misses     = 0;
    cache->evictions  = 0;

    cache->queue = (uint64_t *)malloc((size_t)capacity * sizeof(uint64_t));
    if (!cache->queue) { free(cache); return NULL; }

    cache->table_size = (capacity * 2 >= DEFAULT_TABLE_SIZE)
                        ? capacity * 2 + 3
                        : DEFAULT_TABLE_SIZE;

    cache->table = (FIFOHashEntry **)calloc((size_t)cache->table_size,
                                             sizeof(FIFOHashEntry *));
    if (!cache->table) { free(cache->queue); free(cache); return NULL; }

    return cache;
}

int fifo_access(FIFOCache *cache, uint64_t address)
{
    /* ── HIT: address already in cache — FIFO does nothing ── */
    if (hashset_contains(cache, address)) {
        cache->hits++;
        return CACHE_HIT;
    }

    /* ── MISS ── */
    cache->misses++;

    if (cache->size == cache->capacity) {
        /* Evict the oldest entry (head of circular queue) */
        uint64_t victim = cache->queue[cache->head];
        hashset_remove(cache, victim);
        cache->head = (cache->head + 1) % cache->capacity;
        cache->size--;
        cache->evictions++;
    }

    /* Insert new address at tail */
    cache->queue[cache->tail] = address;
    cache->tail = (cache->tail + 1) % cache->capacity;
    hashset_insert(cache, address);
    cache->size++;

    return CACHE_MISS;
}

void fifo_print(const FIFOCache *cache)
{
    printf("FIFO Cache (size=%d / capacity=%d) [oldest -> newest]:\n",
           cache->size, cache->capacity);
    for (int i = 0; i < cache->size; i++) {
        int idx = (cache->head + i) % cache->capacity;
        printf("  [%d] 0x%016llx%s\n", i,
               (unsigned long long)cache->queue[idx],
               (i == 0) ? "  ← next evict" : "");
    }
    if (cache->size == 0) printf("  (empty)\n");
}

void fifo_print_stats(const FIFOCache *cache)
{
    uint64_t total = cache->hits + cache->misses;
    double   hr    = (total > 0)
                     ? (100.0 * (double)cache->hits / (double)total)
                     : 0.0;

    printf("----------------------------------\n");
    printf("FIFO Cache Statistics\n");
    printf("----------------------------------\n");
    printf("  Capacity  : %d\n",      cache->capacity);
    printf("  Accesses  : %llu\n",    (unsigned long long)total);
    printf("  Hits      : %llu\n",    (unsigned long long)cache->hits);
    printf("  Misses    : %llu\n",    (unsigned long long)cache->misses);
    printf("  Evictions : %llu\n",    (unsigned long long)cache->evictions);
    printf("  Hit Rate  : %.2f%%\n",  hr);
    printf("----------------------------------\n");
}

void fifo_destroy(FIFOCache *cache)
{
    if (!cache) return;

    free(cache->queue);

    for (int i = 0; i < cache->table_size; i++) {
        FIFOHashEntry *e = cache->table[i];
        while (e) {
            FIFOHashEntry *nxt = e->next;
            free(e);
            e = nxt;
        }
    }
    free(cache->table);
    free(cache);
}
