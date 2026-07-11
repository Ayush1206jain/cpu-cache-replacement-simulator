/*
 * main.c — CPU Cache Replacement Simulator
 *
 * Day 5: Demonstrates LRU, FIFO, and LFU policies side-by-side.
 * Uses the unified Cache interface introduced today.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o simulator \
 *       src/main.c src/cache/lru.c src/cache/fifo.c \
 *       src/cache/lfu.c src/cache/cache.c -Isrc/cache
 * Run:
 *   ./simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "cache.h"

/* ─── Helper: run a named access sequence on a cache ─────────────── */
static void run_sequence(Cache *c, const char **labels,
                         const uint64_t *addrs, int n)
{
    printf("\n%-6s | %-8s | %-12s | %s\n",
           "Step", "Addr", "Result", "Policy");
    printf("──────────────────────────────────────\n");

    for (int i = 0; i < n; i++) {
        int r = cache_access(c, addrs[i]);
        printf("%-6d | %-8s | %-12s | %s\n",
               i + 1,
               labels[i],
               r == CACHE_HIT ? "HIT  ✓" : "MISS ✗",
               c->policy_name);
    }
}

/* ─── Demo 1: Same sequence across all 3 policies ─────────────────── */
static void demo_policy_comparison(void)
{
    printf("\n╔═══════════════════════════════════════════════════════╗\n");
    printf("║  Demo 1: LRU vs FIFO vs LFU on [A,B,C,A,D,B] cap=3  ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    const char    *labels[] = {"A","B","C","A","D","B"};
    const uint64_t addrs[]  = { 1,  2,  3,  1,  4,  2};
    int n = 6;

    const char *policies[] = {"LRU", "FIFO", "LFU"};

    printf("\n%-8s | %-5s | %-5s | %s\n",
           "Policy", "Hits", "Miss", "Hit Rate");
    printf("──────────────────────────────────\n");

    for (int p = 0; p < 3; p++) {
        Cache *c = cache_create(3, policies[p]);

        int hits = 0, total = n;
        for (int i = 0; i < n; i++)
            if (cache_access(c, addrs[i]) == CACHE_HIT) hits++;

        printf("%-8s | %-5d | %-5d | %.2f%%\n",
               policies[p], hits, total - hits,
               100.0 * hits / total);
        cache_destroy(c);
    }

    /* Now print step-by-step for each policy */
    for (int p = 0; p < 3; p++) {
        Cache *c = cache_create(3, policies[p]);
        printf("\n── %s ────────────────────────────────", policies[p]);
        run_sequence(c, labels, addrs, n);
        cache_print_stats(c);
        cache_destroy(c);
    }
}

/* ─── Demo 2: LFU cache pollution ─────────────────────────────────── */
static void demo_lfu_pollution(void)
{
    printf("\n╔═══════════════════════════════════════════════════════╗\n");
    printf("║  Demo 2: LFU Cache Pollution                          ║\n");
    printf("║  Hot item A (freq=10) blocks new items from cache     ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n\n");

    Cache *lru = cache_create(3, "LRU");
    Cache *lfu = cache_create(3, "LFU");

    /* Phase 1: make A hot in both caches */
    printf("Phase 1: Access A ten times (make it 'hot')\n");
    for (int i = 0; i < 10; i++) {
        cache_access(lru, 0xA);
        cache_access(lfu, 0xA);
    }

    /* Phase 2: fill with B and C */
    printf("Phase 2: Load B and C\n");
    cache_access(lru, 0xB); cache_access(lfu, 0xB);
    cache_access(lru, 0xC); cache_access(lfu, 0xC);

    /* Phase 3: now A is "cold" — access D, E, F */
    printf("Phase 3: Access D, E, F (A is no longer needed)\n\n");

    const uint64_t new_data[] = {0xD, 0xE, 0xF};
    const char    *nd_labels[] = {"D", "E", "F"};

    printf("%-6s | %-4s %-8s | %-4s %-8s\n",
           "Addr", "LRU", "Result", "LFU", "Result");
    printf("──────────────────────────────────────────────\n");

    for (int i = 0; i < 3; i++) {
        int rl = cache_access(lru, new_data[i]);
        int rf = cache_access(lfu, new_data[i]);
        printf("  %-4s | %-4s %-8s | %-4s %-8s\n",
               nd_labels[i],
               "LRU:", rl == CACHE_HIT ? "HIT" : "MISS",
               "LFU:", rf == CACHE_HIT ? "HIT" : "MISS");
    }

    printf("\nFinal cache contents:\n");
    printf("LRU → "); cache_print(lru);
    printf("LFU → "); cache_print(lfu);

    printf("\nStats:\n");
    cache_print_stats(lru);
    cache_print_stats(lfu);

    cache_destroy(lru);
    cache_destroy(lfu);
}

/* ─── Demo 3: FIFO weakness — ignores recency ─────────────────────── */
static void demo_fifo_vs_lru_recency(void)
{
    printf("\n╔═══════════════════════════════════════════════════════╗\n");
    printf("║  Demo 3: FIFO Weakness — Ignores Recency              ║\n");
    printf("║  A is hit just before FIFO evicts it anyway           ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n\n");

    Cache *lru  = cache_create(3, "LRU");
    Cache *fifo = cache_create(3, "FIFO");

    /* Step 1: fill cache A,B,C */
    cache_access(lru,  1); cache_access(fifo, 1);
    cache_access(lru,  2); cache_access(fifo, 2);
    cache_access(lru,  3); cache_access(fifo, 3);

    /* Step 2: access A again (touch it to show recency) */
    cache_access(lru,  1); cache_access(fifo, 1);
    printf("After touching A again:\n");
    printf("LRU:  A is now MRU → safe from eviction\n");
    printf("FIFO: A is still oldest → will be evicted next!\n\n");

    /* Step 3: insert D — who gets evicted? */
    int rl = cache_access(lru,  4);   /* LRU evicts B (LRU entry)   */
    int rf = cache_access(fifo, 4);   /* FIFO evicts A (oldest load) */
    printf("Insert D:\n");
    printf("  LRU  evicts → B (actual LRU)  | D result: %s\n",
           rl == CACHE_HIT ? "HIT" : "MISS");
    printf("  FIFO evicts → A (oldest load) | D result: %s\n",
           rf == CACHE_HIT ? "HIT" : "MISS");

    /* Step 4: access A — was it kept? */
    int lru_a  = cache_access(lru,  1);
    int fifo_a = cache_access(fifo, 1);
    printf("\nAccess A after D inserted:\n");
    printf("  LRU:  A → %s (A was promoted, still in cache)\n",
           lru_a  == CACHE_HIT ? "HIT  ✓" : "MISS ✗");
    printf("  FIFO: A → %s (A was evicted despite recent access!)\n",
           fifo_a == CACHE_HIT ? "HIT  ✓" : "MISS ✗");

    printf("\nStats:\n");
    cache_print_stats(lru);
    cache_print_stats(fifo);

    cache_destroy(lru);
    cache_destroy(fifo);
}

/* ─── Main ─────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     CPU Cache Replacement Simulator — Day 5              ║\n");
    printf("║     LRU · FIFO · LFU with Unified Interface             ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");

    demo_policy_comparison();
    demo_lfu_pollution();
    demo_fifo_vs_lru_recency();

    return EXIT_SUCCESS;
}
