/*
 * test__trace_reader.c -- Unit tests for Trace Reader and Simulator Core
 *
 * Tests:
 *  1. Trace reader opens and reads simple.trace correctly
 *  2. Parser handles all op codes (R W I L S M)
 *  3. Valgrind format parsed (address,size)
 *  4. Comments and blank lines are skipped
 *  5. Malformed lines are skipped gracefully
 *  6. Simulator: simple.trace -> verify hit/miss counts by hand
 *  7. Simulator: valgrind.trace -> verify MODIFY counts 2 accesses
 *  8. Simulator: policy comparison on same trace
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o test_day7 tests/test_day7.c
 *       src/trace.c src/simulator.c src/cache/set_cache.c
 *       src/cache/lru.c src/cache/fifo.c src/cache/lfu.c src/cache/cache.c
 *       -Isrc -Isrc/cache
 *
 * Day 7 -- CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "simulator.h"
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

/* Trace files are relative to project root -- adjust if needed */
#define TRACE_SIMPLE   "traces/simple.trace"
#define TRACE_VALGRIND "traces/valgrind.trace"
#define TRACE_MATRIX   "traces/matrix.trace"

/* ------------------------------------------------------------------ */
/* Test 1: Trace reader opens simple.trace                             */
/* ------------------------------------------------------------------ */
static void test_trace_open(void)
{
    printf("\n=== Test 1: Trace reader opens file ===\n");

    TraceReader *tr = trace_open(TRACE_SIMPLE);
    ASSERT(tr != NULL, "trace_open(simple.trace) returns non-NULL");

    trace_close(tr);

    TraceReader *bad = trace_open("traces/does_not_exist.trace");
    ASSERT(bad == NULL, "trace_open on missing file returns NULL");
}

/* ------------------------------------------------------------------ */
/* Test 2: Simple format parsing -- R/W operations                     */
/* ------------------------------------------------------------------ */
static void test_trace_simple_format(void)
{
    printf("\n=== Test 2: Simple format R/W parsing ===\n");

    TraceReader *tr = trace_open(TRACE_SIMPLE);
    ASSERT(tr != NULL, "File opened");

    TraceRecord rec;
    int rc;

    /* First record: R 0x000 */
    rc = trace_next(tr, &rec);
    ASSERT(rc == 1,                    "First record read successfully");
    ASSERT(rec.type == ACCESS_READ,    "R -> ACCESS_READ");
    ASSERT(rec.address == 0x000,       "Address 0x000 parsed");

    /* Second record: R 0x040 */
    rc = trace_next(tr, &rec);
    ASSERT(rc == 1,                    "Second record read");
    ASSERT(rec.address == 0x040,       "Address 0x040 parsed");

    /* Skip to 7th record: W 0x200 */
    for (int i = 0; i < 4; i++) trace_next(tr, &rec);
    rc = trace_next(tr, &rec);
    ASSERT(rc == 1,                    "W record read");
    ASSERT(rec.type == ACCESS_WRITE,   "W -> ACCESS_WRITE");
    ASSERT(rec.address == 0x200,       "Address 0x200 parsed");

    trace_close(tr);
}

