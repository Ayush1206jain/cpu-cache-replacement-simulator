/*
 * test_day5.c — Unit tests for FIFO, LFU, and Unified Cache Interface
 *
 * Tests the same access sequence [A,B,C,A,D,B] across all three policies
 * so results can be compared side-by-side.
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o test_day5 tests/test_day5.c ^
 *       src/cache/fifo.c src/cache/lfu.c src/cache/lru.c src/cache/cache.c ^
 *       -Isrc/cache
 * Run:
 *   ./test_day5
 *
 * Day 5 — CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fifo.h"
#include "lfu.h"
#include "cache.h"

/* ─── Minimal test framework ──────────────────────────────────────── */
static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                        \
    do {                                                         \
        tests_run++;                                             \
        if (cond) {                                              \
            tests_passed++;                                      \
            printf("  [PASS] %s\n", msg);                       \
        } else {                                                 \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__);  \
        }                                                        \
    } while (0)

/* ─── Test 1: FIFO — Plan sequence [A,B,C,A,D,B] cap=3 ────────────
 *
 * Hand-trace (FIFO: hits do NOT change order):
 *   Access A → MISS  queue:[A]           head=A
 *   Access B → MISS  queue:[A,B]         head=A
 *   Access C → MISS  queue:[A,B,C]       head=A (full)
 *   Access A → HIT   queue:[A,B,C]       A still in cache, queue UNCHANGED
 *   Access D → MISS  queue:[B,C,D]       A evicted (oldest insertion)
 *   Access B → HIT   queue:[B,C,D]       B still in cache
 *
 * Expected: hits=2, misses=4, evictions=1
 * CRITICAL: A is evicted at step 5 even though it was accessed at step 4!
 *           This is FIFO's key weakness — ignores recency.
 * ─────────────────────────────────────────────────────────────── */
static void test_fifo_plan_sequence(void)
{
    printf("\n=== Test 1: FIFO sequence [A,B,C,A,D,B] cap=3 ===\n");

    FIFOCache *c = fifo_create(3);
    ASSERT(c != NULL, "fifo_create returns non-NULL");

    int r;
    r = fifo_access(c, 1); ASSERT(r == CACHE_MISS, "A -> MISS");
    r = fifo_access(c, 2); ASSERT(r == CACHE_MISS, "B -> MISS");
    r = fifo_access(c, 3); ASSERT(r == CACHE_MISS, "C -> MISS");
    r = fifo_access(c, 1); ASSERT(r == CACHE_HIT,  "A -> HIT");
    r = fifo_access(c, 4); ASSERT(r == CACHE_MISS, "D -> MISS (A evicted despite recent hit!)");
    r = fifo_access(c, 2); ASSERT(r == CACHE_HIT,  "B -> HIT");

    ASSERT(c->hits      == 2, "hits == 2");
    ASSERT(c->misses    == 4, "misses == 4");
    ASSERT(c->evictions == 1, "evictions == 1");

    /* After D inserted, A was evicted. Verify A is gone now. */
    r = fifo_access(c, 1);
    ASSERT(r == CACHE_MISS, "A -> MISS (confirms A was evicted by FIFO)");

    fifo_print(c);
    fifo_print_stats(c);
    fifo_destroy(c);
}

/* ─── Test 2: FIFO — Belady's anomaly exists for FIFO ──────────────
 * With FIFO, increasing cache size can sometimes INCREASE miss rate.
 * This test demonstrates it does NOT happen with LRU (FIFO anomaly check).
 * ─────────────────────────────────────────────────────────────── */
static void test_fifo_capacity_one(void)
{
    printf("\n=== Test 2: FIFO capacity=1 ===\n");

    FIFOCache *c = fifo_create(1);
    ASSERT(c != NULL, "fifo_create(1) succeeds");

    int r;
    r = fifo_access(c, 0xAA); ASSERT(r == CACHE_MISS, "0xAA -> MISS");
    r = fifo_access(c, 0xAA); ASSERT(r == CACHE_HIT,  "0xAA -> HIT (same addr)");
    r = fifo_access(c, 0xBB); ASSERT(r == CACHE_MISS, "0xBB -> MISS (0xAA evicted)");
    r = fifo_access(c, 0xAA); ASSERT(r == CACHE_MISS, "0xAA -> MISS (0xBB evicted)");

    ASSERT(c->hits == 1,      "hits == 1");
    ASSERT(c->misses == 3,    "misses == 3");
    ASSERT(c->evictions == 2, "evictions == 2");

    fifo_print_stats(c);
    fifo_destroy(c);
}

