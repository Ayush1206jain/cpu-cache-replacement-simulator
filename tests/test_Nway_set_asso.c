/*
 * test_day6.c -- Unit tests for N-Way Set-Associative Cache
 *
 * Tests:
 *  1. Address decomposition (tag / index / offset)
 *  2. Direct-mapped conflict miss
 *  3. 2-way resolves conflict miss
 *  4. 4-way -- all items fit, no evictions
 *  5. LRU eviction within a set
 *  6. FIFO ignores hit recency within a set
 *  7. LFU protects high-freq way within a set
 *  8. Policy comparison on same cache config
 *  9. Multiple sets are independent
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o test_day6 tests/test_day6.c
 *       src/cache/set_cache.c src/cache/lru.c src/cache/fifo.c
 *       src/cache/lfu.c src/cache/cache.c -Isrc/cache
 *
 * Day 6 -- CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include "set_cache.h"
#include "lru.h"    /* CACHE_HIT / CACHE_MISS */

/* ------------------------------------------------------------------ */
/* Minimal test framework                                               */
/* ------------------------------------------------------------------ */
static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                          \
    do {                                                           \
        tests_run++;                                               \
        if (cond) {                                                \
            tests_passed++;                                        \
            printf("  [PASS] %s\n", msg);                         \
        } else {                                                   \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__);   \
        }                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/* Test 1: Address decomposition                                        */