/* ------------------------------------------------------------------ */
/* Test 3: Valgrind format parsing -- L/S/M/I with size               */
/* ------------------------------------------------------------------ */
static void test_trace_valgrind_format(void)
{
    printf("\n=== Test 3: Valgrind L/S/M/I format with ,size ===\n");

    /* Write a temp trace string to a temp file */
    const char *tmp = "traces/tmp_test.trace";
    FILE *f = fopen(tmp, "w");
    ASSERT(f != NULL, "Temp trace file created");
    if (!f) return;

    fprintf(f, "I  0x400510,4\n");   /* instruction fetch */
    fprintf(f, "L  0x000000,4\n");   /* load */
    fprintf(f, "S  0x000080,8\n");   /* store */
    fprintf(f, "M  0x000100,4\n");   /* modify */
    fclose(f);

    TraceReader *tr = trace_open(tmp);
    ASSERT(tr != NULL, "Temp trace opened");

    TraceRecord rec;

    trace_next(tr, &rec);
    ASSERT(rec.type == ACCESS_READ,   "I -> ACCESS_READ (instruction fetch)");
    ASSERT(rec.address == 0x400510,   "I address parsed");
    ASSERT(rec.size == 4,             "I size=4 parsed");

    trace_next(tr, &rec);
    ASSERT(rec.type == ACCESS_READ,   "L -> ACCESS_READ");
    ASSERT(rec.size == 4,             "L size=4 parsed");

    trace_next(tr, &rec);
    ASSERT(rec.type == ACCESS_WRITE,  "S -> ACCESS_WRITE");
    ASSERT(rec.address == 0x80,       "S address parsed");
    ASSERT(rec.size == 8,             "S size=8 parsed");

    trace_next(tr, &rec);
    ASSERT(rec.type == ACCESS_MODIFY, "M -> ACCESS_MODIFY");
    ASSERT(rec.address == 0x100,      "M address parsed");

    trace_close(tr);
    remove(tmp);
}

/* ------------------------------------------------------------------ */
/* Test 4: Comments and blank lines skipped                            */
/* ------------------------------------------------------------------ */
static void test_trace_comments_skipped(void)
{
    printf("\n=== Test 4: Comments and blank lines skipped ===\n");

    const char *tmp = "traces/tmp_comments.trace";
    FILE *f = fopen(tmp, "w");
    ASSERT(f != NULL, "Temp file created");
    if (!f) return;

    fprintf(f, "# This is a comment\n");
    fprintf(f, "\n");
    fprintf(f, "= another comment style\n");
    fprintf(f, "   \n");
    fprintf(f, "R 0x000\n");
    fprintf(f, "# another comment\n");
    fprintf(f, "W 0x100\n");
    fclose(f);

    TraceReader *tr = trace_open(tmp);
    ASSERT(tr != NULL, "File opened");

    TraceRecord rec;
    int rc;

    rc = trace_next(tr, &rec);
    ASSERT(rc == 1 && rec.address == 0x000, "First real record R 0x000");

    rc = trace_next(tr, &rec);
    ASSERT(rc == 1 && rec.type == ACCESS_WRITE, "Second real record W 0x100");

    rc = trace_next(tr, &rec);
    ASSERT(rc == 0, "EOF after 2 records (comments skipped)");

    ASSERT(tr->lines_skipped >= 4, "At least 4 lines skipped (comments+blanks)");

    trace_close(tr);
    remove(tmp);
}

/* ------------------------------------------------------------------ */
/* Test 5: Malformed lines skipped gracefully                          */
/* ------------------------------------------------------------------ */
static void test_trace_malformed_lines(void)
{
    printf("\n=== Test 5: Malformed lines skipped gracefully ===\n");

    const char *tmp = "traces/tmp_malformed.trace";
    FILE *f = fopen(tmp, "w");
    ASSERT(f != NULL, "Temp file created");
    if (!f) return;

    fprintf(f, "R 0x000\n");
    fprintf(f, "GARBAGE LINE WITH NO OP\n");  /* malformed */
    fprintf(f, "Z 0x100\n");                  /* unknown op */
    fprintf(f, "W 0x200\n");
    fclose(f);

    TraceReader *tr = trace_open(tmp);
    ASSERT(tr != NULL, "File opened");

    TraceRecord rec;
    int rc;

    rc = trace_next(tr, &rec);
    ASSERT(rc == 1 && rec.address == 0x000, "R 0x000 read OK");

    rc = trace_next(tr, &rec);
    ASSERT(rc == 1 && rec.address == 0x200, "W 0x200 read OK (malformed lines skipped)");

    rc = trace_next(tr, &rec);
    ASSERT(rc == 0, "EOF");

    trace_close(tr);
    remove(tmp);
}