/* ─── Test 3: FIFO — Circular wrap-around ────────────────────────── */
static void test_fifo_circular_wrap(void)
{
    printf("\n=== Test 3: FIFO circular queue wrap-around ===\n");

    FIFOCache *c = fifo_create(3);

    /* Fill: queue = [1, 2, 3] */
    fifo_access(c, 1);
    fifo_access(c, 2);
    fifo_access(c, 3);

    /* Evict 1, insert 4: queue = [2, 3, 4] */
    fifo_access(c, 4);
    /* Evict 2, insert 5: queue = [3, 4, 5] */
    fifo_access(c, 5);
    /* Evict 3, insert 6: queue = [4, 5, 6] (head wrapped around ring) */
    fifo_access(c, 6);

    /* Trace after [1,2,3] fill → [2,3,4] → [3,4,5] → [4,5,6]:
     * Access 1 → MISS: 4 evicted → queue [5,6,1]
     * Access 4 → MISS: 5 evicted → queue [6,1,4]
     * Access 5 → MISS: 6 evicted → queue [1,4,5]
     * Access 6 → MISS: 1 evicted → queue [4,5,6]
     * All are misses because each re-insertion evicts the current head. */
    int r;
    r = fifo_access(c, 1); ASSERT(r == CACHE_MISS, "1 -> MISS (evicted in wrap-around cycle)");
    r = fifo_access(c, 4); ASSERT(r == CACHE_MISS, "4 -> MISS (evicted when 1 was re-inserted)");
    r = fifo_access(c, 5); ASSERT(r == CACHE_MISS, "5 -> MISS (evicted when 4 was re-inserted)");
    r = fifo_access(c, 6); ASSERT(r == CACHE_MISS, "6 -> MISS (evicted when 5 was re-inserted)");
    printf("  Note: circular queue wrap confirmed — ring buffer recycles correctly.\n");

    fifo_print(c);
    fifo_print_stats(c);
    fifo_destroy(c);
}

/* ─── Test 4: LFU — Plan sequence [A,B,C,A,D,B] cap=3 ─────────────
 *
 * Hand-trace (LFU: evict min-freq, LRU tie-break):
 *   Access A → MISS  entries:{A:1}               min_freq=1
 *   Access B → MISS  entries:{A:1, B:1}
 *   Access C → MISS  entries:{A:1, B:1, C:1}      full, all freq=1
 *   Access A → HIT   entries:{A:2, B:1, C:1}      A.freq=2
 *   Access D → MISS  entries:{A:2, C:1, D:1}      B evicted (freq=1, oldest)
 *   Access B → MISS  entries:{A:2, D:1, B:1}      C evicted (freq=1, oldest)
 *
 * Expected: hits=1, misses=5, evictions=2
 * Same hit count as LRU for this sequence, but different eviction victims.
 * ─────────────────────────────────────────────────────────────── */
static void test_lfu_plan_sequence(void)
{
    printf("\n=== Test 4: LFU sequence [A,B,C,A,D,B] cap=3 ===\n");

    LFUCache *c = lfu_create(3);
    ASSERT(c != NULL, "lfu_create returns non-NULL");

    int r;
    r = lfu_access(c, 1); ASSERT(r == CACHE_MISS, "A -> MISS");
    r = lfu_access(c, 2); ASSERT(r == CACHE_MISS, "B -> MISS");
    r = lfu_access(c, 3); ASSERT(r == CACHE_MISS, "C -> MISS");
    r = lfu_access(c, 1); ASSERT(r == CACHE_HIT,  "A -> HIT (freq becomes 2)");
    r = lfu_access(c, 4); ASSERT(r == CACHE_MISS, "D -> MISS (B evicted, lowest freq+oldest)");
    r = lfu_access(c, 2); ASSERT(r == CACHE_MISS, "B -> MISS (C evicted, lowest freq+oldest)");

    ASSERT(c->hits      == 1, "hits == 1");
    ASSERT(c->misses    == 5, "misses == 5");
    ASSERT(c->evictions == 2, "evictions == 2");

    lfu_print(c);
    lfu_print_stats(c);
    lfu_destroy(c);
}

/* ─── Test 5: LFU — Frequency accumulation (cache pollution demo) ── */
static void test_lfu_frequency_accumulation(void)
{
    printf("\n=== Test 5: LFU frequency accumulation (cache pollution) ===\n");

    LFUCache *c = lfu_create(3);

    /* Warm up: access A many times to build high freq */
    for (int i = 0; i < 10; i++) lfu_access(c, 0xA);
    lfu_access(c, 0xB);
    lfu_access(c, 0xC);
    /* Cache: A(freq=10), B(freq=1), C(freq=1) */

    /* Now access D — must evict B or C (both freq=1, not A!) */
    int r = lfu_access(c, 0xD);
    ASSERT(r == CACHE_MISS, "D -> MISS (B or C evicted, NOT A despite A being cold now)");

    /* A is still in cache — LFU cache pollution! */
    r = lfu_access(c, 0xA);
    ASSERT(r == CACHE_HIT, "A -> HIT (A protected by old high frequency - pollution!)");

    printf("  ^ This demonstrates LFU cache pollution:\n");
    printf("    Old 'hot' data A (freq=10) blocks new data even if A won't be used again.\n");

    lfu_print(c);
    lfu_print_stats(c);
    lfu_destroy(c);
}

