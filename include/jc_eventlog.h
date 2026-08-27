/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_eventlog.h - an opt-in, append-only JSONL structured-event sink.
 *
 * Generalizes the autonomy envelope's audit-journal pattern
 * (jc_env_journal_begin/_end) into a reusable sink so the agent core can emit
 * uniform telemetry (latency, tokens, cost, tool/route/verify events, and -- at
 * the `full` tier -- prompt/response content) for offline analysis. Default off;
 * write the file OUTSIDE any agent workspace (a snapshot rollback reverts files
 * inside it -- see docs/ANECDOTES.md). See ROADMAP M21.
 *
 * Usage:
 *   o = jc_eventlog_begin(log, "model_call");   // NULL when disabled
 *   cJSON_AddNumberToObject(o, "in_tok", in);   // no-ops safely if o == NULL
 *   jc_eventlog_end(log, o);                     // one compact line + flush
 */
#ifndef JC_EVENTLOG_H
#define JC_EVENTLOG_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "cJSON.h"

#include <stdio.h>

/* Capture tiers (config `logging.level`). */
enum jc_eventlog_level {
    JC_EVENTLOG_OFF = 0,     /* nothing is written                          */
    JC_EVENTLOG_METRICS = 1, /* structured metrics only (no content)        */
    JC_EVENTLOG_FULL = 2     /* metrics + bounded prompt/response/tool I/O   */
};

/* The current event-log schema version, stamped as `v` on every event. Bump on a
 * breaking change to field names/shapes so downstream tooling can adapt. */
#define JC_EVENTLOG_SCHEMA 1

/* Default per-field cap for `full`-tier content (prompt/response/tool I/O). */
#define JC_EVENTLOG_TEXT_MAX (16 * 1024)

struct jc_eventlog {
    FILE *f;        /* append target, or NULL => disabled (all ops no-op)   */
    char  sid[40];  /* session id stamped on each event, or "" (correlation)*/
    char  workspace[1024]; /* workspace root stamped as "ws" (M56), or ""   */
    long  seq;      /* per-sink monotonic event counter                     */
    int   level;    /* enum jc_eventlog_level                               */
    int   owns_f;   /* whether jc_eventlog_close() should fclose(f)         */
    /* M292: the file being appended to. The sink knew where it was writing and
     * could not be asked, so the TUI had no way to analyse the log this session
     * is producing -- the obvious input for `/learn analyze`. Empty when the sink
     * is disabled or writing to a stream it does not own. */
    char  path[1100];
};

/* Parse a tier name ("off"/"metrics"/"full") to a jc_eventlog_level, or -1 if
 * unrecognized. NULL/empty => JC_EVENTLOG_OFF. Pure. */
int jc_eventlog_level_parse(const char *s);

/* The file this sink appends to, or NULL when disabled / not a file. */
const char *jc_eventlog_path(const struct jc_eventlog *log);

/* The canonical name for a level ("off"/"metrics"/"full"). Pure. */
const char *jc_eventlog_level_name(int level);

/* Initialize `log` to the disabled state (f=NULL); all calls become no-ops. */
void jc_eventlog_disable(struct jc_eventlog *log);

/* Open an event log appending to `path` (its parent directory is created). `sid`
 * (may be NULL/"") is stamped on every event. `level` is a jc_eventlog_level;
 * JC_EVENTLOG_OFF leaves the log disabled. Returns JC_OK on success; on any
 * failure (or OFF) the log is left disabled and JC_ERR_* is returned. */
jc_status jc_eventlog_open(struct jc_eventlog *log, const char *path,
                          const char *sid, int level);

/* Stamp `ws` (the workspace root) as the "ws" field on every subsequent event,
 * so an offline summarizer can filter/group by project (M56). NULL/"" clears it.
 * No-op on a disabled log. */
void jc_eventlog_set_workspace(struct jc_eventlog *log, const char *ws);

/* Close the file (if owned) and reset to disabled. Safe on a NULL/disabled log. */
void jc_eventlog_close(struct jc_eventlog *log);

/* Begin an event: returns a cJSON object pre-stamped with `v`, `ts` (wall-clock
 * seconds), `sid` (when set), `seq`, and `event`, for the caller to add fields
 * to. Returns NULL when the log is disabled (log==NULL or log->f==NULL), so
 * callers can add fields unconditionally (cJSON_Add* tolerate a NULL object). */
cJSON *jc_eventlog_begin(struct jc_eventlog *log, const char *event);

/* Finish an event: write `o` as one compact JSON line + '\n', flush, and free
 * `o`. Tolerates a NULL `log` and/or NULL `o`. */
void jc_eventlog_end(struct jc_eventlog *log, cJSON *o);

/* Whether content capture (the `full` tier) is enabled -- gate prompt/response/
 * tool-I/O fields behind this. 0 when disabled or metrics-only. */
int jc_eventlog_full(const struct jc_eventlog *log);

/* Add string `s` to object `o` under `key`, truncated to at most `max` bytes
 * (0 => unbounded) on a UTF-8 boundary, with a "...[+N B]" marker when cut.
 * No-op when `o`/`key`/`s` is NULL. For `full`-tier content fields. */
void jc_eventlog_add_text(cJSON *o, const char *key, const char *s, jc_size max);

#ifdef __cplusplus
}
#endif
#endif /* JC_EVENTLOG_H */
