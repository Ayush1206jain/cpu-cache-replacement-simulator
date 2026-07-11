/*
 * lfu.c — LFU (Least Frequently Used) Cache Replacement Policy
 *
 * Data structures:
 *   entries[]     → flat array of LFUEntry, one slot per capacity
 *   hash table    → address → index in entries[] (O(1) lookup)
 *   access_clock  → monotonic counter for LRU tie-breaking on equal freq
 *
 * Eviction strategy (finding the victim):
 *   Linear scan over entries[] — O(N). Find the entry with minimum freq.
 *   Among entries with the same minimum freq, pick the one with the
 *   smallest last_access timestamp (i.e., least recently used = LRU).
 *
 *   Why O(N) is acceptable here:
 *     Cache capacity in a simulator is bounded (typically < 1024 entries).
 *     O(N) eviction is simpler, bug-free, and far easier to reason about
 *     than a min-heap with position tracking in C. It also mirrors
 *     how many real embedded controllers implement LFU in firmware.
 *
 * Day 5 — CPU Cache Replacement Simulator
 */

#include "lfu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Hash function (same Knuth multiplicative as lru.c) ─────────── */
static inline int hash_addr(uint64_t address, int table_size)
{
    uint64_t key = address * 11400714819323198485ULL;
    return (int)(key % (uint64_t)table_size);
}

/* ─── Hash map: address → entry index ───────────────────────────── */

static LFUHashEntry *hashmap_find(LFUCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    LFUHashEntry *e = c->table[idx];
    while (e) {
        if (e->address == address) return e;
        e = e->next;
    }
    return NULL;
}

static void hashmap_insert(LFUCache *c, uint64_t address, int entry_idx)
{
    int idx = hash_addr(address, c->table_size);
    LFUHashEntry *e = (LFUHashEntry *)malloc(sizeof(LFUHashEntry));
    if (!e) { fprintf(stderr, "[LFU] malloc failed\n"); exit(EXIT_FAILURE); }
    e->address   = address;
    e->entry_idx = entry_idx;
    e->next      = c->table[idx];
    c->table[idx] = e;
}

