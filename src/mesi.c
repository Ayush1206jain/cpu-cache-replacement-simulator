/*
 * mesi.c -- MESI Cache Coherence Protocol Implementation
 *
 * Implements the full 4-state MESI protocol for a 2-core shared-bus system.
 * Every state transition is logged with before/after states and the reason.
 *
 * Key design decision: direct-mapped caches (1-way per set).
 * This is sufficient to demonstrate all MESI transitions;
 * associativity would add complexity without new protocol insight.
 *
 * Day 9 -- CPU Cache Replacement Simulator
 */

#include "mesi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* State name helpers                                                   */
/* ------------------------------------------------------------------ */
const char *mesi_state_str(MESIState s)
{
    switch (s) {
        case MESI_INVALID:   return "I (Invalid)";
        case MESI_SHARED:    return "S (Shared)";
        case MESI_EXCLUSIVE: return "E (Exclusive)";
        case MESI_MODIFIED:  return "M (Modified)";
        default:             return "? (Unknown)";
    }
}

static const char *short_state(MESIState s)
{
    switch (s) {
        case MESI_INVALID:   return "I";
        case MESI_SHARED:    return "S";
        case MESI_EXCLUSIVE: return "E";
        case MESI_MODIFIED:  return "M";
        default:             return "?";
    }
}

/* ------------------------------------------------------------------ */
/* Transition log                                                       */
/* ------------------------------------------------------------------ */
static void log_transition(const MESISystem *sys, int core_id,
                           uint64_t addr,
                           MESIState before, MESIState after,
                           const char *reason)
{
    if (!sys->log_enabled) return;
    printf("  [Core%d] 0x%06llx : %s -> %s  (%s)\n",
           core_id,
           (unsigned long long)addr,
           short_state(before),
           short_state(after),
           reason);
}

/* ------------------------------------------------------------------ */
/* Create / destroy                                                     */
/* ------------------------------------------------------------------ */
MESICache *mesi_cache_create(int n_sets, int line_size, int core_id)
{
    MESICache *c = (MESICache *)calloc(1, sizeof(MESICache));
    if (!c) return NULL;

    c->lines = (MESILine *)calloc((size_t)n_sets, sizeof(MESILine));
    if (!c->lines) { free(c); return NULL; }

    /* All lines start Invalid */
    for (int i = 0; i < n_sets; i++) {
        c->lines[i].state = MESI_INVALID;
        c->lines[i].valid = 0;
    }

    c->n_sets    = n_sets;
    c->line_size = line_size;
    c->core_id   = core_id;
    return c;
}

void mesi_cache_destroy(MESICache *c)
{
    if (!c) return;
    free(c->lines);
    free(c);
}

MESISystem *mesi_system_create(int n_sets, int line_size)
{
    MESISystem *sys = (MESISystem *)calloc(1, sizeof(MESISystem));
    if (!sys) return NULL;

    sys->core[0] = mesi_cache_create(n_sets, line_size, 0);
    sys->core[1] = mesi_cache_create(n_sets, line_size, 1);

    if (!sys->core[0] || !sys->core[1]) {
        mesi_system_destroy(sys);
        return NULL;
    }

    sys->log_enabled = 1;   /* log transitions by default */
    return sys;
}

void mesi_system_destroy(MESISystem *sys)
{
    if (!sys) return;
    mesi_cache_destroy(sys->core[0]);
    mesi_cache_destroy(sys->core[1]);
    free(sys);
}

/* ------------------------------------------------------------------ */
/* Internal: get line from a cache for a given address                 */
/* ------------------------------------------------------------------ */
static MESILine *get_line(MESICache *c, uint64_t addr)
{
    int idx = mesi_set_index(c, addr);
    return &c->lines[idx];
}

/* ------------------------------------------------------------------ */
/* Internal: check if a line in a cache matches address (valid + tag)  */
/* ------------------------------------------------------------------ */
static int line_matches(MESICache *c, uint64_t addr)
{
    int idx = mesi_set_index(c, addr);
    MESILine *l = &c->lines[idx];
    return l->valid && (l->tag == mesi_tag(c, addr))
           && (l->state != MESI_INVALID);
}

