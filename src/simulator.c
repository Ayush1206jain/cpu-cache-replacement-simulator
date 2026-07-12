/*
 * simulator.c -- Trace-Driven Cache Simulator Core Implementation
 *
 * Core loop:
 *   for each record in trace:
 *     set_cache_access(cache, record.address)   --> HIT or MISS
 *     if MODIFY: access again (read-then-write)
 *     accumulate per-type stats
 *
 * Day  -- CPU Cache Replacement Simulator
 */

#include "simulator.h"
#include "trace.h"
#include "set_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* simulator_run                                                         */
/* ------------------------------------------------------------------ */
SimResult simulator_run(const SimConfig *cfg)
{
    SimResult r;
    memset(&r, 0, sizeof(r));

    if (!cfg || !cfg->trace_path) {
        fprintf(stderr, "[sim] No trace path specified.\n");
        r.success = 0;
        return r;
    }

    /* Create fresh cache */
    SetAssocCache *cache = set_cache_create(cfg->n_sets, cfg->n_ways,
                                            cfg->line_size, cfg->policy);
    if (!cache) {
        fprintf(stderr, "[sim] Failed to create cache.\n");
        r.success = 0;
        return r;
    }

    /* Open trace */
    TraceReader *tr = trace_open(cfg->trace_path);
    if (!tr) {
        set_cache_destroy(cache);
        r.success = 0;
        return r;
    }

    /* Default: simulate all access types */
    int sim_writes  = (cfg->count_writes != 0) ? 1 : 0;

    /* If user left them as 0 (zero-init SimConfig), default to 1 */
    if (cfg->count_writes == 0 && cfg->count_instruction_fetches == 0)
        sim_writes = 1;

    /* ---- Main simulation loop ---- */
    TraceRecord rec;
    while (trace_next(tr, &rec) == 1) {

        /* Decide whether to simulate this access type */
        if (rec.type == ACCESS_WRITE && !sim_writes)  continue;

        int result1, result2 = CACHE_MISS;
        int accesses_this_record = 1;

        if (rec.type == ACCESS_MODIFY) {
            /* MODIFY = read access + write access */
            result1 = set_cache_access(cache, rec.address);   /* read */
            result2 = set_cache_access(cache, rec.address);   /* write */
            accesses_this_record = 2;
            r.modify_accesses++;
            r.read_accesses++;
            r.write_accesses++;
        } else {
            result1 = set_cache_access(cache, rec.address);
            if (rec.type == ACCESS_READ) {
                r.read_accesses++;
            } else {
                r.write_accesses++;
            }
        }

        /* Count hits / misses */
        r.total_accesses += (uint64_t)accesses_this_record;

        if (result1 == CACHE_HIT) {
            r.hits++;
            if (rec.type == ACCESS_READ || rec.type == ACCESS_MODIFY)
                r.read_hits++;
            else
                r.write_hits++;
        } else {
            r.misses++;
        }

        /* Second access for MODIFY */
        if (rec.type == ACCESS_MODIFY) {
            if (result2 == CACHE_HIT) {
                r.hits++;
                r.write_hits++;
            } else {
                r.misses++;
            }
        }
    }

    /* Collect final cache stats */
    r.evictions = cache->evictions;

    /* Collect trace reader stats */
    r.trace_lines_read     = tr->lines_read;
    r.trace_records_parsed = tr->records_parsed;
    r.trace_lines_skipped  = tr->lines_skipped;

    /* Compute rates */
    if (r.total_accesses > 0) {
        r.hit_rate  = 100.0 * (double)r.hits  / (double)r.total_accesses;
        r.miss_rate = 100.0 * (double)r.misses / (double)r.total_accesses;
    }

    r.success = 1;
    set_cache_destroy(cache);
    trace_close(tr);
    return r;
}

/* ------------------------------------------------------------------ */
/* simulator_print_report                                               */
/* ------------------------------------------------------------------ */
void simulator_print_report(const SimResult *r, const SimConfig *cfg)
{
    if (!r || !r->success) {
        printf("[sim] Simulation failed -- no report.\n");
        return;
    }

    static const char *pnames[] = {"LRU", "FIFO", "LFU"};
    const char *pname = (cfg->policy < 3) ? pnames[cfg->policy] : "?";

    printf("==============================================\n");
    printf("  Simulation Report\n");
    printf("==============================================\n");
    printf("  Trace file   : %s\n", cfg->trace_path);
    printf("  Cache config : %d-way, %d sets, %d-byte lines (%d KB)\n",
           cfg->n_ways, cfg->n_sets, cfg->line_size,
           cfg->n_sets * cfg->n_ways * cfg->line_size / 1024);
    printf("  Policy       : %s\n", pname);
    printf("----------------------------------------------\n");
    printf("  Trace stats  :\n");
    printf("    Lines read   : %llu\n", (unsigned long long)r->trace_lines_read);
    printf("    Records used : %llu\n", (unsigned long long)r->trace_records_parsed);
    printf("    Lines skipped: %llu\n", (unsigned long long)r->trace_lines_skipped);
    printf("----------------------------------------------\n");
    printf("  Access breakdown:\n");
    printf("    Total        : %llu\n", (unsigned long long)r->total_accesses);
    printf("    Reads        : %llu  (hits: %llu)\n",
           (unsigned long long)r->read_accesses,
           (unsigned long long)r->read_hits);
    printf("    Writes       : %llu  (hits: %llu)\n",
           (unsigned long long)r->write_accesses,
           (unsigned long long)r->write_hits);
    if (r->modify_accesses > 0)
        printf("    Modifys (M)  : %llu\n",
               (unsigned long long)r->modify_accesses);
    printf("----------------------------------------------\n");
    printf("  Performance:\n");
    printf("    Hits         : %llu\n", (unsigned long long)r->hits);
    printf("    Misses       : %llu\n", (unsigned long long)r->misses);
    printf("    Evictions    : %llu\n", (unsigned long long)r->evictions);
    printf("    Hit Rate     : %.2f%%\n", r->hit_rate);
    printf("    Miss Rate    : %.2f%%\n", r->miss_rate);
    printf("==============================================\n");
}

/* ------------------------------------------------------------------ */
/* simulator_print_comparison                                           */
/* ------------------------------------------------------------------ */
void simulator_print_comparison(const SimResult *results,
                                const SimConfig *configs,
                                int count)
{
    static const char *pnames[] = {"LRU", "FIFO", "LFU"};

    printf("==============================================\n");
    printf("  Policy Comparison\n");
    printf("==============================================\n");
    printf("  %-6s | %5s | %5s | %9s | %10s\n",
           "Policy", "Hits", "Miss", "Hit Rate", "Evictions");
    printf("  -----------------------------------------------\n");

    for (int i = 0; i < count; i++) {
        if (!results[i].success) continue;
        const char *pname = (configs[i].policy < 3) ? pnames[configs[i].policy] : "?";
        printf("  %-6s | %5llu | %5llu | %8.2f%% | %10llu\n",
               pname,
               (unsigned long long)results[i].hits,
               (unsigned long long)results[i].misses,
               results[i].hit_rate,
               (unsigned long long)results[i].evictions);
    }
    printf("==============================================\n");
}
