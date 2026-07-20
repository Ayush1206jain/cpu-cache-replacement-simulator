/*
 * test_day11.c -- Unit tests for Workload Generators and Benchmark Engine
 *
 * Tests:
 *  1. Sequential: word-stride (8B), wraps at region, addresses aligned
 *  2. Random: range, alignment, reproducible seed
 *  3. workload_build_all: 2 workloads (Sequential, Random)
 *  4. Benchmark: sequential hit rate > 50% (spatial locality present)
 *  5. Benchmark: sequential hit rate increases with cache size (LRU)
 *  6. Benchmark: policy_matrix all cells in [0, 100]
 *  7. Benchmark: CSV file created and non-empty
 *  8. Interview Q: LFU <= LRU on sequential scan
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o test_day11
 *       tests/test_day11.c src/workload.c src/benchmark.c
 *       src/cache/set_cache.c src/cache/lru.c src/cache/fifo.c
 *       src/cache/lfu.c src/cache/cache.c
 *       -Isrc -Isrc/cache -lm
 *
 * Day 11 -- CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "workload.h"
#include "benchmark.h"
#include "set_cache.h"

/* ------------------------------------------------------------------ */
/* Test framework                                                       */
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
/* Test 1: Sequential address generation                                */
/* ------------------------------------------------------------------ */
static void test_sequential(void)
{
    printf("\n=== Test 1: Sequential workload (word stride = 8B) ===\n");

    size_t n = 16;
    uint64_t *a = workload_sequential(n);
    ASSERT(a != NULL, "workload_sequential returns non-NULL");

    /* First address is WL_BASE_ADDR */
    ASSERT(a[0] == WL_BASE_ADDR, "First addr == WL_BASE_ADDR");

    /* FIX: stride is now 8 bytes (WORD), not 64 bytes (LINE) */
    ASSERT(a[1] == a[0] + 8,  "Second addr = BASE + 8  (word stride)");
    ASSERT(a[2] == a[0] + 16, "Third addr  = BASE + 16 (word stride)");

    /* First 8 accesses share the SAME cache line (spatial locality) */
    uint64_t line0 = a[0] & ~(uint64_t)(WL_LINE_SIZE - 1);
    int same_line = 1;
    for (size_t i = 0; i < 8; i++)
        if ((a[i] & ~(uint64_t)(WL_LINE_SIZE-1)) != line0) { same_line=0; break; }
    ASSERT(same_line, "First 8 accesses share the same 64B cache line");

    /* All addresses are within the valid region */
    int in_range = 1;
    for (size_t i = 0; i < n; i++)
        if (a[i] < WL_BASE_ADDR || a[i] >= WL_BASE_ADDR + WL_REGION_SIZE)
            { in_range = 0; break; }
    ASSERT(in_range, "All sequential addresses within [BASE, BASE+REGION)");

    /* Wraps: n_words-th access == first (3KB window = 384 words) */
    size_t n_words = (3 * 1024) / 8;   /* 384 words */
    uint64_t *b = workload_sequential(n_words + 1);
    ASSERT(b != NULL, "Wrap test allocated");
    ASSERT(b[n_words] == b[0], "Sequential wraps at 3KB boundary");
    free(b);

    free(a);
}

/* ------------------------------------------------------------------ */
/* Test 2: Random address range                                         */
/* ------------------------------------------------------------------ */
static void test_random(void)
{
    printf("\n=== Test 2: Random workload range ===\n");

    size_t n = 1000;
    uint64_t *a = workload_random(n, 42);
    ASSERT(a != NULL, "workload_random returns non-NULL");

    int in_range = 1, aligned = 1;
    uint64_t max_addr = WL_BASE_ADDR + WL_REGION_SIZE;
    for (size_t i = 0; i < n; i++) {
        if (a[i] < WL_BASE_ADDR || a[i] >= max_addr)  in_range = 0;
        if (a[i] % WL_LINE_SIZE != 0)                  aligned  = 0;
    }
    ASSERT(in_range, "All random addrs in [BASE, BASE+REGION)");
    ASSERT(aligned,  "All random addrs cache-line aligned");

    /* Same seed -> same sequence */
    uint64_t *b = workload_random(n, 42);
    ASSERT(b != NULL, "Second call non-NULL");
    ASSERT(memcmp(a, b, n * sizeof(uint64_t)) == 0,
           "Same seed produces identical sequence");
    free(b);

    /* Different seed -> different sequence */
    uint64_t *c = workload_random(n, 99);
    ASSERT(c != NULL, "Different seed non-NULL");
    ASSERT(memcmp(a, c, n * sizeof(uint64_t)) != 0,
           "Different seed produces different sequence");
    free(c);

    free(a);
}

