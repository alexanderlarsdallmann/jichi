/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_reach.h - the reach footer: what the run's RECORD checked, and what it
 * did not (M630, the argumentation program's third slice).
 *
 * tsuiseki-04 proves that a run's summary is a summary of what was TRIED, and
 * that the final answer "is exactly as reliable as the run and not one bit
 * more" -- it is the one artifact nothing checked. The record knows what the
 * answer cannot: whether a verifier ran and what colour it was, how many tool
 * results carried is_error, whether a test assertion was edited, whether a
 * write left the scope. A user reads the sentence and never the journal. So
 * the envelope foots the answer with two lines derived from the counters --
 * never from the model -- in the shape of the test tier's M305 header,
 * `checked` and `NOT checked`, for a run. GATE_INTEGRITY.md's STATE-THE-REACH
 * made the shell's reach visible BEFORE the run; this is its run-END half.
 *
 * Pure: a struct of counts in, text or JSON out. The counts come from the
 * headless sink (tool calls, tool errors -- these exist with or without an
 * envelope) and from the envelope when one is armed. */
#ifndef JC_REACH_H
#define JC_REACH_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct cJSON;

struct jc_reach {
    int envelope;         /* an envelope was armed at all (--auto bounds)  */
    int verifier_armed;   /* the envelope had a verify command             */
    int verify_green;     /* outcome OK with a verifier armed              */
    int verify_red;       /* outcome VERIFY_FAILED                         */
    int rolled_back;      /* the verifier's rollback fired                 */
    int scope_armed;      /* an --edit-scope was set                       */
    int scope_violations; /* writes seen outside it                        */
    int shell_ran;        /* a shell tool ran: its changes are unattributed */
    int tool_calls;       /* attempted                                     */
    int tool_errors;      /* results with is_error                         */
    int tool_refused;     /* M638: of those, stopped by a fence            */
    int test_edits;       /* test-assertion edits the envelope counted     */
    /* M631: the plan artifact, reconciled. plan_present 0 => no plan file,
     * and the footer says NOTHING about plans (no invented absence). */
    int plan_present;
    int plan_named;       /* files ## Touches predicted                    */
    int plan_touched;     /* of those, written this run                    */
    int plan_drift;       /* files written that the plan did not name      */
    const char *plan_drift_list; /* comma-joined, or NULL                  */
};

/* Two lines into buf -- "checked: ...\nnot checked: ..." -- each half naming
 * every fact that belongs to it. A zero is a claim ("0 errors"), so counts are
 * always printed; absences are stated ("no verifier armed"), never left blank
 * (M316: silence is indistinguishable from a pass). */
void jc_reach_line(const struct jc_reach *r, char *buf, jc_size cap);

/* The same facts as a JSON object for the `done` event's `reach` member:
 * verify ("green"|"red"|"none"), tool_calls, tool_errors, test_edits,
 * scope ("clean"|"violated"|"none"), shell_ran, checked, not_checked. Caller
 * owns the object. */
struct cJSON *jc_reach_json(const struct jc_reach *r);

#ifdef __cplusplus
}
#endif
#endif /* JC_REACH_H */