/* ------------------------------------------------------------------ */
/* mesi_read: Core core_id reads address                               */
/*                                                                      */
/* PrRd transitions:                                                   */
/*   Own state I -> BusRd -> get data                                  */
/*             If other core has M: other writes back, both -> S       */
/*             If other core has E: other -> S, requester -> S         */
/*             If other core has S: requester -> S                     */
/*             If other core has I: requester -> E (exclusive)         */
/*   Own state S -> S (no bus, already shared)                         */
/*   Own state E -> E (no bus, exclusive read)                         */
/*   Own state M -> M (no bus, modified read)                          */
/* ------------------------------------------------------------------ */
MESIState mesi_read(MESISystem *sys, int core_id, uint64_t address)
{
    MESICache *self  = sys->core[core_id];
    MESICache *other = sys->core[1 - core_id];

    self->reads++;

    MESILine *own_line   = get_line(self, address);
    MESIState own_before = own_line->state;
    uint64_t  own_tag    = mesi_tag(self, address);

    /* ---- Case 1: HIT in own cache ---- */
    if (own_line->valid && own_line->tag == own_tag
        && own_line->state != MESI_INVALID) {
        self->read_hits++;
        /* State doesn't change on a read hit */
        log_transition(sys, core_id, address,
                       own_before, own_before, "PrRd HIT (no state change)");
        return own_line->state;
    }

    /* ---- Case 2: MISS -- issue BusRd ---- */
    sys->bus_reads++;

    /* Check if other core has the line */
    int other_has = line_matches(other, address);
    MESILine   *oth_line   = get_line(other, address);
    MESIState   oth_before = oth_line->state;

    if (other_has) {
        /* Snoop: other core reacts to BusRd */
        switch (oth_before) {
            case MESI_MODIFIED:
                /* Other must write back dirty data, then share */
                other->writebacks++;
                sys->bus_writebacks++;
                oth_line->state = MESI_SHARED;
                log_transition(sys, 1 - core_id, address,
                               MESI_MODIFIED, MESI_SHARED,
                               "BusRd snoop: M->S (writeback to mem)");
                /* Requester gets shared copy */
                own_line->valid = 1;
                own_line->tag   = own_tag;
                own_line->state = MESI_SHARED;
                log_transition(sys, core_id, address,
                               own_before, MESI_SHARED,
                               "PrRd MISS: I->S (from M owner writeback)");
                break;

            case MESI_EXCLUSIVE:
                /* Other downgrades E->S, requester gets S */
                oth_line->state = MESI_SHARED;
                log_transition(sys, 1 - core_id, address,
                               MESI_EXCLUSIVE, MESI_SHARED,
                               "BusRd snoop: E->S (downgrade)");
                own_line->valid = 1;
                own_line->tag   = own_tag;
                own_line->state = MESI_SHARED;
                log_transition(sys, core_id, address,
                               own_before, MESI_SHARED,
                               "PrRd MISS: I->S (shared with E holder)");
                break;

            case MESI_SHARED:
                /* Other stays S, requester also gets S */
                log_transition(sys, 1 - core_id, address,
                               MESI_SHARED, MESI_SHARED,
                               "BusRd snoop: S->S (no change)");
                own_line->valid = 1;
                own_line->tag   = own_tag;
                own_line->state = MESI_SHARED;
                log_transition(sys, core_id, address,
                               own_before, MESI_SHARED,
                               "PrRd MISS: I->S (another sharer exists)");
                break;

            case MESI_INVALID:
                other_has = 0;  /* treat as not present */
                break;
        }
    }

    if (!other_has) {
        /* No other sharer -- requester gets Exclusive */
        own_line->valid = 1;
        own_line->tag   = own_tag;
        own_line->state = MESI_EXCLUSIVE;
        log_transition(sys, core_id, address,
                       own_before, MESI_EXCLUSIVE,
                       "PrRd MISS: I->E (no other sharer, exclusive)");
    }

    return own_line->state;
}