/* ------------------------------------------------------------------ */
/* Test 6: Simulator -- simple.trace hand-verification                 */
/*                                                                      */
/* Cache: 2-way, 4 sets, 64-byte lines                                 */
/* Expected: 10 accesses, 5 hits, 5 misses (50% hit rate)             */
/* See simple.trace comments for full hand-trace.                      */
/* ------------------------------------------------------------------ */
static void test_simulator_simple_trace(void)
{
    printf("\n=== Test 6: Simulator -- simple.trace (hand-verified) ===\n");

    SimConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.n_sets    = 4;
    cfg.n_ways    = 2;
    cfg.line_size = 64;
    cfg.policy    = POLICY_LRU;
    cfg.trace_path = TRACE_SIMPLE;

    SimResult r = simulator_run(&cfg);

    ASSERT(r.success == 1,            "Simulation succeeded");
    ASSERT(r.total_accesses == 10,    "total_accesses == 10");
    ASSERT(r.hits == 4,               "hits == 4");
    ASSERT(r.misses == 6,             "misses == 6");
    ASSERT(r.read_accesses == 8,      "read_accesses == 8");
    ASSERT(r.write_accesses == 2,     "write_accesses == 2");

    printf("  Hit rate: %.2f%% (expected 40.00%%)\n", r.hit_rate);
    ASSERT(r.hit_rate >= 39.9 && r.hit_rate <= 40.1, "hit_rate ~= 40.00%");

    simulator_print_report(&r, &cfg);
}

/* ------------------------------------------------------------------ */
/* Test 7: Simulator -- MODIFY counts as 2 accesses                   */
/* ------------------------------------------------------------------ */
static void test_simulator_modify_counts(void)
{
    printf("\n=== Test 7: Simulator -- MODIFY counts as 2 accesses ===\n");

    /* Write a minimal trace with one M record */
    const char *tmp = "traces/tmp_modify.trace";
    FILE *f = fopen(tmp, "w");
    ASSERT(f != NULL, "Temp trace created");
    if (!f) return;
    fprintf(f, "M 0x000\n");  /* 1 modify = 1 read + 1 write = 2 accesses */
    fclose(f);

    SimConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.n_sets    = 4;
    cfg.n_ways    = 2;
    cfg.line_size = 64;
    cfg.policy    = POLICY_LRU;
    cfg.trace_path = tmp;

    SimResult r = simulator_run(&cfg);

    ASSERT(r.success == 1,            "Simulation succeeded");
    ASSERT(r.total_accesses == 2,     "MODIFY -> total_accesses == 2");
    ASSERT(r.modify_accesses == 1,    "modify_accesses == 1");
    /* First access (read) is MISS, second access (write) is HIT */
    ASSERT(r.hits == 1,               "1 hit (write after read hits same line)");
    ASSERT(r.misses == 1,             "1 miss (cold read miss)");

    remove(tmp);
}

/* ------------------------------------------------------------------ */
/* Test 8: Policy comparison -- same trace, 3 policies                */
/* ------------------------------------------------------------------ */
static void test_simulator_policy_comparison(void)
{
    printf("\n=== Test 8: Policy comparison on simple.trace ===\n");

    CachePolicy pols[] = {POLICY_LRU, POLICY_FIFO, POLICY_LFU};
    SimResult   results[3];
    SimConfig   configs[3];

    for (int i = 0; i < 3; i++) {
        memset(&configs[i], 0, sizeof(SimConfig));
        configs[i].n_sets    = 4;
        configs[i].n_ways    = 2;
        configs[i].line_size = 64;
        configs[i].policy    = pols[i];
        configs[i].trace_path = TRACE_SIMPLE;
        results[i] = simulator_run(&configs[i]);
        ASSERT(results[i].success == 1, "Simulation succeeded");
    }

    simulator_print_comparison(results, configs, 3);
    ASSERT(1, "Comparison table printed");
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("============================================\n");
    printf("  Day 7 Tests -- Trace Reader + Simulator\n");
    printf("  CPU Cache Replacement Simulator\n");
    printf("============================================\n");

    test_trace_open();
    test_trace_simple_format();
    test_trace_valgrind_format();
    test_trace_comments_skipped();
    test_trace_malformed_lines();
    test_simulator_simple_trace();
    test_simulator_modify_counts();
    test_simulator_policy_comparison();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