/* ------------------------------------------------------------------ */
/* Test 3: workload_build_all (2 workloads)                            */
/* ------------------------------------------------------------------ */
static void test_build_all(void)
{
    printf("\n=== Test 3: workload_build_all (2 workloads) ===\n");

    Workload *wl = workload_build_all(500, 42);
    ASSERT(wl != NULL, "workload_build_all returns non-NULL");

    ASSERT(wl[0].addrs != NULL && strcmp(wl[0].name, "Sequential") == 0,
           "wl[0] = Sequential");
    ASSERT(wl[1].addrs != NULL && strcmp(wl[1].name, "Random") == 0,
           "wl[1] = Random");
    ASSERT(wl[0].n == 500, "Sequential has 500 accesses");
    ASSERT(wl[1].n == 500, "Random has 500 accesses");

    workload_free_all(wl, 2);
}

/* ------------------------------------------------------------------ */
/* Test 4: Sequential hit rate > 50% (spatial locality is present)     */
/* ------------------------------------------------------------------ */
static void test_sequential_hit_rate(void)
{
    printf("\n=== Test 4: Sequential hit rate > 50%% (spatial locality) ===\n");

    /* 4KB, 4-way cache */
    int n_sets = 16, n_ways = 4;
    size_t n = BENCH_N_ACCESSES;

    uint64_t *seq = workload_sequential(n);
    SetAssocCache *c = set_cache_create(n_sets, n_ways, 64, POLICY_LRU);
    for (size_t i = 0; i < n; i++) set_cache_access(c, seq[i]);

    double hr = 100.0 * (double)c->hits / (double)(c->hits + c->misses);
    printf("  4KB LRU sequential hit rate: %.2f%%\n", hr);
    /* Word-stride gives ~87.5% intra-line hits regardless of cache size */
    ASSERT(hr > 50.0, "Sequential hit rate > 50%% (spatial locality works)");

    set_cache_destroy(c);
    free(seq);
}

/* ------------------------------------------------------------------ */
/* Test 6: LRU hit rate increases with cache size (sequential)         */
/* ------------------------------------------------------------------ */
static void test_size_sweep_sequential(void)
{
    printf("\n=== Test 6: Hit rate increases with cache size (seq, LRU) ===\n");

    size_t n = BENCH_N_ACCESSES;
    Workload wl;
    wl.name  = "Sequential";
    wl.addrs = workload_sequential(n);
    wl.n     = n;

    int sizes_kb[] = {1, 4, 16, 64};
    double prev_hr = -1.0;
    int monotone = 1;

    for (int i = 0; i < 4; i++) {
        int total_bytes = sizes_kb[i] * 1024;
        int n_ways = 4;
        int n_sets = total_bytes / (n_ways * 64);
        if (n_sets < 1) n_sets = 1;
        /* round down to power of 2 */
        int p2 = 1;
        while (p2 * 2 <= n_sets) p2 <<= 1;
        n_sets = p2;

        SetAssocCache *c = set_cache_create(n_sets, n_ways, 64, POLICY_LRU);
        for (size_t j = 0; j < n; j++) set_cache_access(c, wl.addrs[j]);

        double hr = (c->hits + c->misses > 0)
                    ? 100.0 * (double)c->hits / (double)(c->hits + c->misses)
                    : 0.0;
        printf("  %2dKB: %.2f%% hit rate\n", sizes_kb[i], hr);

        if (prev_hr > hr + 0.01) monotone = 0;  /* allow tiny float error */
        prev_hr = hr;
        set_cache_destroy(c);
    }

    ASSERT(monotone, "Hit rate is non-decreasing as cache size grows");
    free(wl.addrs);
}