/* ------------------------------------------------------------------ */
/* mesi_write: Core core_id writes to address                          */
/*                                                                      */
/* PrWr transitions:                                                   */
/*   Own state I -> BusRdX -> M (fetch + invalidate others)           */
/*   Own state S -> BusUpgr -> M (invalidate others, upgrade)         */
/*   Own state E -> M (silent upgrade, no bus traffic)                 */
/*   Own state M -> M (already owner, no bus traffic)                  */
/*                                                                      */
/* Snooped BusRdX or BusUpgr on other core:                           */
/*   S -> I  (must invalidate shared copy)                             */
/*   E -> I  (must invalidate exclusive copy)                          */
/*   M -> I  (must write back AND invalidate)                          */
/* ------------------------------------------------------------------ */
MESIState mesi_write(MESISystem *sys, int core_id, uint64_t address)
{
    MESICache *self  = sys->core[core_id];
    MESICache *other = sys->core[1 - core_id];

    self->writes++;

    MESILine *own_line   = get_line(self, address);
    MESIState own_before = own_line->state;
    uint64_t  own_tag    = mesi_tag(self, address);

    /* ---- Check if own cache already has the line ---- */
    int own_has = own_line->valid && own_line->tag == own_tag
                  && own_line->state != MESI_INVALID;

    /* ---- Handle other core's line via bus snooping ---- */
    MESILine *oth_line   = get_line(other, address);
    MESIState oth_before = oth_line->state;
    int other_has = line_matches(other, address);

    if (own_has) {
        self->write_hits++;

        switch (own_before) {
            case MESI_MODIFIED:
                /* Already M -- no bus, just write */
                log_transition(sys, core_id, address,
                               MESI_MODIFIED, MESI_MODIFIED,
                               "PrWr HIT: M->M (already owner)");
                break;

            case MESI_EXCLUSIVE:
                /* Silent upgrade E->M, no bus needed */
                own_line->state = MESI_MODIFIED;
                self->upgrades++;
                log_transition(sys, core_id, address,
                               MESI_EXCLUSIVE, MESI_MODIFIED,
                               "PrWr HIT: E->M (silent upgrade, no bus)");
                break;

            case MESI_SHARED:
                /* Need to invalidate other sharers -- BusUpgr */
                sys->bus_invalidates++;
                if (other_has) {
                    oth_line->state = MESI_INVALID;
                    oth_line->valid = 0;
                    other->invalidations++;
                    log_transition(sys, 1 - core_id, address,
                                   oth_before, MESI_INVALID,
                                   "BusUpgr snoop: S->I (invalidated)");
                }
                own_line->state = MESI_MODIFIED;
                log_transition(sys, core_id, address,
                               MESI_SHARED, MESI_MODIFIED,
                               "PrWr HIT: S->M (BusUpgr, others invalidated)");
                break;

            default:
                break;
        }
    } else {
        /* Own line is INVALID -- BusRdX (fetch + exclusive ownership) */
        sys->bus_readx++;

        if (other_has) {
            switch (oth_before) {
                case MESI_MODIFIED:
                    /* Other must write back, then invalidate */
                    other->writebacks++;
                    sys->bus_writebacks++;
                    oth_line->state = MESI_INVALID;
                    oth_line->valid = 0;
                    other->invalidations++;
                    log_transition(sys, 1 - core_id, address,
                                   MESI_MODIFIED, MESI_INVALID,
                                   "BusRdX snoop: M->I (writeback + invalidate)");
                    break;

                case MESI_EXCLUSIVE:
                    oth_line->state = MESI_INVALID;
                    oth_line->valid = 0;
                    other->invalidations++;
                    log_transition(sys, 1 - core_id, address,
                                   MESI_EXCLUSIVE, MESI_INVALID,
                                   "BusRdX snoop: E->I (invalidate)");
                    break;

                case MESI_SHARED:
                    oth_line->state = MESI_INVALID;
                    oth_line->valid = 0;
                    other->invalidations++;
                    log_transition(sys, 1 - core_id, address,
                                   MESI_SHARED, MESI_INVALID,
                                   "BusRdX snoop: S->I (invalidate)");
                    break;

                default:
                    break;
            }
        }

        /* Requester gets Modified */
        own_line->valid = 1;
        own_line->tag   = own_tag;
        own_line->state = MESI_MODIFIED;
        log_transition(sys, core_id, address,
                       own_before, MESI_MODIFIED,
                       "PrWr MISS: I->M (BusRdX, fetch + exclusive)");
    }

    return own_line->state;
}

/* ------------------------------------------------------------------ */
/* Print stats                                                          */
/* ------------------------------------------------------------------ */
void mesi_print_stats(const MESISystem *sys)
{
    printf("==============================================\n");
    printf("  MESI System Statistics\n");
    printf("==============================================\n");

    for (int i = 0; i < 2; i++) {
        MESICache *c = sys->core[i];
        uint64_t total_acc = c->reads + c->writes;
        uint64_t total_hit = c->read_hits + c->write_hits;
        printf("  Core %d:\n", i);
        printf("    Reads       : %llu  (hits: %llu)\n",
               (unsigned long long)c->reads,
               (unsigned long long)c->read_hits);
        printf("    Writes      : %llu  (hits: %llu)\n",
               (unsigned long long)c->writes,
               (unsigned long long)c->write_hits);
        printf("    Hit rate    : %.2f%%\n",
               total_acc > 0 ? 100.0 * (double)total_hit / (double)total_acc : 0.0);
        printf("    Writebacks  : %llu\n",
               (unsigned long long)c->writebacks);
        printf("    Invalidations: %llu\n",
               (unsigned long long)c->invalidations);
        printf("    E->M upgrades: %llu\n",
               (unsigned long long)c->upgrades);
        printf("\n");
    }

    printf("  Bus transactions:\n");
    printf("    BusRd   (read miss)           : %llu\n",
           (unsigned long long)sys->bus_reads);
    printf("    BusRdX  (write miss, excl)    : %llu\n",
           (unsigned long long)sys->bus_readx);
    printf("    BusUpgr (S->M invalidate)     : %llu\n",
           (unsigned long long)sys->bus_invalidates);
    printf("    Writebacks to memory          : %llu\n",
           (unsigned long long)sys->bus_writebacks);
    printf("==============================================\n");
}
