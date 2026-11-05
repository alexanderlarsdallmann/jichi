/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_delegreport.c - the delegated-run report (see jc_delegreport.h). */

#include "jc_delegreport.h"
#include "jc_snprintf.h"

#include <string.h>
#include <time.h>     /* M876: gmtime for the failure times */

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
    case JC_DELEG_TASK_BUDGET: return "task_budget";   /* M861 */
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
        if (strcmp(name, "task_budget") == 0) { return JC_DELEG_TASK_BUDGET; }
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
    /* M861 (N28): 2 is the TASK's own budget (max_tool_calls) -- the run goes
     * on, so the run budget's "do not delegate again" would be wrong advice. */
    if (budget_stopped == 2) {
        return JC_DELEG_TASK_BUDGET;
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
    case JC_DELEG_TASK_BUDGET:
        return "it used the tool calls its task allowed (max_tool_calls), so this "
               "answer may be partial -- give what is left to a new task, or "
               "finish it yourself";
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
static const char *fail_advice_for(enum jc_fail_class c, int count)
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
    case JC_FAIL_AT_BOUND:
        /* M876 (N47 (c)): "kept hitting" only when it did -- run 0094's report
         * said it of the call that was not why the run ended. */
        if (count >= 2) {
            return "a command it ran kept hitting its own time limit and being "
                   "cut off -- it does not finish in the time given, so "
                   "re-running the same subtask will time out again; narrow or "
                   "restructure it";
        }
        return "a command it ran hit its own time limit and was cut off -- it "
               "may not finish in the time given: give it more time, or narrow "
               "the subtask";
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
    int model_ended;   /* M876: the run ended on its model calls */

    if (r == NULL || out == NULL) {
        return;
    }
    have_measure = (r->tool_calls >= 0 || r->tokens >= 0.0);
    have_fail = (r->fail_tool[0] != '\0');
    model_ended = (r->stop == JC_DELEG_ERROR && r->run_error[0] != '\0');

    /* A clean, unmeasured delegation gets NO block. The report exists to carry
     * information a parent cannot otherwise get; "it finished" is already implied
     * by an answer arriving, and a line saying so on every delegation is the
     * per-round nag M347 rejected on measured evidence. */
    if (r->stop == JC_DELEG_DONE && !have_measure && !have_fail &&
        r->files_changed[0] == '\0' && !r->cut && !model_ended) {
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
    /* M876 (N47 (a)): what ended the run comes first, when it was the model
     * calls -- before the files and the last failing call, which a reader takes
     * for the cause because it is the only failure named. */
    if (model_ended) {
        jc_sb_append(out, "\n[delegate] the run ended because its model calls "
                     "failed: ");
        jc_sb_append(out, r->run_error);
        if (r->run_error_attempts > 0) {
            jc_snprintf(buf, sizeof buf, " (%d attempt%s", r->run_error_attempts,
                        r->run_error_attempts == 1 ? "" : "s");
            jc_sb_append(out, buf);
            if (r->run_error_t0 > 0 && r->run_error_t1 >= r->run_error_t0) {
                time_t a = (time_t)r->run_error_t0;
                time_t b = (time_t)r->run_error_t1;
                struct tm *ta = gmtime(&a);
                int ah = 0, am = 0, as = 0;
                if (ta != NULL) {
                    ah = ta->tm_hour;
                    am = ta->tm_min;
                    as = ta->tm_sec;
                }
                {
                    struct tm *tb = gmtime(&b);
                    if (ta != NULL && tb != NULL) {
                        jc_snprintf(buf, sizeof buf,
                                    ", %02d:%02d:%02d-%02d:%02d:%02d UTC",
                                    ah, am, as, tb->tm_hour, tb->tm_min,
                                    tb->tm_sec);
                        jc_sb_append(out, buf);
                    }
                }
            }
            jc_sb_append(out, ")");
        }
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
        if (model_ended) {
            /* M876 (N47 (b)): an earlier event, not the cause -- and its remedy
             * would answer a cause that was not the cause, so it is not given. */
            jc_sb_append(out, "\n[delegate] (that call came earlier; it is not "
                         "why the run ended)");
        } else if (r->fail_stopped && r->fail_cls == JC_FAIL_KILLED) {
            /* M885 (N47, continued): the stop killed it, so "a limit killed the
             * call -- split it" would answer a limit that was never met. */
            jc_sb_append(out, "\n[delegate] (that call was killed because the "
                         "run was being stopped, not by a limit)");
        } else {
            adv = fail_advice_for(r->fail_cls, r->fail_count);
            if (adv != NULL) {
                jc_sb_append(out, "\n[delegate] ");
                jc_sb_append(out, adv);
                jc_sb_append(out, ".");
            }
        }
    }
    /* M858 (N42): a cut last reply. Its text reads like an answer and stops
     * mid-sentence; without this line nothing tells the parent. */
    if (r->cut) {
        jc_sb_append(out, "\n[delegate] its last reply was cut off at the output "
                     "ceiling -- the answer may stop mid-sentence; do not take "
                     "it as finished.");
    }
    adv = advice_for(r->stop);
    if (r->stop == JC_DELEG_MAX_ITERS && r->looped > 0) {
        adv = "it repeated the same call without progress and was stopped, its "
              "work kept -- the same subtask will most likely loop the same way: "
              "narrow it, or finish it yourself";
    }
    if (r->stop == JC_DELEG_ABORTED && r->term) {
        /* M885 (N47, continued): a SIGTERM from outside is not the operator's
         * interrupt, and its advice is not an order to the parent's own run. */
        adv = "it was stopped from outside the run (SIGTERM), not by an "
              "operator's interrupt, so its answer may be partial -- this says "
              "nothing about your own run, which goes on: check what it did "
              "before relying on it";
    }
    if (r->stop == JC_DELEG_BUDGET && r->share_only) {
        adv = "it used its share of the run's budget, so this answer may be "
              "partial -- the run itself has budget left: finish the rest "
              "yourself, or delegate it within what is left";
    }
    if (adv != NULL) {
        jc_sb_append(out, "\n[delegate] ");
        jc_sb_append(out, adv);
        jc_sb_append(out, ".");
    }
}
