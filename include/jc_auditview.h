/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_auditview.h - offline summarizer for the privileged-command audit log
 * (M158). The read side of jc_audit: parses the append-only JSONL that
 * jc_audit_privileged writes (~/.jichi.d/audit/privileged.jsonl) into
 * per-decision / per-launcher counts plus the most recent entries, and renders
 * an operator summary. Pure (no I/O) -- the `audit` subcommand shell in main.c
 * does the file reading -- mirroring the jc_eventlog (writer) / jc_telemetry
 * (reader) split. */

#ifndef JC_AUDITVIEW_H
#define JC_AUDITVIEW_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_json.h" /* cJSON for the --output json renderer (M160) */

/* How many of the newest entries the summary shows verbatim. */
#define JC_AUDITVIEW_RECENT 10
/* Command text kept per recent entry (truncated with "..."). */
#define JC_AUDITVIEW_CMD 96

struct jc_audit_count {
    char name[32];
    long n;
};

struct jc_audit_row {
    double ts;
    char launcher[16];
    char decision[24];
    char mode[8];
    char cmd[JC_AUDITVIEW_CMD];
};

struct jc_audit_summary {
    long total;      /* entries counted (after any --since cutoff)          */
    long refused;    /* deny + ask_denied + unattended_refused              */
    long ran;        /* allow + allowlist + ask_approved                    */
    long skipped;    /* entries older than the cutoff (informational)       */
    long malformed;  /* lines that failed to parse (informational)          */
    struct jc_vec by_decision; /* of struct jc_audit_count                  */
    struct jc_vec by_launcher; /* of struct jc_audit_count                  */
    /* Ring of the newest JC_AUDITVIEW_RECENT entries, oldest-first when
     * rendered. `nrecent` counts stored rows; `rpos` is the ring cursor. */
    struct jc_audit_row recent[JC_AUDITVIEW_RECENT];
    int nrecent;
    int rpos;
};

void jc_auditview_init(struct jc_audit_summary *s);
void jc_auditview_free(struct jc_audit_summary *s);

/* Fold one whole JSONL text into the summary. Entries with ts < since_ts are
 * skipped (counted in `skipped`); pass 0.0 for no cutoff. Tolerates blank and
 * malformed lines. */
void jc_auditview_feed(struct jc_audit_summary *s, const char *text,
                       double since_ts);

/* True if `decision` means the command was refused (vs actually run). */
int jc_auditview_is_refusal(const char *decision);

/* Render the operator summary (counts + recent entries) into `out`. */
void jc_auditview_render(const struct jc_audit_summary *s, struct jc_sb *out);

/* Build the machine-readable summary (M160, `audit --output json`):
 * {v:1, total, refused, ran, skipped, malformed,
 *  by_decision:{name:n,...}, by_launcher:{...},
 *  recent:[{ts,launcher,decision,mode,command}...]}  (oldest-first).
 * Caller owns the returned object (cJSON_Delete). NULL on OOM/NULL input. */
cJSON *jc_auditview_json(const struct jc_audit_summary *s);

#ifdef __cplusplus
}
#endif
#endif /* JC_AUDITVIEW_H */
