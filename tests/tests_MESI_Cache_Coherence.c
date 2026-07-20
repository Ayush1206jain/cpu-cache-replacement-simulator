/*
 * test_day9.c -- Unit tests for MESI Cache Coherence Protocol
 *
 * Tests every MESI state transition systematically:
 *
 *   PrRd (local read):
 *    1. I -> E : cold read, no sharer        (exclusive)
 *    2. I -> S : cold read, other has E/S    (shared)
 *    3. E -> E : read hit, already exclusive
 *    4. S -> S : read hit, already shared
 *    5. M -> M : read hit, already modified
 *    6. M->S   : snooped BusRd while in Modified (writeback + share)
 *
 *   PrWr (local write):
 *    7. I -> M : write miss, BusRdX
 *    8. S -> M : write hit upgrade, BusUpgr (invalidates other)
 *    9. E -> M : silent upgrade, no bus traffic
 *   10. M -> M : write hit, already modified
 *
 *   Interview Q: Core A has M state, Core B reads -> both end in S
 *   False sharing scenario
 *
 * Build:
 *   gcc -Wall -Wextra -std=c11 -O2 -o test_day9 tests/test_day9.c
 *       src/mesi.c -Isrc
 *
 * Day 9 -- CPU Cache Replacement Simulator
 */

#include <stdio.h>
#include <stdlib.h>
#include "mesi.h"

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

/* Convenience: turn off log during setup, re-enable for tested op */
#define LOG_OFF(sys) ((sys)->log_enabled = 0)
#define LOG_ON(sys)  ((sys)->log_enabled = 1)

/* Get MESI state of address on core */
static MESIState state_of(MESISystem *sys, int core, uint64_t addr)
{
    MESICache *c   = sys->core[core];
    MESILine  *l   = &c->lines[mesi_set_index(c, addr)];
    uint64_t   tag = mesi_tag(c, addr);
    if (l->valid && l->tag == tag) return l->state;
    return MESI_INVALID;
}

