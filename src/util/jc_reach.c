/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_reach.c - see the header. Pure over a struct of counts. */
#include "jc_reach.h"
#include "jc_outcome.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <string.h>

static void reach_halves(const struct jc_reach *r, char *checked,
                         jc_size ccap, char *unchecked, jc_size ucap)
{
    jc_size n = 0;
    jc_size m = 0;

    checked[0] = '\0';
    unchecked[0] = '\0';
    /* M688: an armed verifier that reached NEITHER verdict is reported below,
     * in the not-checked half. It used to print "verify did not conclude" HERE
     * -- on the checked side -- which is the wrong side of the colon: a
     * verifier that never concluded is the definition of something that was not
     * checked. Found in the same audit that found the budget cell empty, in the
     * same printed block, where "verify did not conclude" sat two words from
     * "not checked: (nothing)". */
    if (r->verifier_armed && (r->verify_red || r->verify_green)) {
        n += jc_snprintf(checked + n, ccap - n, "verify %s%s",
                         r->verify_red ? "RED" : "green",
                         r->rolled_back ? " (work rolled back to the last green)" : "");
    }
    /* M638: the refusals are printed beside the errors they are a part of.
     * Read in anger, "23 errors" sent the reader to the transcript before
     * any file; four of them were the plan-mode fence doing its job. The
     * number meant to worry the reader should say how much of it is a fence
     * working -- and the zero is a claim like every other count. */
    n += jc_snprintf(checked + n, ccap - n, "%s%d tool call%s, %d error%s "
                     "(%d refused by a fence)",
                     n > 0 ? " · " : "", r->tool_calls,
                     r->tool_calls == 1 ? "" : "s", r->tool_errors,
                     r->tool_errors == 1 ? "" : "s", r->tool_refused);
    if (r->envelope) {
        n += jc_snprintf(checked + n, ccap - n, " · %d test edit%s",
                         r->test_edits, r->test_edits == 1 ? "" : "s");
    }
    if (r->plan_present) {
        n += jc_snprintf(checked + n, ccap - n, " · plan: %d of %d predicted "
                         "file%s touched", r->plan_touched, r->plan_named,
                         r->plan_named == 1 ? "" : "s");
        /* M638: when the shell ran and the plan is short, say WHY in the
         * same clause. The not-checked half already says the shell's writes
         * are unattributed; read in anger, the first line was read before the
         * second and "3 of 5" was doubted on its own. */
        if (r->shell_ran && r->plan_touched < r->plan_named) {
            n += jc_snprintf(checked + n, ccap - n, " (%d unaccounted for -- "
                             "a shell command ran; its writes are not "
                             "attributed)", r->plan_named - r->plan_touched);
        }
        if (r->plan_drift > 0) {
            n += jc_snprintf(checked + n, ccap - n, " · plan drift: %s",
                             r->plan_drift_list != NULL ? r->plan_drift_list
                                                        : "(unnamed)");
        }
    }
    if (r->scope_armed) {
        if (r->scope_violations > 0) {
            n += jc_snprintf(checked + n, ccap - n,
                             " · %d write%s OUTSIDE the edit scope",
                             r->scope_violations,
                             r->scope_violations == 1 ? "" : "s");
        } else {
            n += jc_snprintf(checked + n, ccap - n, " · writes in scope");
        }
    }
    /* M689: a shell command that provably changed nothing is a CHECKED fact,
     * not an unchecked one -- the sweep looked and the tree was identical. It
     * earns a place on this side precisely because the alternative was a
     * warning that fired on `ls` and taught readers to skip the line. */
    if (r->shell_ran && r->shell_wrote_nothing) {
        n += jc_snprintf(checked + n, ccap - n,
                         "%sa shell command ran and changed nothing",
                         n > 0 ? " · " : "");
    }

    /* M687, widened at M688: FIRST, and in both branches. A truncated answer is
     * the largest thing that was not checked, and it is not conditional on an
     * envelope -- the run that prompted M687 armed one and was told
     * "(nothing -- a verifier and an edit scope were armed)" about a turn cut
     * off mid-task. M687 asked only about the iteration cap; the audit in
     * docs/plans/2026-09-run-outcome.md then found a `--max-tool-calls` run
     * printing the same false "(nothing)" beside an EMPTY answer, so the test
     * is now `answer_truncated` -- one question that covers the cap, a budget,
     * a deadline, an interrupt and an error, and cannot be left empty for the
     * next one. Placed ahead of the rest so it is not read as a footnote. */
    if (r->answer_truncated && r->answer_capped) {
        /* The run FINISHED; the reply did not fit. Saying "the run did not
         * finish" here would be false, and it is the branch a NULL stop_clause
         * would otherwise take -- see tests/test_reach.c. The remedy is named
         * because it is actionable and the provider already knows it. */
        m += jc_snprintf(unchecked + m, ucap - m,
                         "THE ANSWER IS INCOMPLETE -- the reply hit the OUTPUT "
                         "CEILING, so what the model had said by then is the "
                         "whole of it; raise this model's maxTokens");
    } else if (r->answer_truncated) {
        m += jc_snprintf(unchecked + m, ucap - m,
                         "THE ANSWER IS INCOMPLETE -- the run %s, so what the "
                         "model had said by then is the whole of it and the "
                         "task may be unfinished",
                         r->stop_clause != NULL ? r->stop_clause
                                                : "did not finish");
    }
    if (!r->envelope) {
        m += jc_snprintf(unchecked + m, ucap - m,
                         "%sno envelope armed -- no verifier, no edit scope, no "
                         "budget; nothing about this run's result was tested "
                         "(see docs/AUTONOMY.md)", m > 0 ? " · " : "");
    } else {
        if (!r->verifier_armed) {
            m += jc_snprintf(unchecked + m, ucap - m, "%sno verifier armed -- "
                             "the answer's claim of success was not tested",
                             m > 0 ? " · " : "");
        } else if (!r->verify_red && !r->verify_green) {
            /* M688: armed, but it never returned a verdict -- the run stopped
             * first. Distinct from "no verifier armed": somebody DID arm one,
             * and the reason it says nothing is the stop, not the setup. */
            m += jc_snprintf(unchecked + m, ucap - m, "%sthe verifier never "
                             "reached a verdict -- it was armed, the run "
                             "stopped first", m > 0 ? " · " : "");
        }
        if (!r->scope_armed) {
            m += jc_snprintf(unchecked + m, ucap - m, "%sno edit scope -- "
                             "writes were not fenced", m > 0 ? " · " : "");
        }
        if (r->verify_no_count) {
            m += jc_snprintf(unchecked + m, ucap - m, "%sthe verifier passed "
                             "but printed no test count, so nothing could be "
                             "checked about what it ran (a gate that is silent "
                             "on success hides a gate that ran nothing)",
                             m > 0 ? " · " : "");
        }
        if (r->shell_ran && !r->shell_wrote_nothing) {
            m += jc_snprintf(unchecked + m, ucap - m, "%sa shell command ran "
                             "-- changes it made are not attributed to the run",
                             m > 0 ? " · " : "");
        }
        if (m == 0) {
            jc_snprintf(unchecked, ucap, "(nothing -- a verifier and an edit "
                        "scope were armed)");
        }
    }
    (void)n;
}

