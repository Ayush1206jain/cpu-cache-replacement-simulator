/*
 * test_day11.c -- Unit tests for Workload Generators and Benchmark Engine
 *
 * Tests:
 *  1. Sequential generates correct addresses (stride = 64B, wraps)
 *  2. Random generates addresses within valid range
 *  3. Zipfian: rank-1 address is most frequent (hot set)
 *  4. Mixed: address array is non-NULL, correct count
 *  5. workload_build_all: all 4 populated
 *  6. Benchmark: sequential scan hit rate increases with cache size (LRU)
 *  7. Benchmark: Zipfian > Random hit rate for same small cache (LRU)
 *  8. Benchmark: policy_matrix all cells non-negative and <= 100%
 *  9. Benchmark: CSV file is created and non-empty
 * 10. Interview Q: LFU vs LRU on sequential scan (LFU worse explanation)
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
    printf("\n=== Test 1: Sequential workload ===\n");

    size_t n = 16;
    uint64_t *a = workload_sequential(n);
    ASSERT(a != NULL, "workload_sequential returns non-NULL");

    /* First address is WL_BASE_ADDR */
    ASSERT(a[0] == WL_BASE_ADDR, "First addr == WL_BASE_ADDR");

    /* Stride is exactly WL_LINE_SIZE (64 bytes) */
    ASSERT(a[1] == a[0] + WL_LINE_SIZE, "Second addr = BASE + 64");
    ASSERT(a[2] == a[0] + 2 * WL_LINE_SIZE, "Third addr = BASE + 128");

    /* All addresses aligned to cache line boundary */
    int all_aligned = 1;
    for (size_t i = 0; i < n; i++)
        if (a[i] % WL_LINE_SIZE != 0) { all_aligned = 0; break; }
    ASSERT(all_aligned, "All sequential addresses cache-line aligned");

    /* Wraps: address (N_LINES)th access should equal first */
    size_t n_lines = WL_REGION_SIZE / WL_LINE_SIZE;
    uint64_t *b = workload_sequential(n_lines + 1);
    ASSERT(b != NULL, "Wrap test allocated");
    ASSERT(b[n_lines] == b[0], "Sequential wraps at region boundary");
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
/* Test 3: Zipfian -- rank-1 is the most frequent                      */
/* ------------------------------------------------------------------ */
static void test_zipfian(void)
{
    printf("\n=== Test 3: Zipfian -- hot address most frequent ===\n");

    size_t n = 5000;
    uint64_t *a = workload_zipfian(n, 42, 1.0);
    ASSERT(a != NULL, "workload_zipfian returns non-NULL");

    /* Count frequency of each line index */
    int max_freq_idx = 0;
    uint64_t *counts = (uint64_t *)calloc(WL_REGION_SIZE / WL_LINE_SIZE,
                                          sizeof(uint64_t));
    ASSERT(counts != NULL, "Frequency table allocated");

    for (size_t i = 0; i < n; i++) {
        int idx = (int)((a[i] - WL_BASE_ADDR) / WL_LINE_SIZE);
        if (idx >= 0 && idx < (int)(WL_REGION_SIZE / WL_LINE_SIZE))
            counts[idx]++;
    }

    /* Find most frequent */
    for (int k = 0; k < (int)(WL_REGION_SIZE / WL_LINE_SIZE); k++)
        if (counts[k] > counts[max_freq_idx]) max_freq_idx = k;

    /* Rank-0 (first line) should be the hot address */
    ASSERT(max_freq_idx == 0,
           "Rank-1 address (index 0) is the most frequent");

    /* Top 20% of addresses should cover >= 60% of accesses */
    int vocab = 1024;
    int top20 = vocab / 5;   /* 20% */
    uint64_t top20_count = 0;
    for (int k = 0; k < top20 && k < (int)(WL_REGION_SIZE / WL_LINE_SIZE); k++)
        top20_count += counts[k];
    double top20_pct = 100.0 * (double)top20_count / (double)n;
    printf("  Top 20%% addresses cover %.1f%% of accesses\n", top20_pct);
    ASSERT(top20_pct >= 60.0, "Top 20% addresses cover >= 60% accesses (Zipf law)");

    free(counts);
    free(a);
}

