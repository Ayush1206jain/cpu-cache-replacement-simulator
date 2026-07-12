/*
 * trace.h -- Memory Access Trace Reader
 *
 * Supported trace formats:
 *
 *   1. Simple format (default):
 *        R 0x7fff5a3c
 *        W 0x400512
 *        I 0x400800          (instruction fetch, treated as read)
 *
 *   2. Valgrind lackey format:
 *        I  0x400512,4
 *        S  0x7fff5a3c,8     (store = write)
 *        L  0x601048,4       (load  = read)
 *        M  0x601048,4       (modify = read + write, counts as 2 accesses)
 *
 * Lines beginning with '#' or '=' are treated as comments and skipped.
 * Both formats are auto-detected per line.
 *
 * -- CPU Cache Replacement Simulator
 */

#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Access type                                                          */
/* ------------------------------------------------------------------ */
typedef enum {
    ACCESS_READ    = 0,   /* Instruction fetch or data load  */
    ACCESS_WRITE   = 1,   /* Data store                      */
    ACCESS_MODIFY  = 2    /* Read-then-write (Valgrind 'M')  */
} AccessType;

/* ------------------------------------------------------------------ */
/* One parsed trace record                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t   address;   /* Memory address (hex parsed)     */
    int        size;      /* Access size in bytes (0 if N/A) */
    AccessType type;      /* READ / WRITE / MODIFY           */
} TraceRecord;

/* ------------------------------------------------------------------ */
/* Trace reader handle                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    FILE    *file;
    char     path[512];
    uint64_t lines_read;
    uint64_t records_parsed;
    uint64_t lines_skipped;    /* comments / blank / malformed */
} TraceReader;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * Open a trace file for reading.
 * Returns NULL on error (file not found, malloc fail).
 */
TraceReader *trace_open(const char *path);

/**
 * Read the next trace record.
 * Returns 1 on success, 0 on EOF, -1 on parse error (record skipped).
 * out_rec is only written on return value 1.
 */
int trace_next(TraceReader *tr, TraceRecord *out_rec);

/** Close the file and free the reader. */
void trace_close(TraceReader *tr);

/** Return human-readable string for access type. */
const char *trace_access_type_str(AccessType t);

#endif /* TRACE_H */