void jc_reach_line(const struct jc_reach *r, char *buf, jc_size cap)
{
    char checked[640];
    char unchecked[512];
    if (r == NULL || buf == NULL || cap == 0) {
        return;
    }
    reach_halves(r, checked, sizeof checked, unchecked, sizeof unchecked);
    jc_snprintf(buf, cap, "checked: %s\nnot checked: %s", checked, unchecked);
}

struct cJSON *jc_reach_json(const struct jc_reach *r)
{
    char checked[640];
    char unchecked[512];
    cJSON *o;
    if (r == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    reach_halves(r, checked, sizeof checked, unchecked, sizeof unchecked);
    cJSON_AddStringToObject(o, "verify",
        !r->verifier_armed ? "none" : (r->verify_red ? "red"
                                       : (r->verify_green ? "green" : "unknown")));
    cJSON_AddNumberToObject(o, "tool_calls", (double)r->tool_calls);
    cJSON_AddNumberToObject(o, "tool_errors", (double)r->tool_errors);
    cJSON_AddNumberToObject(o, "tool_refused", (double)r->tool_refused);
    cJSON_AddNumberToObject(o, "test_edits", (double)r->test_edits);
    cJSON_AddStringToObject(o, "scope",
        !r->scope_armed ? "none" : (r->scope_violations > 0 ? "violated" : "clean"));
    cJSON_AddBoolToObject(o, "shell_ran", r->shell_ran ? 1 : 0);
    if (r->plan_present) {
        cJSON_AddNumberToObject(o, "plan_named", (double)r->plan_named);
        cJSON_AddNumberToObject(o, "plan_touched", (double)r->plan_touched);
        cJSON_AddNumberToObject(o, "plan_drift", (double)r->plan_drift);
        if (r->plan_drift_list != NULL) {
            cJSON_AddStringToObject(o, "plan_drift_paths", r->plan_drift_list);
        }
    }
    cJSON_AddStringToObject(o, "checked", checked);
    cJSON_AddStringToObject(o, "not_checked", unchecked);
    return o;
}
