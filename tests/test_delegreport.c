/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_delegreport.c - the delegated-run report (M437).
 *
 * THE DEFECT UNDER TEST. `spawn_subagent` answered a failure with one of two
 * fixed strings, so a parent could not tell an edit-scope denial from a tool
 * error from a refusal. Its only moves were to re-delegate identically -- paying
 * the whole subtask a second time -- or to give up. Both are expensive on a run
 * metered by tokens and neither is informed.
 *
 * What is asserted here is mostly about RESTRAINT and PRECEDENCE, because those
 * are the two places a report like this goes wrong: it either says something on
 * every call until nobody reads it (the nag M347 rejected on measured evidence),
 * or it describes a run with the wrong one of two true facts.
 */
#include "jc_test.h"
#include "jc_delegreport.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>

static void render(const struct jc_delegate_report *r, struct jc_sb *sb)
{
    jc_sb_init(sb);
    jc_delegreport_render(r, sb);
}

void test_delegreport(void)
{
    struct jc_delegate_report r;
    struct jc_sb sb;

    /* --- the names round-trip -----------------------------------------------
     * The fork pool sends the stop reason as a NAME over the pipe, because a name
     * survives an enum renumbering and an integer does not. That makes the pair
     * name/parse a wire contract, so it is asserted in both directions. */
    JC_CHECK(strcmp(jc_delegreport_stop_name(JC_DELEG_DONE), "done") == 0);
    JC_CHECK(jc_delegreport_stop_parse("max_iters") == JC_DELEG_MAX_ITERS);
    JC_CHECK(jc_delegreport_stop_parse("budget") == JC_DELEG_BUDGET);
    JC_CHECK(jc_delegreport_stop_parse("no_answer") == JC_DELEG_NO_ANSWER);
    JC_CHECK(jc_delegreport_stop_parse("aborted") == JC_DELEG_ABORTED);
    JC_CHECK(jc_delegreport_stop_parse("error") == JC_DELEG_ERROR);
    JC_CHECK(jc_delegreport_stop_parse("task_budget") == JC_DELEG_TASK_BUDGET);
    JC_CHECK(strcmp(jc_delegreport_stop_name(JC_DELEG_TASK_BUDGET), "task_budget") == 0);
    /* An unknown or absent name must not invent a reason. The caller checks for
     * an empty string before calling, so "done" is the safe floor. */
    JC_CHECK(jc_delegreport_stop_parse("wat") == JC_DELEG_DONE);
    JC_CHECK(jc_delegreport_stop_parse("") == JC_DELEG_DONE);
    JC_CHECK(jc_delegreport_stop_parse(NULL) == JC_DELEG_DONE);

    /* --- precedence: BUDGET outranks MAX_ITERS ------------------------------
     * A run can be both. The cap exit sets `capped` on the way out of a loop whose
     * budget check had already fired, so the flags are not exclusive -- and the
     * remedies are OPPOSITE. An iteration cap invites a narrower re-delegation; an
     * exhausted budget forbids delegating at all. Reporting the cap there would
     * send the parent to spend a budget that is already gone. */
    JC_CHECK(jc_delegreport_stop_from(1, 0, 1, 1, 1) == JC_DELEG_BUDGET);
    JC_CHECK(jc_delegreport_stop_from(1, 0, 1, 0, 1) == JC_DELEG_MAX_ITERS);
    /* M861: 2 is the TASK's own budget, which also outranks the cap; an abort
     * still outranks it. */
    JC_CHECK(jc_delegreport_stop_from(1, 0, 1, 2, 1) == JC_DELEG_TASK_BUDGET);
    JC_CHECK(jc_delegreport_stop_from(1, 1, 0, 2, 1) == JC_DELEG_ABORTED);
    /* An abort outranks everything: re-delegating after the operator interrupted
     * the run is the wrong move whatever else was true of it. */
    JC_CHECK(jc_delegreport_stop_from(1, 1, 1, 1, 1) == JC_DELEG_ABORTED);
    JC_CHECK(jc_delegreport_stop_from(0, 0, 0, 0, 1) == JC_DELEG_ERROR);
    /* An empty answer with an OK status is its own outcome, not "done": M322 made
     * exactly this distinction for the top-level turn after a machine driver was
     * told `done` with nothing in hand. */
    JC_CHECK(jc_delegreport_stop_from(1, 0, 0, 0, 0) == JC_DELEG_NO_ANSWER);
    JC_CHECK(jc_delegreport_stop_from(1, 0, 0, 0, 1) == JC_DELEG_DONE);

    /* --- restraint: a clean unmeasured delegation renders NOTHING -----------
     * The report exists to carry what a parent cannot otherwise get. That an
     * answer arrived already says the delegate finished, so a block saying so on
     * every single delegation is pure cost -- and the habit of skipping a line
     * that is usually empty is what makes the line that matters invisible. */
    jc_delegreport_init(&r);
    render(&r, &sb);
    JC_CHECK(sb.data == NULL || sb.data[0] == '\0');
    jc_sb_free(&sb);

    /* --- but a clean delegation WITH measurements does report -------------- */
    jc_delegreport_init(&r);
    r.tool_calls = 18;
    r.tokens = 24100.0;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "stop=done") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "18 tool calls") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "24.1k tokens") != NULL);
    jc_sb_free(&sb);

    /* Singular, because "1 tool calls" in a line a model reads every turn is the
     * kind of sloppiness that makes the rest of the line look unconsidered. */
    jc_delegreport_init(&r);
    r.tool_calls = 1;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "1 tool call") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "1 tool calls") == NULL);
    jc_sb_free(&sb);

    /* --- not measured is not zero ------------------------------------------
     * The counters live on the envelope, so they exist only under --auto. A zero
     * would assert the delegate made no tool calls, which is a different claim and
     * usually a false one. Absent means absent. */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "tool call") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "tokens") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "0 tool") == NULL);
    jc_sb_free(&sb);

    /* --- the decisive field: a DENIAL is named as policy, not as an error ---
     * This is the whole reason the milestone exists. The parent's move for a
     * denial is categorically different from its move for a flaky tool: the
     * delegate cannot widen its own fence, so re-delegating is guaranteed to fail
     * again, and the honest outcome is to report the denial upward. */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_DONE;
    r.fail_cls = JC_FAIL_DENIED;
    jc_snprintf(r.fail_tool, sizeof r.fail_tool, "write_file");
    jc_snprintf(r.fail_msg, sizeof r.fail_msg,
                "refused: outside this run's edit scope");
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "write_file") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "(denied)") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "edit scope") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "refused again") != NULL);
    /* Reported even though the delegation SUCCEEDED: an answer produced after a
     * denial is a different thing from one produced cleanly, and the parent is the
     * only party that can act on the denial. */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "stop=done") != NULL);
    jc_sb_free(&sb);

    /* Each class carries its own remedy, and they are not interchangeable -- a
     * wrong cause is a loop amplifier (M342), which is why this checks that the
     * not_found advice does NOT appear on a killed call. */
    jc_delegreport_init(&r);
    r.fail_cls = JC_FAIL_KILLED;
    jc_snprintf(r.fail_tool, sizeof r.fail_tool, "run_terminal_command");
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "(killed)") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "split it") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "check the path") == NULL);
    jc_sb_free(&sb);

    /* --- every stop reason names a next move ------------------------------- */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_BUDGET;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "do not delegate again") != NULL);
    jc_sb_free(&sb);

    /* M869 (def-305): a MAX_ITERS stop that was a loop stop -- not "narrower". */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    r.looped = 2;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "repeated the same call") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "NARROWER") == NULL);
    jc_sb_free(&sb);

    /* M861: a parallel child's SHARE of the run's budget, while the run has
     * budget left -- no "do not delegate again" either. */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_BUDGET;
    r.share_only = 1;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "stop=budget") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "its share of the run's budget") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "do not delegate again") == NULL);
    jc_sb_free(&sb);

    /* M861: the task's budget -- the run goes on, so no "do not delegate again". */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_TASK_BUDGET;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "stop=task_budget") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "max_tool_calls") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "do not delegate again") == NULL);
    jc_sb_free(&sb);

    jc_delegreport_init(&r);
    r.stop = JC_DELEG_MAX_ITERS;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "NARROWER") != NULL);
    jc_sb_free(&sb);

    jc_delegreport_init(&r);
    r.stop = JC_DELEG_NO_ANSWER;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "unchanged") != NULL);
    jc_sb_free(&sb);

    /* --- files_changed, the one field only the fork pool can fill ---------- */
    jc_delegreport_init(&r);
    jc_snprintf(r.files_changed, sizeof r.files_changed, "src/a.c, tests/b.c");
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "files changed:") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "tests/b.c") != NULL);
    jc_sb_free(&sb);

    /* --- M858 (N42): a cut last reply ------------------------------------ */
    /* A clean, unmeasured delegation renders nothing (the M347 rule) -- but a
     * cut reply is information: its text reads like an answer and stops
     * mid-sentence. The line must appear even on an otherwise clean report. */
    jc_delegreport_init(&r);
    r.cut = 1;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "cut off at the output ceiling") != NULL);
    jc_sb_free(&sb);
    jc_delegreport_init(&r);
    render(&r, &sb);
    JC_CHECK(sb.data == NULL || strstr(sb.data, "cut off") == NULL);
    jc_sb_free(&sb);

    /* --- M876 (the demos drive's N47): the run's own model failure first ------
     * Run 0094's worker failed on the gateway; its report named only its last
     * failing TOOL call, a time limit, and its lead planned around that. */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_ERROR;
    jc_snprintf(r.fail_tool, sizeof r.fail_tool, "run_terminal_command");
    r.fail_cls = JC_FAIL_AT_BOUND;
    r.fail_count = 3;
    jc_snprintf(r.run_error, sizeof r.run_error,
                "could not connect within 20s (timeouts.connect)");
    r.run_error_attempts = 12;
    r.run_error_t0 = 1790951327L;   /* 2026-10-02 14:28:47 UTC */
    r.run_error_t1 = 1790951638L;   /* 2026-10-02 14:33:58 UTC */
    render(&r, &sb);
    if (JC_REQUIRE(sb.data != NULL)) {
        const char *re = strstr(sb.data, "the run ended because its model calls "
                                         "failed: could not connect within 20s");
        const char *lf = strstr(sb.data, "last failing call: run_terminal_command");
        /* (a) named, with its attempts and times, and BEFORE the tool call */
        JC_CHECK(re != NULL && lf != NULL && re < lf);
        JC_CHECK(strstr(sb.data, "(12 attempts, 14:28:47-14:33:58 UTC)") != NULL);
        /* (b) the tool call is an earlier event, and its remedy is not given */
        JC_CHECK(strstr(sb.data, "it is not why the run ended") != NULL);
        JC_CHECK(strstr(sb.data, "time limit") == NULL);
    }
    jc_sb_free(&sb);
    /* no model failure recorded: the tool call's remedy, as before */
    r.run_error[0] = '\0';
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "model calls failed") == NULL &&
             strstr(sb.data, "kept hitting its own time limit") != NULL);
    jc_sb_free(&sb);
    /* a model failure on a run that did not END on it (stop done) is not named */
    jc_snprintf(r.run_error, sizeof r.run_error, "HTTP 502 from the provider");
    r.stop = JC_DELEG_DONE;
    render(&r, &sb);
    JC_CHECK(sb.data == NULL || strstr(sb.data, "model calls failed") == NULL);
    jc_sb_free(&sb);
    /* (c) "kept hitting" only when the class repeated */
    jc_delegreport_init(&r);
    jc_snprintf(r.fail_tool, sizeof r.fail_tool, "run_terminal_command");
    r.fail_cls = JC_FAIL_AT_BOUND;
    r.fail_count = 1;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "kept hitting") == NULL &&
             strstr(sb.data, "hit its own time limit and was cut off") != NULL);
    jc_sb_free(&sb);
    r.fail_count = 0;   /* an older child: not known, read as once */
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "kept hitting") == NULL);
    jc_sb_free(&sb);
    r.fail_count = 2;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "kept hitting") != NULL);
    jc_sb_free(&sb);

    /* --- M885 (the demos drive's N47, continued): a SIGTERM from outside ----
     * Run 0102: a supervisor stopped one worker with SIGTERM; the batch result
     * said "the operator interrupted the run -- stop and report, do not restart
     * it", about the run, and "a limit killed the call ... split it", about the
     * shell call the SIGTERM had killed. The lead wrote the first into the
     * team's log as the operator's interruption. */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_ABORTED;
    r.term = 1;
    render(&r, &sb);
    if (JC_REQUIRE(sb.data != NULL)) {
        JC_CHECK(strstr(sb.data, "operator interrupted") == NULL);
        JC_CHECK(strstr(sb.data, "stopped from outside the run (SIGTERM)") != NULL);
        JC_CHECK(strstr(sb.data, "your own run, which goes on") != NULL);
    }
    jc_sb_free(&sb);
    r.term = 0;   /* the operator's interrupt keeps its advice */
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "operator interrupted") != NULL &&
             strstr(sb.data, "SIGTERM") == NULL);
    jc_sb_free(&sb);
    /* the killed call: no limit advice when the run's own stop killed it */
    jc_delegreport_init(&r);
    r.stop = JC_DELEG_ABORTED;
    jc_snprintf(r.fail_tool, sizeof r.fail_tool, "run_terminal_command");
    r.fail_cls = JC_FAIL_KILLED;
    r.fail_stopped = 1;
    render(&r, &sb);
    if (JC_REQUIRE(sb.data != NULL)) {
        JC_CHECK(strstr(sb.data, "a limit killed the call") == NULL);
        JC_CHECK(strstr(sb.data, "killed because the run was being stopped") != NULL);
    }
    jc_sb_free(&sb);
    r.fail_stopped = 0;   /* a kill the stop did not cause keeps its advice */
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "a limit killed the call") != NULL &&
             strstr(sb.data, "was being stopped") == NULL);
    jc_sb_free(&sb);
    r.fail_stopped = 1;   /* the flag speaks only for a killed call */
    r.fail_cls = JC_FAIL_NOT_FOUND;
    render(&r, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "was being stopped") == NULL &&
             strstr(sb.data, "could not find what the task named") != NULL);
    jc_sb_free(&sb);

    /* --- NULL-safety: both delegation tools call this on failure paths ------ */
    jc_sb_init(&sb);
    jc_delegreport_render(NULL, &sb);
    JC_CHECK(sb.data == NULL || sb.data[0] == '\0');
    jc_sb_free(&sb);
    jc_delegreport_init(&r);
    jc_delegreport_render(&r, NULL);   /* must not crash */
    jc_delegreport_init(NULL);         /* must not crash */
}
