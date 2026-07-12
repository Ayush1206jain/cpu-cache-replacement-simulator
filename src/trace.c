/*
 * trace.c -- Memory Access Trace Reader Implementation
 *
 * Parses trace files line by line using fgets().
 * Auto-detects format from the operation character on each line.
 *
 * Simple format:   "R 0x7fff5a3c"   or  "W 0x400512"
 * Valgrind format: "L 0x7fff5a3c,8" or  "S 0x400512,4"
 *                  "M 0x601048,4"   (counts as one record, type=MODIFY)
 *                  "I 0x400800,4"   (instruction fetch = READ)
 *
 *  -- CPU Cache Replacement Simulator
 */

#include "trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define LINE_BUF 256

/* ------------------------------------------------------------------ */
/* Internal: skip leading whitespace                                    */
/* ------------------------------------------------------------------ */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* ------------------------------------------------------------------ */
/* Internal: parse one text line into a TraceRecord                    */
/*                                                                      */
/* Returns:  1 -- success                                              */
/*           0 -- blank / comment line (skip, not an error)            */
/*          -1 -- malformed line (skip, log as error)                  */
/* ------------------------------------------------------------------ */
static int parse_line(const char *line, TraceRecord *out)
{
    const char *p = skip_ws(line);

    /* Skip blank lines and comments (# or =) */
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#' || *p == '=')
        return 0;

    /* First non-space character is the operation code */
    char op = (char)toupper((unsigned char)*p);
    p = skip_ws(p + 1);    /* advance past op, then skip whitespace */

    /* Determine access type */
    AccessType type;
    switch (op) {
        case 'R': case 'I': case 'L': type = ACCESS_READ;   break;
        case 'W': case 'S':           type = ACCESS_WRITE;  break;
        case 'M':                     type = ACCESS_MODIFY; break;
        default:
            return -1;  /* unknown op -- malformed */
    }

    /* Parse hex address (with or without 0x prefix) */
    if (*p == '\0') return -1;

    char *end = NULL;
    uint64_t address = (uint64_t)strtoull(p, &end, 16);

    if (end == p) return -1;   /* no digits consumed -- malformed */

    /* Optional ",size" suffix (Valgrind format) */
    int size = 0;
    if (*end == ',') {
        char *end2 = NULL;
        size = (int)strtol(end + 1, &end2, 10);
        if (end2 == end + 1) size = 0;  /* failed to parse size -- ignore */
    }

    out->address = address;
    out->size    = size;
    out->type    = type;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

TraceReader *trace_open(const char *path)
{
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[trace] Cannot open '%s': %s\n",
                path, strerror(errno));
        return NULL;
    }

    TraceReader *tr = (TraceReader *)calloc(1, sizeof(TraceReader));
    if (!tr) { fclose(f); return NULL; }

    tr->file = f;
    strncpy(tr->path, path, sizeof(tr->path) - 1);
    return tr;
}

int trace_next(TraceReader *tr, TraceRecord *out_rec)
{
    if (!tr || !tr->file || !out_rec) return 0;

    char buf[LINE_BUF];

    while (fgets(buf, sizeof(buf), tr->file)) {
        tr->lines_read++;

        int rc = parse_line(buf, out_rec);
        if (rc == 1) {
            tr->records_parsed++;
            return 1;          /* success -- caller processes record */
        } else if (rc == 0) {
            tr->lines_skipped++;  /* blank / comment */
        } else {
            tr->lines_skipped++;  /* malformed -- skip silently */
        }
    }

    return 0;   /* EOF */
}

void trace_close(TraceReader *tr)
{
    if (!tr) return;
    if (tr->file) fclose(tr->file);
    free(tr);
}

const char *trace_access_type_str(AccessType t)
{
    switch (t) {
        case ACCESS_READ:   return "READ";
        case ACCESS_WRITE:  return "WRITE";
        case ACCESS_MODIFY: return "MODIFY";
        default:            return "UNKNOWN";
    }
}