/* ------------------------------------------------------------------ */
/* Test 1: PrRd on cold line (no sharer) -> Exclusive                 */
/* ------------------------------------------------------------------ */
static void test_read_cold_exclusive(void)
{
    printf("\n=== Test 1: PrRd cold (no sharer) -> Exclusive ===\n");

    MESISystem *sys = mesi_system_create(4, 64);
    LOG_ON(sys);

    MESIState s = mesi_read(sys, 0, 0x000);
    ASSERT(s == MESI_EXCLUSIVE, "Core0 read cold -> E (Exclusive)");
    ASSERT(state_of(sys, 0, 0x000) == MESI_EXCLUSIVE, "Core0 line state == E");
    ASSERT(state_of(sys, 1, 0x000) == MESI_INVALID,   "Core1 line state == I (no line)");
    ASSERT(sys->bus_reads == 1, "BusRd issued == 1");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 2: PrRd when other core has Exclusive -> both go Shared       */
/* ------------------------------------------------------------------ */
static void test_read_other_has_exclusive(void)
{
    printf("\n=== Test 2: PrRd -- other core has E -> both go S ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    /* Core 0 reads first -> gets E */
    LOG_OFF(sys);
    mesi_read(sys, 0, 0x000);
    LOG_ON(sys);

    ASSERT(state_of(sys, 0, 0x000) == MESI_EXCLUSIVE, "Setup: Core0 has E");

    /* Core 1 reads same address -> both go S */
    MESIState s = mesi_read(sys, 1, 0x000);
    ASSERT(s == MESI_SHARED, "Core1 read -> S (shares with Core0)");
    ASSERT(state_of(sys, 0, 0x000) == MESI_SHARED, "Core0 downgraded E->S");
    ASSERT(state_of(sys, 1, 0x000) == MESI_SHARED, "Core1 gets S");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 3: PrRd hit on Exclusive -> stays Exclusive                   */
/* ------------------------------------------------------------------ */
static void test_read_hit_exclusive(void)
{
    printf("\n=== Test 3: PrRd HIT on Exclusive -> stays E ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    LOG_OFF(sys);
    mesi_read(sys, 0, 0x000);   /* Core0 -> E */
    LOG_ON(sys);

    uint64_t br_before = sys->bus_reads;
    MESIState s = mesi_read(sys, 0, 0x000);
    ASSERT(s == MESI_EXCLUSIVE, "Read HIT on E -> still E");
    ASSERT(sys->bus_reads == br_before, "No BusRd on E read hit");
    ASSERT(sys->core[0]->read_hits == 1, "read_hits == 1");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 4: PrRd hit on Shared -> stays Shared                         */
/* ------------------------------------------------------------------ */
static void test_read_hit_shared(void)
{
    printf("\n=== Test 4: PrRd HIT on Shared -> stays S ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    LOG_OFF(sys);
    mesi_read(sys, 0, 0x000);  /* Core0 -> E */
    mesi_read(sys, 1, 0x000);  /* Core1 -> S, Core0 -> S */
    LOG_ON(sys);

    uint64_t br_before = sys->bus_reads;
    MESIState s = mesi_read(sys, 0, 0x000);   /* Core0 read again */
    ASSERT(s == MESI_SHARED, "Read HIT on S -> still S");
    ASSERT(sys->bus_reads == br_before, "No BusRd on S read hit");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 5: PrRd hit on Modified -> stays Modified                     */
/* ------------------------------------------------------------------ */
static void test_read_hit_modified(void)
{
    printf("\n=== Test 5: PrRd HIT on Modified -> stays M ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    LOG_OFF(sys);
    mesi_write(sys, 0, 0x000);  /* Core0 -> M */
    LOG_ON(sys);

    uint64_t br_before = sys->bus_reads;
    MESIState s = mesi_read(sys, 0, 0x000);
    ASSERT(s == MESI_MODIFIED, "Read HIT on M -> still M");
    ASSERT(sys->bus_reads == br_before, "No BusRd on M read hit");
    ASSERT(sys->core[0]->read_hits == 1, "read_hits == 1");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 6: INTERVIEW Q: Core A has M, Core B reads                    */
/*          -> A: M->S (writeback), B: I->S                           */
/* ------------------------------------------------------------------ */
static void test_modified_snooped_read(void)
{
    printf("\n=== Test 6: INTERVIEW Q: Core A (M) snooped by Core B read ===\n");
    printf("  Scenario: Core A has Modified line. Core B reads same address.\n");

    MESISystem *sys = mesi_system_create(4, 64);

    /* Setup: Core 0 writes -> gets M */
    LOG_OFF(sys);
    mesi_write(sys, 0, 0x000);
    LOG_ON(sys);

    ASSERT(state_of(sys, 0, 0x000) == MESI_MODIFIED, "Setup: Core0 has M");
    ASSERT(state_of(sys, 1, 0x000) == MESI_INVALID,  "Setup: Core1 has I");

    uint64_t wb_before = sys->bus_writebacks;

    printf("  --- Core 1 issues read to 0x000 ---\n");
    MESIState s1 = mesi_read(sys, 1, 0x000);

    ASSERT(state_of(sys, 0, 0x000) == MESI_SHARED,  "Core0: M->S (writeback+share)");
    ASSERT(s1 == MESI_SHARED,                        "Core1: I->S (gets shared copy)");
    ASSERT(sys->bus_writebacks == wb_before + 1,     "Writeback issued (dirty->mem)");
    ASSERT(sys->core[0]->writebacks == 1,            "Core0 writeback count == 1");

    printf("\n  Final states:\n");
    printf("    Core0: %s\n", mesi_state_str(state_of(sys, 0, 0x000)));
    printf("    Core1: %s\n", mesi_state_str(state_of(sys, 1, 0x000)));

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 7: PrWr miss (I -> M) with BusRdX                            */
/* ------------------------------------------------------------------ */
static void test_write_cold(void)
{
    printf("\n=== Test 7: PrWr cold (I->M) -- BusRdX ===\n");

    MESISystem *sys = mesi_system_create(4, 64);
    LOG_ON(sys);

    MESIState s = mesi_write(sys, 0, 0x000);
    ASSERT(s == MESI_MODIFIED, "Core0 write cold -> M");
    ASSERT(state_of(sys, 0, 0x000) == MESI_MODIFIED, "Core0 line == M");
    ASSERT(state_of(sys, 1, 0x000) == MESI_INVALID,  "Core1 line == I");
    ASSERT(sys->bus_readx == 1, "BusRdX issued == 1");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 8: PrWr on Shared -> M, other invalidated (BusUpgr)          */
/* ------------------------------------------------------------------ */
static void test_write_shared_upgrade(void)
{
    printf("\n=== Test 8: PrWr on S -> M, BusUpgr invalidates other ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    /* Setup: both cores share the line */
    LOG_OFF(sys);
    mesi_read(sys, 0, 0x000);  /* Core0 -> E */
    mesi_read(sys, 1, 0x000);  /* Core1 -> S, Core0 -> S */
    LOG_ON(sys);

    ASSERT(state_of(sys, 0, 0x000) == MESI_SHARED, "Setup: Core0 has S");
    ASSERT(state_of(sys, 1, 0x000) == MESI_SHARED, "Setup: Core1 has S");

    printf("  --- Core 0 writes to 0x000 ---\n");
    MESIState s = mesi_write(sys, 0, 0x000);

    ASSERT(s == MESI_MODIFIED, "Core0: S->M (after BusUpgr)");
    ASSERT(state_of(sys, 1, 0x000) == MESI_INVALID, "Core1: S->I (invalidated)");
    ASSERT(sys->bus_invalidates == 1, "BusUpgr issued == 1");
    ASSERT(sys->core[1]->invalidations == 1, "Core1 invalidation count == 1");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 9: PrWr on Exclusive -> M (silent upgrade, no bus)            */
/* ------------------------------------------------------------------ */
static void test_write_exclusive_silent_upgrade(void)
{
    printf("\n=== Test 9: PrWr on E -> M (silent upgrade, no bus) ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    LOG_OFF(sys);
    mesi_read(sys, 0, 0x000);  /* Core0 -> E */
    LOG_ON(sys);

    uint64_t bus_before = sys->bus_reads + sys->bus_readx + sys->bus_invalidates;

    printf("  --- Core 0 writes (has E) ---\n");
    MESIState s = mesi_write(sys, 0, 0x000);

    uint64_t bus_after = sys->bus_reads + sys->bus_readx + sys->bus_invalidates;

    ASSERT(s == MESI_MODIFIED, "Core0: E->M (silent upgrade)");
    ASSERT(bus_after == bus_before, "No bus traffic on E->M upgrade");
    ASSERT(sys->core[0]->upgrades == 1, "upgrades count == 1");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 10: PrWr on Modified -> M (already owner, no bus)            */
/* ------------------------------------------------------------------ */
static void test_write_modified_noop(void)
{
    printf("\n=== Test 10: PrWr on M -> M (already owner) ===\n");

    MESISystem *sys = mesi_system_create(4, 64);

    LOG_OFF(sys);
    mesi_write(sys, 0, 0x000);   /* Core0 -> M */
    LOG_ON(sys);

    uint64_t bus_before = sys->bus_reads + sys->bus_readx + sys->bus_invalidates;
    uint64_t wh_before  = sys->core[0]->write_hits;

    printf("  --- Core 0 writes again (has M) ---\n");
    MESIState s = mesi_write(sys, 0, 0x000);

    ASSERT(s == MESI_MODIFIED, "Core0: M->M (already owner)");
    ASSERT(sys->bus_reads + sys->bus_readx + sys->bus_invalidates == bus_before,
           "No bus traffic on M->M write");
    ASSERT(sys->core[0]->write_hits == wh_before + 1,
           "write_hits incremented");

    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 11: False sharing scenario                                      */
/*                                                                      */
/* Core 0 and Core 1 both write to DIFFERENT variables that live      */
/* in the SAME cache line. This causes unnecessary invalidations.      */
/*                                                                      */
/* Example: int a, b in same 64-byte line                             */
/*   Core0 writes a (0x000) -> M                                       */
/*   Core1 writes b (0x004) -> same set! -> invalidates Core0's line  */
/*   Core0 writes a again -> must re-fetch (unnecessary!)             */
/* ------------------------------------------------------------------ */
static void test_false_sharing(void)
{
    printf("\n=== Test 11: False Sharing -- two cores, same cache line ===\n");
    printf("  Core0 writes 0x000 (var a), Core1 writes 0x004 (var b).\n");
    printf("  Both land in the SAME cache line -> conflict!\n\n");

    MESISystem *sys = mesi_system_create(4, 64);   /* 64-byte lines */
    LOG_ON(sys);

    /* 0x000 and 0x004 are both within the same 64-byte cache line */
    /* set_index = (0x000 >> 6) & 3 = 0 for both addresses */

    printf("  --- Core0 writes 0x000 (var a) ---\n");
    mesi_write(sys, 0, 0x000);    /* Core0: I->M */

    printf("  --- Core1 writes 0x004 (var b, same line!) ---\n");
    mesi_write(sys, 1, 0x004);    /* Core1: I->M, Core0: M->I (invalidated!) */

    printf("  --- Core0 writes 0x000 again (var a) ---\n");
    mesi_write(sys, 0, 0x000);    /* Core0: I->M again -- must re-fetch! */

    ASSERT(sys->core[0]->invalidations >= 1,
           "Core0 was invalidated at least once (false sharing)");
    ASSERT(sys->bus_writebacks >= 1,
           "At least 1 writeback occurred (dirty line evicted by false share)");

    printf("\n  False sharing causes %llu unnecessary invalidation(s)\n",
           (unsigned long long)sys->core[0]->invalidations);
    printf("  and %llu writeback(s) to memory.\n\n",
           (unsigned long long)sys->bus_writebacks);

    mesi_print_stats(sys);
    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Test 12: Full sequence -- Core A writes, Core B reads, A writes     */
/* ------------------------------------------------------------------ */
static void test_full_sequence(void)
{
    printf("\n=== Test 12: Full 2-core sequence ===\n");

    MESISystem *sys = mesi_system_create(4, 64);
    LOG_ON(sys);

    printf("  Step 1: Core0 reads  0x000 -> I->E\n");
    mesi_read(sys, 0, 0x000);
    ASSERT(state_of(sys, 0, 0x000) == MESI_EXCLUSIVE, "Core0: E");

    printf("  Step 2: Core0 writes 0x000 -> E->M (silent upgrade)\n");
    mesi_write(sys, 0, 0x000);
    ASSERT(state_of(sys, 0, 0x000) == MESI_MODIFIED, "Core0: M");

    printf("  Step 3: Core1 reads  0x000 -> Core0 M->S, Core1 I->S\n");
    mesi_read(sys, 1, 0x000);
    ASSERT(state_of(sys, 0, 0x000) == MESI_SHARED, "Core0: M->S");
    ASSERT(state_of(sys, 1, 0x000) == MESI_SHARED, "Core1: I->S");

    printf("  Step 4: Core1 writes 0x000 -> Core1 S->M, Core0 S->I\n");
    mesi_write(sys, 1, 0x000);
    ASSERT(state_of(sys, 1, 0x000) == MESI_MODIFIED, "Core1: S->M");
    ASSERT(state_of(sys, 0, 0x000) == MESI_INVALID,  "Core0: S->I (invalidated)");

    printf("  Step 5: Core0 reads  0x000 -> Core1 M->S, Core0 I->S\n");
    mesi_read(sys, 0, 0x000);
    ASSERT(state_of(sys, 0, 0x000) == MESI_SHARED, "Core0: I->S");
    ASSERT(state_of(sys, 1, 0x000) == MESI_SHARED, "Core1: M->S (writeback)");

    mesi_print_stats(sys);
    mesi_system_destroy(sys);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    printf("============================================\n");
    printf("  Day 9 Tests -- MESI Coherence Protocol\n");
    printf("  CPU Cache Replacement Simulator\n");
    printf("============================================\n");

    test_read_cold_exclusive();
    test_read_other_has_exclusive();
    test_read_hit_exclusive();
    test_read_hit_shared();
    test_read_hit_modified();
    test_modified_snooped_read();
    test_write_cold();
    test_write_shared_upgrade();
    test_write_exclusive_silent_upgrade();
    test_write_modified_noop();
    test_false_sharing();
    test_full_sequence();

    printf("\n============================================\n");
    printf("  Results: %d / %d tests passed\n", tests_passed, tests_run);
    printf("============================================\n");

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
