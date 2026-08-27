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

    /* --- NULL-safety: both delegation tools call this on failure paths ------ */
    jc_sb_init(&sb);
    jc_delegreport_render(NULL, &sb);
    JC_CHECK(sb.data == NULL || sb.data[0] == '\0');
    jc_sb_free(&sb);
    jc_delegreport_init(&r);
    jc_delegreport_render(&r, NULL);   /* must not crash */
    jc_delegreport_init(NULL);         /* must not crash */
}