/* ------------------------------------------------------------------ */
/* Test 6: (renumbered -- placeholder slot kept for index alignment)    */
/* policy_matrix all cells in [0, 100]                                 */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Test 6: All policy_matrix cells in [0, 100]                        */
/* ------------------------------------------------------------------ */
static void test_policy_matrix_valid(void)
{
    printf("\n=== Test 8: Policy matrix all cells in [0%%, 100%%] ===\n");

    BenchReport r = benchmark_run(4);  /* 4KB default size */

    int all_valid = 1;
    for (int p = 0; p < BENCH_N_POLICIES; p++) {
        for (int w = 0; w < BENCH_N_WORKLOADS; w++) {
            double hr = r.policy_matrix[p][w].hit_rate;
            if (hr < 0.0 || hr > 100.0) all_valid = 0;
        }
    }
    ASSERT(all_valid, "All policy_matrix hit rates in [0, 100]");

    int all_size_valid = 1;
    for (int s = 0; s < BENCH_N_SIZES; s++) {
        for (int w = 0; w < BENCH_N_WORKLOADS; w++) {
            double hr = r.size_matrix[s][w].hit_rate;
            if (hr < 0.0 || hr > 100.0) all_size_valid = 0;
        }
    }
    ASSERT(all_size_valid, "All size_matrix hit rates in [0, 100]");
}

/* ------------------------------------------------------------------ */
/* Test 7: CSV file created and non-empty                              */
/* ------------------------------------------------------------------ */
static void test_csv_output(void)
{
    printf("\n=== Test 9: CSV file created and non-empty ===\n");

    const char *csv = "results/test_results.csv";
    BenchReport r = benchmark_run(4);
    benchmark_write_csv(&r, csv);

    FILE *f = fopen(csv, "r");
    ASSERT(f != NULL, "CSV file exists after benchmark_write_csv");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        ASSERT(sz > 100, "CSV file is non-empty (> 100 bytes)");
        fclose(f);

        /* Check header line */
        f = fopen(csv, "r");
        char line[256];
        ASSERT(fgets(line, sizeof(line), f) != NULL, "CSV has at least one line");
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* Test 8: Interview Q -- why LFU worse than LRU on sequential scan  */
/* ------------------------------------------------------------------ */
static void test_lfu_vs_lru_sequential(void)
{
    printf("\n=== Test 10: Interview Q -- LFU vs LRU on sequential scan ===\n");

    size_t n = BENCH_N_ACCESSES;
    int n_sets = 4;    /* very small -- force evictions */
    int n_ways = 4;

    uint64_t *seq = workload_sequential(n);

    SetAssocCache *lru_c = set_cache_create(n_sets, n_ways, 64, POLICY_LRU);
    SetAssocCache *lfu_c = set_cache_create(n_sets, n_ways, 64, POLICY_LFU);

    for (size_t i = 0; i < n; i++) {
        set_cache_access(lru_c, seq[i]);
        set_cache_access(lfu_c, seq[i]);
    }

    double hr_lru = 100.0*(double)lru_c->hits / (double)(lru_c->hits+lru_c->misses);
    double hr_lfu = 100.0*(double)lfu_c->hits / (double)(lfu_c->hits+lfu_c->misses);

    printf("  LRU hit rate on sequential: %.2f%%\n", hr_lru);
    printf("  LFU hit rate on sequential: %.2f%%\n", hr_lfu);
    printf("  Answer: LFU fails because sequential scan never repeats addresses.\n");
    printf("  LFU evicts NEW entries (freq=1) over stale old entries (freq=1 too)\n");
    printf("  but since frequencies are ALL equal, LFU behaves like FIFO.\n");
    printf("  LRU keeps the MOST RECENT entries which have the best spatial locality.\n");

    /* LFU should be <= LRU on pure sequential */
    ASSERT(hr_lfu <= hr_lru + 1.0,
           "LFU hit rate <= LRU on sequential scan (confirms interview answer)");

    set_cache_destroy(lru_c);
    set_cache_destroy(lfu_c);
    free(seq);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    /* Create results/ directory if needed (best-effort) */
    system("mkdir results 2>nul");

    printf("============================================\n");
    printf("  Day 11 Tests -- Workloads + Benchmark\n");
    printf("  CPU Cache Replacement Simulator\n");
    printf("============================================\n");

    test_sequential();
    test_random();
    test_build_all();
    test_sequential_hit_rate();
    test_size_sweep_sequential();
    test_policy_matrix_valid();
    test_csv_output();
    test_lfu_vs_lru_sequential();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