/* ------------------------------------------------------------------ */
/* Test 4: Mixed workload                                               */
/* ------------------------------------------------------------------ */
static void test_mixed(void)
{
    printf("\n=== Test 4: Mixed workload ===\n");

    size_t n = 1000;
    uint64_t *a = workload_mixed(n, 42);
    ASSERT(a != NULL, "workload_mixed returns non-NULL");

    /* All addresses in valid range */
    int in_range = 1, aligned = 1;
    uint64_t max_addr = WL_BASE_ADDR + WL_REGION_SIZE;
    for (size_t i = 0; i < n; i++) {
        if (a[i] < WL_BASE_ADDR || a[i] >= max_addr)  in_range = 0;
        if (a[i] % WL_LINE_SIZE != 0)                  aligned  = 0;
    }
    ASSERT(in_range, "All mixed addrs in valid range");
    ASSERT(aligned,  "All mixed addrs cache-line aligned");

    free(a);
}

/* ------------------------------------------------------------------ */
/* Test 5: workload_build_all                                           */
/* ------------------------------------------------------------------ */
static void test_build_all(void)
{
    printf("\n=== Test 5: workload_build_all ===\n");

    Workload *wl = workload_build_all(500, 42);
    ASSERT(wl != NULL, "workload_build_all returns non-NULL");

    ASSERT(wl[0].addrs != NULL && strcmp(wl[0].name, "Sequential") == 0,
           "wl[0] = Sequential");
    ASSERT(wl[1].addrs != NULL && strcmp(wl[1].name, "Random") == 0,
           "wl[1] = Random");
    ASSERT(wl[2].addrs != NULL && strcmp(wl[2].name, "Zipfian") == 0,
           "wl[2] = Zipfian");
    ASSERT(wl[3].addrs != NULL && strcmp(wl[3].name, "Mixed") == 0,
           "wl[3] = Mixed");

    for (int i = 0; i < 4; i++)
        ASSERT(wl[i].n == 500, "Each workload has 500 accesses");

    workload_free_all(wl, 4);
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
/* Test 7: Zipfian > Random hit rate for small cache                   */
/* ------------------------------------------------------------------ */
static void test_zipf_beats_random(void)
{
    printf("\n=== Test 7: Zipfian hit rate > Random hit rate (small cache) ===\n");

    size_t n = BENCH_N_ACCESSES;
    int size_kb = 1, n_ways = 4;
    int total_bytes = size_kb * 1024;
    int n_sets = total_bytes / (n_ways * 64);
    if (n_sets < 1) n_sets = 1;
    int p2 = 1;
    while (p2 * 2 <= n_sets) p2 <<= 1;
    n_sets = p2;

    /* Zipfian run */
    uint64_t *zip = workload_zipfian(n, 42, 1.0);
    SetAssocCache *c1 = set_cache_create(n_sets, n_ways, 64, POLICY_LRU);
    for (size_t i = 0; i < n; i++) set_cache_access(c1, zip[i]);
    double hr_zip = 100.0 * (double)c1->hits / (double)(c1->hits + c1->misses);
    set_cache_destroy(c1);
    free(zip);

    /* Random run */
    uint64_t *rnd = workload_random(n, 42);
    SetAssocCache *c2 = set_cache_create(n_sets, n_ways, 64, POLICY_LRU);
    for (size_t i = 0; i < n; i++) set_cache_access(c2, rnd[i]);
    double hr_rnd = 100.0 * (double)c2->hits / (double)(c2->hits + c2->misses);
    set_cache_destroy(c2);
    free(rnd);

    printf("  Zipfian hit rate : %.2f%%\n", hr_zip);
    printf("  Random  hit rate : %.2f%%\n", hr_rnd);
    ASSERT(hr_zip > hr_rnd,
           "Zipfian hit rate > Random hit rate (hot-set exploited by LRU)");
}

/* ------------------------------------------------------------------ */
/* Test 8: All policy_matrix cells in [0, 100]                        */
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
/* Test 9: CSV file created and non-empty                              */
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
/* Test 10: Interview Q -- why LFU worse than LRU on sequential scan  */
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
    test_zipfian();
    test_mixed();
    test_build_all();
    test_size_sweep_sequential();
    test_zipf_beats_random();
    test_policy_matrix_valid();
    test_csv_output();
    test_lfu_vs_lru_sequential();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
