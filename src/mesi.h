/*
 * mesi.h -- MESI Cache Coherence Protocol
 *
 * Models a snooping-based MESI protocol for a shared-bus 2-core system.
 *
 * States per cache line:
 *   INVALID   (I) -- Line not present / stale. Must fetch before use.
 *   SHARED    (S) -- Clean copy. One or more cores may have it. Read-only.
 *   EXCLUSIVE (E) -- Clean copy. Only this core has it. Can upgrade to M on write.
 *   MODIFIED  (M) -- Dirty copy. Only this core has it. Must write-back on eviction.
 *
 * Transitions (2-core snooping bus):
 *
 *   Local read (PrRd):
 *     I --> (bus read) --> E if no other sharers, else S
 *     S --> S (already shared, no bus traffic)
 *     E --> E (exclusive, no bus traffic)
 *     M --> M (dirty, no bus traffic)
 *
 *   Local write (PrWr):
 *     I --> (bus read-for-ownership) --> M
 *     S --> (bus invalidate) --> M
 *     E --> M (silent upgrade, no bus traffic)
 *     M --> M (already dirty, no bus traffic)
 *
 *   Snooped read from another core (BusRd):
 *     I --> I (not present, ignore)
 *     S --> S (share is fine)
 *     E --> S (must share now, downgrade from exclusive)
 *     M --> S (must write-back dirty data, then share)
 *
 *   Snooped write/invalidate from another core (BusRdX / BusUpgr):
 *     I --> I (not present, ignore)
 *     S --> I (another core is taking ownership, invalidate)
 *     E --> I (another core is taking ownership, invalidate)
 *     M --> I (another core is taking ownership; write-back + invalidate)
 *
 * Day 9 -- CPU Cache Replacement Simulator
 */

#ifndef MESI_H
#define MESI_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* MESI state enum                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    MESI_INVALID   = 0,
    MESI_SHARED    = 1,
    MESI_EXCLUSIVE = 2,
    MESI_MODIFIED  = 3
} MESIState;

/* ------------------------------------------------------------------ */
/* One cache line with MESI state                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t  tag;
    int       valid;
    MESIState state;
} MESILine;

/* ------------------------------------------------------------------ */
/* One core's cache (direct-mapped for simplicity)                      */
/*   -- associativity not needed to demonstrate MESI state machine     */
/* ------------------------------------------------------------------ */
typedef struct {
    MESILine *lines;    /* array[n_sets] */
    int       n_sets;
    int       line_size;
    int       core_id;

    /* Counters */
    uint64_t reads;
    uint64_t writes;
    uint64_t read_hits;
    uint64_t write_hits;
    uint64_t writebacks;        /* dirty lines written back to memory */
    uint64_t invalidations;     /* times a line was snooped-invalidated */
    uint64_t upgrades;          /* E->M silent upgrades */
} MESICache;

/* ------------------------------------------------------------------ */
/* 2-core shared-bus system                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    MESICache *core[2];

    /* Event log */
    int   log_enabled;          /* 1 = print transitions to stdout */

    /* Global bus stats */
    uint64_t bus_reads;         /* BusRd transactions */
    uint64_t bus_readx;         /* BusRdX (read-for-ownership) transactions */
    uint64_t bus_invalidates;   /* BusUpgr (invalidate) transactions */
    uint64_t bus_writebacks;    /* writeback transactions to memory */
} MESISystem;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/** Create a single-core MESI cache (direct-mapped). */
MESICache *mesi_cache_create(int n_sets, int line_size, int core_id);

/** Free a single-core cache. */
void mesi_cache_destroy(MESICache *c);

/** Create a 2-core MESI system sharing a bus. */
MESISystem *mesi_system_create(int n_sets, int line_size);

/** Free the system. */
void mesi_system_destroy(MESISystem *sys);

/**
 * Core `core_id` performs a READ to `address`.
 * Updates MESI states on both cores via bus snooping.
 * Returns the final MESIState of the line on the requesting core.
 */
MESIState mesi_read(MESISystem *sys, int core_id, uint64_t address);

/**
 * Core `core_id` performs a WRITE to `address`.
 * Updates MESI states on both cores via bus snooping.
 * Returns the final MESIState of the line on the requesting core.
 */
MESIState mesi_write(MESISystem *sys, int core_id, uint64_t address);

/** Return human-readable state name. */
const char *mesi_state_str(MESIState s);

/** Print per-core statistics. */
void mesi_print_stats(const MESISystem *sys);

/* ------------------------------------------------------------------ */
/* Address decode helpers (direct-mapped)                               */
/* ------------------------------------------------------------------ */
static inline int mesi_set_index(const MESICache *c, uint64_t addr)
{
    /* offset_bits = log2(line_size), but we use runtime division for clarity */
    int offset = 0, ls = c->line_size;
    while (ls > 1) { offset++; ls >>= 1; }
    return (int)((addr >> offset) % (uint64_t)c->n_sets);
}

static inline uint64_t mesi_tag(const MESICache *c, uint64_t addr)
{
    int offset = 0, ls = c->line_size;
    while (ls > 1) { offset++; ls >>= 1; }
    int idx_bits = 0, ns = c->n_sets;
    while (ns > 1) { idx_bits++; ns >>= 1; }
    return addr >> (offset + idx_bits);
}

#endif /* MESI_H */
