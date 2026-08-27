/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_runsview.h - offline summarizer for autonomy-envelope run journals
 * (M158). The read side of the envelope journal: parses one run's JSONL
 * (~/.jichi.d/runs/<run>.jsonl -- events `start`/`verify`/`budget`/
 * `rollback`/`out_of_scope`/`end` written by jc_env_journal_begin/_end) into a
 * one-row summary, and renders an aligned table. Pure (no I/O) -- the `runs`
 * subcommand shell in main.c walks the directory -- mirroring the
 * jc_eventlog / jc_telemetry writer/reader split. */

#ifndef JC_RUNSVIEW_H
#define JC_RUNSVIEW_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_str.h"
#include "jc_json.h" /* cJSON for the --output json renderer (M160) */

struct jc_run_summary {
    char run[80];         /* run id (from the events' "run" field)          */
    double ts_first;      /* first event ts (run start)                     */
    double ts_last;       /* last event ts                                  */
    char outcome[24];     /* from the `end` event; "?" if the journal has
                           * none (crashed / killed / still running)        */
    int rolled_back;      /* -1 unknown, else 0/1 (from `end`)              */
    double tokens_used;   /* from `end`                                     */
    long tool_calls;      /* from `end`                                     */
    char budget_kind[16]; /* which budget tripped (from `budget`), or ""    */
    int starved;          /* budget stop before any edit (M96)              */
    int verify_pass;      /* `verify` events with exit == 0                 */
    int verify_fail;      /* `verify` events with exit != 0                 */
    long out_of_scope;    /* paths flagged by `out_of_scope` events         */
    long rollbacks;       /* `rollback` events                              */
    int  no_changes;      /* the run wrote NOTHING, so its completion verify never
                           * fired (that is gated on a mutating tool) and it exited
                           * 0 regardless. Absent from the journal when snapshots
                           * are off, since it is then unknowable.                */
    long constraints;     /* `constraint` events: inferred constraints this run
                           * ADOPTED, which silently narrow what it may do. They
                           * announce themselves on stderr once and never again,
                           * so without this a post-mortem cannot tell a run that
                           * chose to do little from one that was forbidden.     */
    long asks_unanswered; /* M359: `ask` events with answered:false -- the
                           * model reached for a human and nobody was there */
    long steered;         /* M161: `control` inject events -- operator
                           * steering (M159); pause/abort surface elsewhere */
    long post_outcome;    /* M329: model calls metered after the run's outcome
                           * was decided. Always a bug, and invisible without
                           * this: the `end` event is already written, so the
                           * tokens/tool-calls a post-mortem reads are short by
                           * whatever was spent afterwards. M328 was 625k
                           * tokens past a 1m budget, found only by comparing
                           * the journal against telemetry by hand.          */
    double learn_tokens;  /* M330: tokens spent by the learn-on-stop mentor
                           * after the run's `end` event. Non-zero means the
                           * run's own tokens_used/tool_calls are SHORT.     */
    long learn_calls;     /* M330: tool calls made by the mentor.              */
    long learn_draft_items; /* M598: memory notes + skills + corrections + rules
                           * `learn apply` would commit from the draft the
                           * mentor produced; -1 when the event predates M598
                           * or the command declared no output file.         */
    int  learn_draft_empty; /* M598: the draft has bytes and NO parseable
                           * section -- `learn apply` would commit nothing.
                           * The zigodot shape: prose under invented headings,
                           * produced for weeks by a mentor that never received
                           * its format block (M596). Rendered as draft=empty. */
    char jichi[32];       /* M290: the build that ran it (from `start`); ""
                           * for a pre-M290 journal. A run journal is what a
                           * supervisor triages from, and "which jichi ran
                           * this" is otherwise unrecoverable once the binary
                           * has moved on. */
    long verify_stuck;    /* M420: `verify_stuck` events (M89) -- the run hit
                           * the SAME verify error twice or more in a row. It
                           * was journaled from M89 and read by nobody, so a
                           * run thrashing on one error had a row identical to
                           * a clean one.                                    */
    long test_edits;      /* M420: `test_assertion_edit` events (M88) -- a test
                           * assertion was MODIFIED during the run, so a green
                           * verify may be a moved goalpost rather than work.
                           * Measured 2026-08-13: a real attempt fired this ten
                           * times and still reported PASS (M410 fixed the
                           * verdict; this makes the supervisor's TABLE see it). */
    char ws[512];         /* M420: the workspace, from `start` -- "" for a
                           * pre-M420 journal. A run that cannot say which
                           * project it belongs to is unusable on a machine
                           * driving several, and `runs` has no such column.  */
    long tool_loops;      /* M432: `tool_loop` events -- a tool call kept failing
                           * the same way inside one turn. Non-zero only, like
                           * blocked= and stuck=: a column that is usually blank is
                           * read, and one that is usually 0 is not. */
    long blocked_repeats; /* M429: `blocked_repeat` events -- the run tried a
                           * POLICY-FORBIDDEN action again after being refused.
                           * Worth a column of its own because a block is neither
                           * a tool error (`ok:false`) nor counted against
                           * --max-tool-calls, so a run can spend its whole token
                           * budget on one and every other signal reads normal. */
    long events;          /* total parsed events                            */
    long journal_events;  /* M421: events whose name is JOURNAL-EXCLUSIVE. The
                           * telemetry sink uses the same `"event"` key and (since
                           * M420) carries `run` too, so `events > 0` no longer
                           * distinguishes the two files. Three names appear in
                           * BOTH vocabularies -- `constraint`, `route`,
                           * `tool_call` -- and none of them may count here.    */
};

/* Parse one journal's whole JSONL text. Returns 0 when at least one
 * JOURNAL-EXCLUSIVE event parsed, -1 otherwise (so a telemetry log fed to this
 * reader is rejected rather than rendered as an all-zero phantom run).
 * Tolerates blank/malformed lines. */
int jc_runsview_parse(const char *text, struct jc_run_summary *out);

/* Render the table header / one summary row into `out` (aligned columns). */
void jc_runsview_render_header(struct jc_sb *out);
void jc_runsview_render_row(const struct jc_run_summary *s, struct jc_sb *out);

/* Build one run's machine-readable summary (M160, `runs --output json`):
 * {run, ts, ts_end, outcome, rolled_back (bool|null when unknown),
 *  tokens_used, tool_calls, budget?, starved?, verify:{pass,fail},
 *  out_of_scope?, rollbacks?, steered?, post_outcome?, learn_tokens?,
 *  learn_calls?, verify_stuck?, test_edits?, ws?, events} -- zero/empty
 * optionals omitted (verify_stuck/test_edits = M420, the two quality signals:
 * stuck on one verify error, and a test assertion modified mid-run; ws = M420,
 * which project the run belonged to; steered = M161
 * operator-inject count; post_outcome = M329, true when spend continued after
 * the outcome; learn_tokens/learn_calls = M330, tokens/calls spent by the
 * learn-on-stop mentor after the run's `end` event).
 * Caller owns the returned object (cJSON_Delete). NULL on OOM/NULL input. */
cJSON *jc_runsview_json(const struct jc_run_summary *s);

#ifdef __cplusplus
}
#endif
#endif /* JC_RUNSVIEW_H */
