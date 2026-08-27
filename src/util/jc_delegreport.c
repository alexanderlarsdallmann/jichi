/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_delegreport.c - the delegated-run report (see jc_delegreport.h). */

#include "jc_delegreport.h"
#include "jc_snprintf.h"

#include <string.h>

void jc_delegreport_init(struct jc_delegate_report *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->stop = JC_DELEG_DONE;
    r->fail_cls = JC_FAIL_OTHER;
    /* NOT zero: see the header. A zero would claim the delegate made no calls. */
    r->tool_calls = -1;
    r->tokens = -1.0;
}

const char *jc_delegreport_stop_name(enum jc_deleg_stop s)
{
    switch (s) {
    case JC_DELEG_MAX_ITERS: return "max_iters";
    case JC_DELEG_BUDGET:    return "budget";
    case JC_DELEG_NO_ANSWER: return "no_answer";
    case JC_DELEG_ABORTED:   return "aborted";
    case JC_DELEG_ERROR:     return "error";
    case JC_DELEG_DONE:      break;
    }
    return "done";
}

enum jc_deleg_stop jc_delegreport_stop_parse(const char *name)
{
    if (name != NULL) {
        if (strcmp(name, "max_iters") == 0) { return JC_DELEG_MAX_ITERS; }
        if (strcmp(name, "budget") == 0)    { return JC_DELEG_BUDGET; }
        if (strcmp(name, "no_answer") == 0) { return JC_DELEG_NO_ANSWER; }
        if (strcmp(name, "aborted") == 0)   { return JC_DELEG_ABORTED; }
        if (strcmp(name, "error") == 0)     { return JC_DELEG_ERROR; }
    }
    return JC_DELEG_DONE;
}

enum jc_deleg_stop jc_delegreport_stop_from(int run_status_ok, int aborted,
                                           int capped, int budget_stopped,
                                           int have_answer)
{
    /* Order matters, and it is the order of what the parent must do about it.
     * ABORTED first: an operator interrupt outranks every other description of
     * the same run, and re-delegating after one is the wrong move.
     * Then a hard failure. Then BUDGET before MAX_ITERS: both may leave a partial
     * answer, but their remedies are opposite -- an iteration cap invites a
     * narrower re-delegation, an exhausted budget forbids delegating at all, and
     * a run can be both at once (the cap exit sets `capped` on the way out of a
     * loop whose budget check had already fired). */
    if (aborted) {
        return JC_DELEG_ABORTED;
    }
    if (!run_status_ok) {
        return JC_DELEG_ERROR;
    }
    if (budget_stopped) {
        return JC_DELEG_BUDGET;
    }
    if (capped) {
        return JC_DELEG_MAX_ITERS;
    }
    if (!have_answer) {
        return JC_DELEG_NO_ANSWER;
    }
    return JC_DELEG_DONE;
}

/* What the parent should DO. A line naming only a cause is the message class
 * M342 measured as a loop amplifier, so every stop reason carries its remedy. */
static const char *advice_for(enum jc_deleg_stop s)
{
    switch (s) {
    case JC_DELEG_MAX_ITERS:
        return "it ran out of iterations, so this answer may be partial -- "
               "re-delegate a NARROWER subtask, or finish the remainder yourself";
    case JC_DELEG_BUDGET:
        return "this RUN's budget is spent, so do not delegate again -- write up "
               "what you have";
    case JC_DELEG_NO_ANSWER:
        return "it ended with nothing to say, so re-running it unchanged will "
               "most likely do the same -- state the subtask more concretely, or "
               "do it yourself";
    case JC_DELEG_ABORTED:
        return "the operator interrupted the run -- stop and report, do not "
               "restart it";
    case JC_DELEG_ERROR:
        return "the delegated run itself failed rather than the task -- retrying "
               "once is reasonable; twice is not";
    case JC_DELEG_DONE:
    default:
        return NULL;
    }
}

/* The remedy for the delegate's last failure, keyed off the class the loop
 * already computed. Distinct from the tool-loop advice (jc_toolloop_render),
 * which addresses the agent that MADE the call; this addresses its parent, whose
 * available moves are different -- notably that a denial is the parent's problem
 * to route around, since the delegate cannot widen its own fence. */
static const char *fail_advice_for(enum jc_fail_class c)
{
    switch (c) {
    case JC_FAIL_DENIED:
        return "a policy refusal, not an accident: re-delegating the same task "
               "will be refused again. Either do that part within what you are "
               "permitted, or say in your final answer what the task needs and "
               "was denied";
    case JC_FAIL_NOT_FOUND:
        return "it could not find what the task named -- check the path or "
               "symbol yourself before delegating again";
    case JC_FAIL_BAD_ARGS:
        return "the delegate mis-shaped a tool call; a clearer subtask usually "
               "fixes this";
    case JC_FAIL_KILLED:
        return "a limit killed the call, so a larger version of the same subtask "
               "will be killed too -- split it";
    case JC_FAIL_NONZERO_EXIT:
        return "a command it ran reported failure; the cause is in that output, "
               "not in how the task was worded";
    case JC_FAIL_OTHER:
    default:
        return NULL;
    }
}

void jc_delegreport_render(const struct jc_delegate_report *r,
                           struct jc_sb *out)
{
    char buf[600];
    const char *adv;
    int have_measure;
    int have_fail;

    if (r == NULL || out == NULL) {
        return;
    }
    have_measure = (r->tool_calls >= 0 || r->tokens >= 0.0);
    have_fail = (r->fail_tool[0] != '\0');

    /* A clean, unmeasured delegation gets NO block. The report exists to carry
     * information a parent cannot otherwise get; "it finished" is already implied
     * by an answer arriving, and a line saying so on every delegation is the
     * per-round nag M347 rejected on measured evidence. */
    if (r->stop == JC_DELEG_DONE && !have_measure && !have_fail &&
        r->files_changed[0] == '\0') {
        return;
    }

    jc_sb_append(out, "\n\n[delegate] stop=");
    jc_sb_append(out, jc_delegreport_stop_name(r->stop));
    if (r->tool_calls >= 0) {
        jc_snprintf(buf, sizeof buf, " \xc2\xb7 %ld tool call%s", r->tool_calls,
                    (r->tool_calls == 1) ? "" : "s");
        jc_sb_append(out, buf);
    }
    if (r->tokens >= 0.0) {
        jc_snprintf(buf, sizeof buf, " \xc2\xb7 %.1fk tokens", r->tokens / 1000.0);
        jc_sb_append(out, buf);
    }
    if (r->files_changed[0] != '\0') {
        jc_sb_append(out, "\n[delegate] files changed: ");
        jc_sb_append(out, r->files_changed);
    }
    if (have_fail) {
        jc_snprintf(buf, sizeof buf, "\n[delegate] last failing call: %s (%s)",
                    r->fail_tool, jc_fail_class_name(r->fail_cls));
        jc_sb_append(out, buf);
        if (r->fail_msg[0] != '\0') {
            jc_sb_append(out, " -- ");
            jc_sb_append(out, r->fail_msg);
        }
        adv = fail_advice_for(r->fail_cls);
        if (adv != NULL) {
            jc_sb_append(out, "\n[delegate] ");
            jc_sb_append(out, adv);
            jc_sb_append(out, ".");
        }
    }
    adv = advice_for(r->stop);
    if (adv != NULL) {
        jc_sb_append(out, "\n[delegate] ");
        jc_sb_append(out, adv);
        jc_sb_append(out, ".");
    }
}
