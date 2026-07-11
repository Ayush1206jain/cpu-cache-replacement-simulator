/*
 * cache.h — Unified Cache Interface
 *
 * Provides a single polymorphic API over all replacement policies:
 *   LRU  (Least Recently Used)
 *   FIFO (First In, First Out)
 *   LFU  (Least Frequently Used)
 *
 * Usage:
 *   Cache *c = cache_create(64, "LRU");
 *   cache_access(c, 0xDEADBEEF);
 *   cache_print_stats(c);
 *   cache_destroy(c);
 *
 * Polymorphism is achieved via C function pointers (vtable pattern).
 * Adding a new policy requires only implementing its own module and
 * registering it in cache_create() — no changes to call sites.
 *
 * CPU Cache Replacement Simulator
 */

#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>

/* ─── Policy enumeration ─────────────────────────────────────────── */
typedef enum {
    POLICY_LRU  = 0,
    POLICY_FIFO = 1,
    POLICY_LFU  = 2,
    POLICY_COUNT         /* Sentinel — keep last */
} CachePolicy;

/* ─── Unified cache handle (vtable pattern) ──────────────────────── */
typedef struct Cache {
    CachePolicy  policy;
    const char  *policy_name;
    void        *impl;            /* Opaque pointer to concrete cache  */

    /* Function pointers — filled in by cache_create() */
    int  (*fn_access)     (void *impl, uint64_t address);
    void (*fn_print_stats)(const void *impl);
    void (*fn_print)      (const void *impl);
    void (*fn_destroy)    (void *impl);
} Cache;

/* ─── Public API ─────────────────────────────────────────────────── */

/**
 * Create a cache with the given capacity and policy.
 * policy_name: "LRU", "FIFO", or "LFU" (case-insensitive)
 * Returns NULL if policy_name is unrecognized or allocation fails.
 */
Cache *cache_create(int capacity, const char *policy_name);

/** Access address. Returns CACHE_HIT (1) or CACHE_MISS (0). */
int cache_access(Cache *c, uint64_t address);

/** Print per-policy statistics (hits, misses, hit rate). */
void cache_print_stats(const Cache *c);

/** Print current cache contents in policy-specific order. */
void cache_print(const Cache *c);

/** Return the policy enum for a given name string. */
CachePolicy cache_policy_from_name(const char *name);

/** Free all memory for this cache. */
void cache_destroy(Cache *c);

#endif /* CACHE_H */
