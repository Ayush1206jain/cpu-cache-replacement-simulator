/*
 * main.c — Entry point for CPU Cache Replacement Simulator
 *
 * Day 4: Demonstrates LRU cache with the plan's test sequence.
 * Future days will add FIFO, LFU, PLRU, and trace-driven simulation.
 *
 * Build:
 *   gcc -Wall -Wextra -o simulator src/main.c src/cache/lru.c -I src/cache
 * Run:
 *   ./simulator
 *
 * Day 4 — CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "lru.h"

/* ─── Demo: Plan's test sequence ──────────────────────────────────── */
static void demo_lru_plan_sequence(void)
{
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║  LRU Demo — Plan sequence [A,B,C,A,D,B] ║\n");
    printf("║  Capacity: 3                              ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    /* Addresses: A=1, B=2, C=3, D=4 for readability */
    struct { uint64_t addr; const char *label; } sequence[] = {
        {1, "A"}, {2, "B"}, {3, "C"},
        {1, "A"}, {4, "D"}, {2, "B"},
    };
    int n = sizeof(sequence) / sizeof(sequence[0]);

    LRUCache *cache = lru_create(3);
    if (!cache) { fprintf(stderr, "Failed to create cache\n"); return; }

    printf("%-8s %-10s %-12s %s\n", "Step", "Address", "Result", "Cache State (MRU→LRU)");
    printf("──────────────────────────────────────────────────────────\n");

    for (int i = 0; i < n; i++) {
        int result = lru_access(cache, sequence[i].addr);

        printf("%-8d %-10s %-12s [",
               i + 1,
               sequence[i].label,
               result == CACHE_HIT ? "HIT ✓" : "MISS ✗");

        /* Print cache state MRU → LRU */
        LRUNode *cur = cache->head;
        int first = 1;
        while (cur) {
            /* Convert address back to letter */
            char lbl = (char)('A' + (int)cur->address - 1);
            if (!first) printf(", ");
            printf("%c", lbl);
            first = 0;
            cur = cur->next;
        }
        printf("]\n");
    }

    printf("\n");
    lru_print_stats(cache);
    lru_destroy(cache);
}

/* ─── Demo: Sequential vs repeated access pattern ─────────────────── */
static void demo_locality(void)
{
    printf("\n╔═════════════════════════════════════════════╗\n");
    printf("║  LRU Demo — Temporal locality effect        ║\n");
    printf("║  Access hot working set repeatedly (cap=4) ║\n");
    printf("╚═════════════════════════════════════════════╝\n\n");

    LRUCache *cache = lru_create(4);
    if (!cache) return;

    uint64_t hot_set[] = {0x100, 0x200, 0x300, 0x400};
    int passes = 5;

    printf("Hot working set: 0x100, 0x200, 0x300, 0x400 (fits in cache)\n");
    printf("Accessing %d times each...\n\n", passes);

    for (int p = 0; p < passes; p++) {
        for (int i = 0; i < 4; i++) {
            int r = lru_access(cache, hot_set[i]);
            printf("  Pass %d | 0x%llx → %s\n",
                   p + 1,
                   (unsigned long long)hot_set[i],
                   r == CACHE_HIT ? "HIT" : "MISS (cold miss, first access)");
        }
    }

    printf("\n");
    lru_print_stats(cache);
    lru_destroy(cache);
}

/* ─── Main ─────────────────────────────────────────────────────────── */
int main(void)
{
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║       CPU Cache Replacement Simulator — Day 4        ║\n");
    printf("║       LRU (Least Recently Used) Implementation       ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");

    demo_lru_plan_sequence();
    demo_locality();

    printf("\nBuild & run tests:\n");
    printf("  gcc -Wall -o test_lru tests/test_lru.c src/cache/lru.c -Isrc/cache\n");
    printf("  ./test_lru\n\n");

    return EXIT_SUCCESS;
}
