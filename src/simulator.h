/*
 * simulator.h -- Trace-Driven Cache Simulator Core
 *
 * Wires a TraceReader to a SetAssocCache (or the unified Cache interface).
 * Replays each memory access from a trace file through the cache model
 * and collects detailed statistics.
 *
 * Usage:
 *   SimConfig cfg = {
 *       .n_sets    = 16,
 *       .n_ways    = 4,
 *       .line_size = 64,
 *       .policy    = POLICY_LRU,
 *       .trace_path = "traces/my_prog.trace"
 *   };
 *   SimResult result = simulator_run(&cfg);
 *   simulator_print_report(&result, &cfg);
 *
 *  -- CPU Cache Replacement Simulator
 */

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdint.h>
#include "cache.h"      /* CachePolicy */

/* ------------------------------------------------------------------ */
/* Simulation configuration                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    int         n_sets;
    int         n_ways;
    int         line_size;
    CachePolicy policy;
    const char *trace_path;

    /* Optional: separate instruction / data cache simulation */
    int         count_instruction_fetches;  /* 1=yes (default), 0=skip I fetches */
    int         count_writes;              /* 1=yes (default), 0=skip writes */
} SimConfig;

/* ------------------------------------------------------------------ */
/* Simulation results                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t total_accesses;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t read_accesses;
    uint64_t write_accesses;
    uint64_t modify_accesses;  /* Valgrind 'M': counts as 2 total_accesses */
    uint64_t read_hits;
    uint64_t write_hits;
    double   hit_rate;         /* hits / total_accesses * 100 */
    double   miss_rate;

    /* Trace reader stats */
    uint64_t trace_lines_read;
    uint64_t trace_records_parsed;
    uint64_t trace_lines_skipped;

    int      success;          /* 0 if trace file could not be opened */
} SimResult;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * Run a full trace-driven simulation.
 * Opens the trace file, replays every access through a fresh cache,
 * collects stats, and returns the result.
 */
SimResult simulator_run(const SimConfig *cfg);

/**
 * Print a formatted simulation report to stdout.
 */
void simulator_print_report(const SimResult *r, const SimConfig *cfg);

/**
 * Compare two simulation results side by side (same trace, different configs).
 * Useful for policy comparison tables.
 */
void simulator_print_comparison(
    const SimResult *results,
    const SimConfig *configs,
    int count);

#endif /* SIMULATOR_H */
