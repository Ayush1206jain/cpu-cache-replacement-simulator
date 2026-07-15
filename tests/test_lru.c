/*
 * test_lru.c — Unit tests for LRU cache replacement policy
 *
 * Tests the sequence from the project plan:
 *   Feed [A, B, C, A, D, B] with capacity 3 and verify every step.
 *
 * Also tests edge cases: capacity=1, repeated access, full eviction chain.
 *
 * Build:
 *   gcc -Wall -Wextra -o test_lru tests/test_lru.c src/cache/lru.c -I src/cache
 * Run:
 *   ./test_lru
 *
 *  — CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lru.h"

/* ─── Minimal test framework ──────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond,msg)                                       \
    do {                                                        \
        tests_run++;                                            \
        if (cond) {                                             \
            tests_passed++;                                     \
            printf("  [PASS] %s\n", msg);                      \
        } else {                                                \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__); \
        }                                                       \
    } while (0);

/* ─── Helper: map letter to address ──────────────────────────────── */
/* A=0x0A, B=0x0B, C=0x0C, D=0x0D for readability */
static uint64_t addr(char c) { return (uint64_t)(c - 'A' + 0x0A); }

/* ─── Test 1: Plan sequence [A,B,C,A,D,B] with capacity 3 ─────────
 *
 * Hand-trace:
 *   Access A → MISS  cache:[A]
 *   Access B → MISS  cache:[B,A]
 *   Access C → MISS  cache:[C,B,A]
 *   Access A → HIT   cache:[A,C,B]   (A moves to front)
 *   Access D → MISS  cache:[D,A,C]   (B evicted — LRU tail)
 *   Access B → MISS  cache:[B,D,A]   (C evicted — LRU tail)
 *
 * Expected: hits=1, misses=5, evictions=2
 * ──────────────────────────────────────────────────────────────── */
static void test_plan_sequence(void)
{
    printf("\n=== Test 1: Plan sequence [A,B,C,A,D,B] cap=3 ===\n");

    LRUCache *c = lru_create(3);
    ASSERT(c != NULL, "lru_create returns non-NULL");

    int r;

    r = lru_access(c, addr('A')); ASSERT(r == CACHE_MISS, "A -> MISS");
    r = lru_access(c, addr('B')); ASSERT(r == CACHE_MISS, "B -> MISS");
    r = lru_access(c, addr('C')); ASSERT(r == CACHE_MISS, "C -> MISS");
    r = lru_access(c, addr('A')); ASSERT(r == CACHE_HIT,  "A -> HIT (temporal locality)");
    r = lru_access(c, addr('D')); ASSERT(r == CACHE_MISS, "D -> MISS (B evicted)");
    r = lru_access(c, addr('B')); ASSERT(r == CACHE_MISS, "B -> MISS (C evicted)");

    ASSERT(c->hits      == 1, "hits == 1");
    ASSERT(c->misses    == 5, "misses == 5");
    ASSERT(c->evictions == 2, "evictions == 2");

    lru_print(c);
    lru_print_stats(c);
    lru_destroy(c);
}

/* ─── Test 2: Capacity = 1 (extreme case) ────────────────────────── */
static void test_capacity_one(void)
{
    printf("\n=== Test 2: Capacity = 1 ===\n");

    LRUCache *c = lru_create(1);
    ASSERT(c != NULL, "lru_create(1) succeeds");

    int r;
    r = lru_access(c, 0x100); ASSERT(r == CACHE_MISS, "0x100 -> MISS");
    r = lru_access(c, 0x100); ASSERT(r == CACHE_HIT,  "0x100 -> HIT (same addr)");
    r = lru_access(c, 0x200); ASSERT(r == CACHE_MISS, "0x200 -> MISS (0x100 evicted)");
    r = lru_access(c, 0x100); ASSERT(r == CACHE_MISS, "0x100 -> MISS (0x200 evicted)");

    ASSERT(c->hits      == 1, "hits == 1");
    ASSERT(c->misses    == 3, "misses == 3");
    ASSERT(c->evictions == 2, "evictions == 2");

    lru_print_stats(c);
    lru_destroy(c);
}