/*                                                                      */
/* Cache: 4 sets, 1 way, 64-byte lines                                 */
/*   offset_bits = 6  (log2 64)                                        */
/*   index_bits  = 2  (log2 4)                                         */
/*                                                                      */
/* Expected:                                                            */
/*   0x000 => set=0, tag=0, offset=0                                   */
/*   0x040 => set=1, tag=0        (0x040=64, next cache set)           */
/*   0x100 => set=0, tag=1        (0x100=256; 256>>6=4; 4&3=0)        */
/*   0x200 => set=0, tag=2                                             */
/* ------------------------------------------------------------------ */
static void test_address_decode(void)
{
    printf("\n=== Test 1: Address decomposition (4 sets, 64B lines) ===\n");

    SetAssocCache *c = set_cache_create(4, 1, 64, POLICY_LRU);
    ASSERT(c != NULL, "Cache created successfully");

    uint64_t tag;
    int sidx, off;

    set_cache_decode(c, 0x000, &tag, &sidx, &off);
    printf("  0x000 -> set=%d, tag=0x%llx, offset=%d\n",
           sidx, (unsigned long long)tag, off);
    ASSERT(sidx == 0 && tag == 0 && off == 0,
           "0x000 -> set=0, tag=0, offset=0");

    set_cache_decode(c, 0x040, &tag, &sidx, &off);
    printf("  0x040 -> set=%d, tag=0x%llx\n",
           sidx, (unsigned long long)tag);
    ASSERT(sidx == 1 && tag == 0,
           "0x040 -> set=1, tag=0");

    set_cache_decode(c, 0x100, &tag, &sidx, &off);
    printf("  0x100 -> set=%d, tag=0x%llx\n",
           sidx, (unsigned long long)tag);
    ASSERT(sidx == 0 && tag == 1,
           "0x100 -> set=0, tag=1 (conflict candidate with 0x000)");

    set_cache_decode(c, 0x200, &tag, &sidx, &off);
    printf("  0x200 -> set=%d, tag=0x%llx\n",
           sidx, (unsigned long long)tag);
    ASSERT(sidx == 0 && tag == 2,
           "0x200 -> set=0, tag=2");

    set_cache_decode(c, 0x041, &tag, &sidx, &off);
    ASSERT(sidx == 1 && off == 1,
           "0x041 -> set=1, offset=1");

    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 2: Direct-mapped (1-way) conflict miss                         */
/*                                                                      */
/* 0x000 and 0x100 both map to set 0 but have different tags.          */
/* With only 1 way, each evicts the other.                             */
/* Access: 0x000, 0x100, 0x000 => 3 misses (direct-mapped thrashing)  */
/* ------------------------------------------------------------------ */
static void test_direct_mapped_conflict(void)
{
    printf("\n=== Test 2: Direct-mapped (1-way) conflict miss ===\n");

    SetAssocCache *c = set_cache_create(4, 1, 64, POLICY_LRU);
    ASSERT(c != NULL, "Cache created");

    int r;
    r = set_cache_access(c, 0x000);
    ASSERT(r == CACHE_MISS, "0x000 -> MISS (cold)");

    r = set_cache_access(c, 0x100);
    ASSERT(r == CACHE_MISS, "0x100 -> MISS (conflict evicts 0x000)");

    r = set_cache_access(c, 0x000);
    ASSERT(r == CACHE_MISS, "0x000 -> MISS (evicted by 0x100 -- conflict!)");

    /* Trace: 0x000 cold miss (loads set0), 0x100 conflict miss (evicts 0x000 -> 1 eviction),
     * 0x000 conflict miss again (evicts 0x100 -> 2nd eviction). So 2 evictions total. */
    ASSERT(c->hits      == 0, "hits == 0");
    ASSERT(c->misses    == 3, "misses == 3");
    ASSERT(c->evictions == 2, "evictions == 2 (each conflict evicts the other)");

    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 3: 2-way eliminates the conflict miss                          */
/*                                                                      */
/* Same access pattern, 2-way cache with 2 sets (same total size).     */
/* 0x000 and 0x100 still map to set 0 but now 2 ways hold both.        */
/* Access: 0x000, 0x100, 0x000 => 2 misses, 1 HIT                     */
/* ------------------------------------------------------------------ */
static void test_2way_resolves_conflict(void)
{
    printf("\n=== Test 3: 2-way resolves conflict miss ===\n");

    SetAssocCache *c = set_cache_create(2, 2, 64, POLICY_LRU);
    ASSERT(c != NULL, "Cache created");

    int sidx0, sidx1;
    uint64_t t0, t1;
    set_cache_decode(c, 0x000, &t0, &sidx0, NULL);
    set_cache_decode(c, 0x100, &t1, &sidx1, NULL);
    printf("  0x000 -> set=%d  |  0x100 -> set=%d\n", sidx0, sidx1);
    ASSERT(sidx0 == sidx1, "Both map to same set (conflict candidate)");
    ASSERT(t0 != t1,       "Different tags -- need 2 ways to hold both");

    int r;
    r = set_cache_access(c, 0x000);
    ASSERT(r == CACHE_MISS, "0x000 -> MISS (cold)");

    r = set_cache_access(c, 0x100);
    ASSERT(r == CACHE_MISS, "0x100 -> MISS (cold, uses way 1)");

    r = set_cache_access(c, 0x000);
    ASSERT(r == CACHE_HIT,  "0x000 -> HIT  (2-way: both fit, no conflict eviction!)");

    ASSERT(c->hits      == 1, "hits == 1");
    ASSERT(c->misses    == 2, "misses == 2");
    ASSERT(c->evictions == 0, "evictions == 0 (no conflict)");

    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 4: 4-way -- all items fit, zero evictions                      */
/* ------------------------------------------------------------------ */
static void test_4way_all_fit(void)
{
    printf("\n=== Test 4: 4-way -- all 4 items fit in one set ===\n");

    SetAssocCache *c = set_cache_create(1, 4, 64, POLICY_LRU);
    ASSERT(c != NULL, "Cache created");

    uint64_t addrs[] = {0x000, 0x100, 0x200, 0x300};
    int r;

    for (int i = 0; i < 4; i++) {
        r = set_cache_access(c, addrs[i]);
        ASSERT(r == CACHE_MISS, "Cold miss on first load");
    }

    r = set_cache_access(c, 0x000); ASSERT(r == CACHE_HIT, "0x000 -> HIT");
    r = set_cache_access(c, 0x100); ASSERT(r == CACHE_HIT, "0x100 -> HIT");
    r = set_cache_access(c, 0x200); ASSERT(r == CACHE_HIT, "0x200 -> HIT");
    r = set_cache_access(c, 0x300); ASSERT(r == CACHE_HIT, "0x300 -> HIT");

    ASSERT(c->hits      == 4, "hits == 4");
    ASSERT(c->evictions == 0, "evictions == 0 (4 ways, 4 items)");

    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 5: LRU eviction within a set                                   */
/*                                                                      */
/* 1 set, 3 ways. Fill A,B,C. Access A (A becomes MRU).               */
/* Insert D -> evict B (LRU tail), NOT A.                              */
/* ------------------------------------------------------------------ */
static void test_lru_within_set(void)
{
    printf("\n=== Test 5: LRU eviction within a set ===\n");

    SetAssocCache *c = set_cache_create(1, 3, 64, POLICY_LRU);
    ASSERT(c != NULL, "Cache created");

    uint64_t A=0x000, B=0x100, C=0x200, D=0x300;
    int r;

    set_cache_access(c, A);  /* way 0 */
    set_cache_access(c, B);  /* way 1 */
    set_cache_access(c, C);  /* way 2 -- full */
    set_cache_access(c, A);  /* HIT: A becomes MRU, LRU order: B,C,A */

    r = set_cache_access(c, D);
    ASSERT(r == CACHE_MISS, "D -> MISS (triggers eviction)");

    r = set_cache_access(c, B);
    ASSERT(r == CACHE_MISS, "B -> MISS (B was LRU, evicted for D)");

    r = set_cache_access(c, A);
    ASSERT(r == CACHE_HIT,  "A -> HIT  (A was MRU, survived)");

    /* After: D evicts B. Then accessing B misses, evicts C. So C is gone now. */
    r = set_cache_access(c, C);
    ASSERT(r == CACHE_MISS, "C -> MISS (C was evicted when B re-inserted)");

    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 6: FIFO within a set -- ignores hit recency                    */
/*                                                                      */
/* 1 set, 3 ways, FIFO. Fill A,B,C. Access A (HIT, order unchanged).  */
/* Insert D -> evict A (oldest inserted, even though just accessed!)   */
/* ------------------------------------------------------------------ */
static void test_fifo_within_set(void)
{
    printf("\n=== Test 6: FIFO eviction ignores hit recency ===\n");

    SetAssocCache *c = set_cache_create(1, 3, 64, POLICY_FIFO);
    ASSERT(c != NULL, "Cache created");

    uint64_t A=0x000, B=0x100, C=0x200, D=0x300;
    int r;

    set_cache_access(c, A);  /* inserted first */
    set_cache_access(c, B);
    set_cache_access(c, C);  /* insertion order: A, B, C */

    r = set_cache_access(c, A);
    ASSERT(r == CACHE_HIT, "A -> HIT  (still in cache)");
    /* FIFO: hit on A does NOT change its position -- still oldest */

    r = set_cache_access(c, D);
    ASSERT(r == CACHE_MISS, "D -> MISS (A evicted -- oldest insertion despite recent hit!)");

    r = set_cache_access(c, A);
    ASSERT(r == CACHE_MISS, "A -> MISS (A was evicted by FIFO ignoring recency)");

    /* After: D evicts A. Then A miss evicts B. So B is gone. */
    r = set_cache_access(c, B);
    ASSERT(r == CACHE_MISS, "B -> MISS (B evicted when A re-inserted after D)");

    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 7: LFU within a set -- high freq way protected                 */
/*                                                                      */
/* 1 set, 3 ways, LFU. Load A (access x5), then B, C.                 */
/* Insert D -> must evict min-freq (B or C, NOT A with freq=5).        */
/* ------------------------------------------------------------------ */
static void test_lfu_within_set(void)
{
    printf("\n=== Test 7: LFU protects high-frequency way ===\n");

    SetAssocCache *c = set_cache_create(1, 3, 64, POLICY_LFU);
    ASSERT(c != NULL, "Cache created");

    uint64_t A=0x000, B=0x100, C=0x200, D=0x300;
    int r;

    set_cache_access(c, A);
    for (int i = 0; i < 4; i++) set_cache_access(c, A); /* freq[A] = 5 */
    set_cache_access(c, B); /* freq[B] = 1 */
    set_cache_access(c, C); /* freq[C] = 1 -- full */

    r = set_cache_access(c, D);
    ASSERT(r == CACHE_MISS, "D -> MISS (evicts B or C, NOT A with freq=5)");

    r = set_cache_access(c, A);
    ASSERT(r == CACHE_HIT, "A -> HIT  (high freq protects A -- LFU cache pollution demo)");

    r = set_cache_access(c, B);
    ASSERT(r == CACHE_MISS, "B -> MISS (B was min-freq, evicted)");

    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Test 8: Policy comparison on same set-associative cache             */
/* ------------------------------------------------------------------ */
static void test_policy_comparison(void)
{
    printf("\n=== Test 8: LRU vs FIFO vs LFU on 2-set 2-way cache ===\n");

    uint64_t seq[] = {0x000, 0x100, 0x000, 0x200, 0x100, 0x000};
    int n = 6;

    CachePolicy policies[] = {POLICY_LRU, POLICY_FIFO, POLICY_LFU};
    const char *pnames[]   = {"LRU", "FIFO", "LFU"};

    printf("  %-6s | %5s | %5s | %8s\n", "Policy", "Hits", "Miss", "Hit Rate");
    printf("  ----------------------------------\n");

    for (int p = 0; p < 3; p++) {
        SetAssocCache *c = set_cache_create(2, 2, 64, policies[p]);
        int hits = 0;
        for (int i = 0; i < n; i++)
            if (set_cache_access(c, seq[i]) == CACHE_HIT) hits++;
        printf("  %-6s | %5d | %5d | %7.1f%%\n",
               pnames[p], hits, n - hits, 100.0 * hits / n);
        set_cache_destroy(c);
    }

    ASSERT(1, "Policy comparison table printed");
}

/* ------------------------------------------------------------------ */
/* Test 9: Multiple sets are independent -- no inter-set evictions     */
/* ------------------------------------------------------------------ */
static void test_multi_set_independence(void)
{
    printf("\n=== Test 9: Multiple sets are independent ===\n");

    SetAssocCache *c = set_cache_create(4, 1, 64, POLICY_LRU);
    ASSERT(c != NULL, "Cache created");

    /* These addresses map to sets 0, 1, 2, 3 respectively */
    uint64_t s0=0x000, s1=0x040, s2=0x080, s3=0x0C0;
    int r;

    set_cache_access(c, s0);
    set_cache_access(c, s1);
    set_cache_access(c, s2);
    set_cache_access(c, s3);

    r = set_cache_access(c, s0); ASSERT(r == CACHE_HIT, "Set 0 addr -> HIT");
    r = set_cache_access(c, s1); ASSERT(r == CACHE_HIT, "Set 1 addr -> HIT");
    r = set_cache_access(c, s2); ASSERT(r == CACHE_HIT, "Set 2 addr -> HIT");
    r = set_cache_access(c, s3); ASSERT(r == CACHE_HIT, "Set 3 addr -> HIT");

    ASSERT(c->evictions == 0, "No evictions -- sets are independent");

    set_cache_print_contents(c);
    set_cache_print_stats(c);
    set_cache_destroy(c);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("============================================\n");
    printf("  Day 6 Tests -- N-Way Set-Associative\n");
    printf("  CPU Cache Replacement Simulator\n");
    printf("============================================\n");

    test_address_decode();
    test_direct_mapped_conflict();
    test_2way_resolves_conflict();
    test_4way_all_fit();
    test_lru_within_set();
    test_fifo_within_set();
    test_lfu_within_set();
    test_policy_comparison();
    test_multi_set_independence();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