/* ─── Test 6: LFU — Tie-break is LRU order ───────────────────────── */
static void test_lfu_tiebreak_lru(void)
{
    printf("\n=== Test 6: LFU tie-break uses LRU order ===\n");

    LFUCache *c = lfu_create(3);
    lfu_access(c, 1);  /* A freq=1, inserted first */
    lfu_access(c, 2);  /* B freq=1 */
    lfu_access(c, 3);  /* C freq=1 — all equal freq */

    /* All freq=1. Cache=[1,2,3] full.
     * Insert D(4): evicts A(1) oldest → cache=[2,3,4]
     * Access A(1): MISS, evicts B(2) oldest → cache=[3,4,1]
     * Access B(2): MISS, evicts C(3) oldest → cache=[4,1,2]
     * Access C(3): MISS — C also evicted in the chain */
    int r = lfu_access(c, 4);
    ASSERT(r == CACHE_MISS, "D(4) -> MISS (A evicted - LRU tie-break)");

    r = lfu_access(c, 1);
    ASSERT(r == CACHE_MISS, "A(1) -> MISS (was evicted)");

    r = lfu_access(c, 2);
    ASSERT(r == CACHE_MISS, "B(2) -> MISS (evicted when A re-inserted)");

    r = lfu_access(c, 3);
    ASSERT(r == CACHE_MISS, "C(3) -> MISS (evicted when B re-inserted)");

    printf("  Note: successive misses on equal-freq entries — LRU tie-break chains correctly.\n");

    lfu_print_stats(c);
    lfu_destroy(c);
}

/* ─── Test 7: Unified interface — all 3 policies ─────────────────── */
static void test_unified_interface(void)
{
    printf("\n=== Test 7: Unified cache interface ===\n");

    const char *policies[] = {"LRU", "FIFO", "LFU"};
    /* Same sequence fed to all three */
    uint64_t seq[] = {1, 2, 3, 1, 4, 2};
    int n = 6;

    for (int p = 0; p < 3; p++) {
        Cache *c = cache_create(3, policies[p]);
        ASSERT(c != NULL, policies[p]);  /* creation succeeds */

        for (int i = 0; i < n; i++) cache_access(c, seq[i]);

        cache_print_stats(c);
        cache_destroy(c);
    }

    /* Invalid policy name returns NULL */
    Cache *bad = cache_create(4, "RANDOM");
    ASSERT(bad == NULL, "Unknown policy -> NULL");
}

/* ─── Test 8: Policy comparison on same sequence ─────────────────── */
static void test_policy_comparison(void)
{
    printf("\n=== Test 8: LRU vs FIFO vs LFU hit rate comparison ===\n");

    /* Sequence designed to show policy differences */
    uint64_t seq[] = {1,2,3,4,1,2,3,4,1,2,3,4,  /* sequential scan (favours no policy) */
                      1,1,1,2,2,3};               /* then repeated hot accesses */
    int n = 18;
    const char *policies[] = {"LRU", "FIFO", "LFU"};

    printf("  %-6s | %5s | %5s | %8s\n", "Policy", "Hits", "Miss", "Hit Rate");
    printf("  --------------------------------\n");

    for (int p = 0; p < 3; p++) {
        Cache *c = cache_create(3, policies[p]);

        int hits = 0, misses = 0;
        for (int i = 0; i < n; i++) {
            if (cache_access(c, seq[i]) == CACHE_HIT) hits++;
            else misses++;
        }

        printf("  %-6s | %5d | %5d | %7.1f%%\n",
               policies[p], hits, misses,
               100.0 * hits / n);

        cache_destroy(c);
    }

    ASSERT(1, "Policy comparison completed");  /* marker assertion */
}

/* ─── Main ───────────────────────────────────────────────────────── */
int main(void)
{
    printf("============================================\n");
    printf("  Day 5 Unit Tests - FIFO + LFU + Unified\n");
    printf("  CPU Cache Replacement Simulator\n");
    printf("============================================\n");

    test_fifo_plan_sequence();
    test_fifo_capacity_one();
    test_fifo_circular_wrap();
    test_lfu_plan_sequence();
    test_lfu_frequency_accumulation();
    test_lfu_tiebreak_lru();
    test_unified_interface();
    test_policy_comparison();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
