/*
 * cache.c — Unified Cache Interface Implementation
 *
 * Wires together LRU, FIFO, and LFU through a common function-pointer vtable.
 * Adding Day 6's PLRU will only require:
 *   1. #include "plru.h"
 *   2. A new case in cache_create()
 *
 * Day 5 — CPU Cache Replacement Simulator
 */

#include "cache.h"
#include "lru.h"
#include "fifo.h"
#include "lfu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─── Type-erasing wrappers (void* → concrete type) ─────────────── */

/* LRU wrappers */
static int  lru_access_w(void *impl, uint64_t addr) { return lru_access((LRUCache *)impl, addr); }
static void lru_stats_w (const void *impl)           { lru_print_stats((const LRUCache *)impl); }
static void lru_print_w (const void *impl)           { lru_print((const LRUCache *)impl); }
static void lru_destroy_w(void *impl)                { lru_destroy((LRUCache *)impl); }

/* FIFO wrappers */
static int  fifo_access_w(void *impl, uint64_t addr) { return fifo_access((FIFOCache *)impl, addr); }
static void fifo_stats_w (const void *impl)           { fifo_print_stats((const FIFOCache *)impl); }
static void fifo_print_w (const void *impl)           { fifo_print((const FIFOCache *)impl); }
static void fifo_destroy_w(void *impl)                { fifo_destroy((FIFOCache *)impl); }

/* LFU wrappers */
static int  lfu_access_w(void *impl, uint64_t addr) { return lfu_access((LFUCache *)impl, addr); }
static void lfu_stats_w (const void *impl)           { lfu_print_stats((const LFUCache *)impl); }
static void lfu_print_w (const void *impl)           { lfu_print((const LFUCache *)impl); }
static void lfu_destroy_w(void *impl)                { lfu_destroy((LFUCache *)impl); }

/* ─── Name → policy mapping ──────────────────────────────────────── */
CachePolicy cache_policy_from_name(const char *name)
{
    if (!name) return POLICY_COUNT;

    /* Case-insensitive compare */
    char upper[16] = {0};
    for (int i = 0; i < 15 && name[i]; i++)
        upper[i] = (char)toupper((unsigned char)name[i]);

    if (strcmp(upper, "LRU")  == 0) return POLICY_LRU;
    if (strcmp(upper, "FIFO") == 0) return POLICY_FIFO;
    if (strcmp(upper, "LFU")  == 0) return POLICY_LFU;

    return POLICY_COUNT;  /* Unknown */
}

/* ─── Factory ────────────────────────────────────────────────────── */
Cache *cache_create(int capacity, const char *policy_name)
{
    CachePolicy policy = cache_policy_from_name(policy_name);
    if (policy == POLICY_COUNT) {
        fprintf(stderr, "[cache] Unknown policy: '%s'. Use LRU, FIFO, or LFU.\n",
                policy_name ? policy_name : "(null)");
        return NULL;
    }

    Cache *c = (Cache *)calloc(1, sizeof(Cache));
    if (!c) return NULL;

    c->policy = policy;

    switch (policy) {
        case POLICY_LRU:
            c->policy_name  = "LRU";
            c->impl         = lru_create(capacity);
            c->fn_access     = lru_access_w;
            c->fn_print_stats = lru_stats_w;
            c->fn_print      = lru_print_w;
            c->fn_destroy    = lru_destroy_w;
            break;

        case POLICY_FIFO:
            c->policy_name  = "FIFO";
            c->impl         = fifo_create(capacity);
            c->fn_access     = fifo_access_w;
            c->fn_print_stats = fifo_stats_w;
            c->fn_print      = fifo_print_w;
            c->fn_destroy    = fifo_destroy_w;
            break;

        case POLICY_LFU:
            c->policy_name  = "LFU";
            c->impl         = lfu_create(capacity);
            c->fn_access     = lfu_access_w;
            c->fn_print_stats = lfu_stats_w;
            c->fn_print      = lfu_print_w;
            c->fn_destroy    = lfu_destroy_w;
            break;

        default:
            free(c);
            return NULL;
    }

    if (!c->impl) {
        fprintf(stderr, "[cache] Failed to allocate %s cache (capacity=%d)\n",
                policy_name, capacity);
        free(c);
        return NULL;
    }

    return c;
}

/* ─── Dispatch calls ─────────────────────────────────────────────── */

int cache_access(Cache *c, uint64_t address)
{
    return c->fn_access(c->impl, address);
}

void cache_print_stats(const Cache *c)
{
    c->fn_print_stats(c->impl);
}

void cache_print(const Cache *c)
{
    c->fn_print(c->impl);
}

void cache_destroy(Cache *c)
{
    if (!c) return;
    c->fn_destroy(c->impl);
    free(c);
}