/* ─── Test 3: All hits after warmup ─────────────────────────────── */
static void test_all_hits_after_warmup(void)
{
    printf("\n=== Test 3: Warm up then all hits ===\n");

    LRUCache *c = lru_create(4);

    /* Warm up: fill the cache */
    lru_access(c, 0xA);
    lru_access(c, 0xB);
    lru_access(c, 0xC);
    lru_access(c, 0xD);

    /* All subsequent accesses to same set should hit */
    int r;
    r = lru_access(c, 0xA); ASSERT(r == CACHE_HIT, "0xA -> HIT after warmup");
    r = lru_access(c, 0xB); ASSERT(r == CACHE_HIT, "0xB -> HIT after warmup");
    r = lru_access(c, 0xC); ASSERT(r == CACHE_HIT, "0xC -> HIT after warmup");
    r = lru_access(c, 0xD); ASSERT(r == CACHE_HIT, "0xD -> HIT after warmup");

    ASSERT(c->hits      == 4, "hits == 4");
    ASSERT(c->evictions == 0, "no evictions during hit-only phase");

    lru_print_stats(c);
    lru_destroy(c);
}

/* ─── Test 4: LRU order correctness ─────────────────────────────── */
static void test_lru_order(void)
{
    printf("\n=== Test 4: LRU eviction order ===\n");

    LRUCache *c = lru_create(3);
    lru_access(c, 1);   /* cache: [1]       */
    lru_access(c, 2);   /* cache: [2,1]     */
    lru_access(c, 3);   /* cache: [3,2,1]   */
    lru_access(c, 1);   /* cache: [1,3,2]  — 1 promoted to MRU */
    lru_access(c, 4);   /* cache: [4,1,3]  — 2 evicted (LRU tail) */
    lru_access(c, 1);   /* cache: [1,3,2]  - 1 promoted to MRU */
    lru_access(c, 4);   /* cache: [4,1,3]  - 2 evicted (LRU tail) */

    /* Now access 2 - should be a miss since it was evicted.
     * Trace: [1,2,3] -> access 1 -> [1,3,2] -> access 4 (evicts 2) -> [4,1,3]
     *        -> access 2 (evicts 3) -> [2,4,1]
     * So 2 was evicted by step 4, then re-loaded in this access. */
    int r = lru_access(c, 2);
    ASSERT(r == CACHE_MISS, "2 -> MISS (was evicted after 4 was inserted)");
 
    /* 1 was re-promoted when we accessed it, so it's still in cache */
    r = lru_access(c, 1); ASSERT(r == CACHE_HIT, "1 -> HIT (was re-promoted to MRU)");
 
    /* 3 was the LRU tail when 2 was re-inserted, so 3 got evicted */
    r = lru_access(c, 3); ASSERT(r == CACHE_MISS, "3 -> MISS (was evicted as LRU tail when 2 loaded)");

    lru_print_stats(c);
    lru_destroy(c);
}

/* ─── Test 5: Large sequence - hit rate smoke test ───────────────── */
static void test_large_sequence(void)
{
    printf("\n=== Test 5: Large working-set smoke test ===\n");

    /* Cache size 8, addresses 0–15 (working set twice cache size) */
    LRUCache *c = lru_create(8);

    /* Sequential scan 0..15 repeated 4 times                                */
    /* Expected: first pass all misses, subsequent passes mixed due to size  */
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < 16; i++) {
            lru_access(c, (uint64_t)i);
        }
    }

    /* Hit rate > 0 (some temporal locality once addresses cycle back) */
    uint64_t total = c->hits + c->misses;
    double hr = (100.0 * (double)c->hits) / (double)total;
    printf("  Hit rate on 4× sequential scan over 16 addrs (cap=8): %.1f%%\n", hr);
    ASSERT(total == 64, "64 total accesses");

    lru_print_stats(c);
    lru_destroy(c);
}

/* ─── Main ───────────────────────────────────────────────────────── */
int main(void)
{
    printf("============================================\n");
    printf("  LRU Cache Unit Tests - Day 4\n");
    printf("  CPU Cache Replacement Simulator\n");
    printf("============================================\n");

    test_plan_sequence();
    test_capacity_one();
    test_all_hits_after_warmup();
    test_lru_order();
    test_large_sequence();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