static void hashmap_remove(LFUCache *c, uint64_t address)
{
    int idx = hash_addr(address, c->table_size);
    LFUHashEntry *e    = c->table[idx];
    LFUHashEntry *prev = NULL;
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

/* ─── Internal: find the victim entry (min freq, LRU tie-break) ──── */
static int find_victim(LFUCache *c)
{
    int      victim_idx  = -1;
    uint64_t min_freq    = UINT64_MAX;
    uint64_t min_last    = UINT64_MAX;  /* for tie-break */

    for (int i = 0; i < c->capacity; i++) {
        if (!c->entries[i].valid) continue;

        uint64_t f = c->entries[i].freq;
        uint64_t t = c->entries[i].last_access;

        if (f < min_freq || (f == min_freq && t < min_last)) {
            min_freq   = f;
            min_last   = t;
            victim_idx = i;
        }
    }
    return victim_idx;  /* -1 only if cache is empty (should not happen) */
}

/* ─── Internal: find a free slot in entries[] ────────────────────── */
static int find_free_slot(LFUCache *c)
{
    for (int i = 0; i < c->capacity; i++) {
        if (!c->entries[i].valid) return i;
    }
    return -1;  /* should never reach here after eviction */
}

/* ─── Public API ─────────────────────────────────────────────────── */

LFUCache *lfu_create(int capacity)
{
    if (capacity <= 0) {
        fprintf(stderr, "[LFU] capacity must be > 0\n");
        return NULL;
    }

    LFUCache *cache = (LFUCache *)calloc(1, sizeof(LFUCache));
    if (!cache) return NULL;

    cache->capacity     = capacity;
    cache->size         = 0;
    cache->access_clock = 0;
    cache->hits         = 0;
    cache->misses       = 0;
    cache->evictions    = 0;

    cache->entries = (LFUEntry *)calloc((size_t)capacity, sizeof(LFUEntry));
    if (!cache->entries) { free(cache); return NULL; }

    /* table_size: pick a prime bigger than 2 × capacity */
    cache->table_size = (capacity * 2 >= 1031) ? capacity * 2 + 3 : 1031;
    cache->table = (LFUHashEntry **)calloc((size_t)cache->table_size,
                                            sizeof(LFUHashEntry *));
    if (!cache->table) { free(cache->entries); free(cache); return NULL; }

    return cache;
}

int lfu_access(LFUCache *cache, uint64_t address)
{
    cache->access_clock++;

    /* ── HIT path ── */
    LFUHashEntry *he = hashmap_find(cache, address);
    if (he) {
        LFUEntry *e = &cache->entries[he->entry_idx];
        e->freq++;
        e->last_access = cache->access_clock;
        cache->hits++;
        return CACHE_HIT;
    }

    /* ── MISS path ── */
    cache->misses++;

    if (cache->size == cache->capacity) {
        /* Find and evict the least-frequently-used entry */
        int victim_idx = find_victim(cache);
        LFUEntry *v    = &cache->entries[victim_idx];

        hashmap_remove(cache, v->address);
        v->valid = 0;
        cache->size--;
        cache->evictions++;
    }

    /* Insert new entry into a free slot */
    int slot = find_free_slot(cache);
    cache->entries[slot].address     = address;
    cache->entries[slot].freq        = 1;
    cache->entries[slot].last_access = cache->access_clock;
    cache->entries[slot].valid       = 1;

    hashmap_insert(cache, address, slot);
    cache->size++;

    return CACHE_MISS;
}

/* Helper for sorting entries by freq (for display) */
static int cmp_entry_freq(const void *a, const void *b)
{
    const LFUEntry *ea = (const LFUEntry *)a;
    const LFUEntry *eb = (const LFUEntry *)b;
    if (!ea->valid && !eb->valid) return 0;
    if (!ea->valid) return  1;
    if (!eb->valid) return -1;
    if (ea->freq < eb->freq) return -1;
    if (ea->freq > eb->freq) return  1;
    /* tie: least recently used first */
    if (ea->last_access < eb->last_access) return -1;
    return 1;
}

void lfu_print(const LFUCache *cache)
{
    printf("LFU Cache (size=%d / capacity=%d) [lowest freq → highest]:\n",
           cache->size, cache->capacity);

    /* Copy entries for sorted display (don't mutate live array) */
    LFUEntry *copy = (LFUEntry *)malloc((size_t)cache->capacity * sizeof(LFUEntry));
    if (!copy) return;
    memcpy(copy, cache->entries, (size_t)cache->capacity * sizeof(LFUEntry));
    qsort(copy, (size_t)cache->capacity, sizeof(LFUEntry), cmp_entry_freq);

    int shown = 0;
    for (int i = 0; i < cache->capacity && shown < cache->size; i++) {
        if (!copy[i].valid) continue;
        printf("  [%d] 0x%016llx  freq=%llu%s\n",
               shown,
               (unsigned long long)copy[i].address,
               (unsigned long long)copy[i].freq,
               (shown == 0) ? "  ← next evict" : "");
        shown++;
    }
    if (shown == 0) printf("  (empty)\n");
    free(copy);
}

void lfu_print_stats(const LFUCache *cache)
{
    uint64_t total = cache->hits + cache->misses;
    double   hr    = (total > 0)
                     ? (100.0 * (double)cache->hits / (double)total)
                     : 0.0;

    printf("──────────────────────────────────\n");
    printf("LFU Cache Statistics\n");
    printf("──────────────────────────────────\n");
    printf("  Capacity  : %d\n",      cache->capacity);
    printf("  Accesses  : %llu\n",    (unsigned long long)total);
    printf("  Hits      : %llu\n",    (unsigned long long)cache->hits);
    printf("  Misses    : %llu\n",    (unsigned long long)cache->misses);
    printf("  Evictions : %llu\n",    (unsigned long long)cache->evictions);
    printf("  Hit Rate  : %.2f%%\n",  hr);
    printf("──────────────────────────────────\n");
}

void lfu_destroy(LFUCache *cache)
{
    if (!cache) return;

    free(cache->entries);

    for (int i = 0; i < cache->table_size; i++) {
        LFUHashEntry *e = cache->table[i];
        while (e) {
            LFUHashEntry *nxt = e->next;
            free(e);
            e = nxt;
        }
    }
    free(cache->table);
    free(cache);
}
