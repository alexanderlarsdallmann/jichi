/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_envelope.c - tests for the autonomy-envelope helpers (jc_envelope.c).
 *
 * The parse / glob / scope / budget helpers are pure and exhaustively checked
 * here; jc_env_run_verify is exercised against deterministic shell commands
 * (true/false/printf) -- no model, no network. Rollback-to-green is proven in
 * test_snapshot via jc_snapshot_restore_commit. */

#include "jc_test.h"
#include "jc_envelope.h"
#include "jc_sysmsg.h"
#include "jc_json.h"
#include "jc_runsview.h"
#include "jc_str.h"
#include "jc_vec.h"

#include <string.h>

/* static, and forward-declared: it is a sub-test called from the envelope
 * aggregate in this file, not a suite entry point in test_main.c -- which is what
 * unit_orphans_lint asks of a non-static test function. */
static void test_test_edit_note(void);

static void test_parse_size(void)
{
    double v = 0.0;

    JC_CHECK(jc_env_parse_size("200k", &v) == 0 && v == 200000.0);
    JC_CHECK(jc_env_parse_size("1m", &v) == 0 && v == 1000000.0);
    JC_CHECK(jc_env_parse_size("512", &v) == 0 && v == 512.0);
    JC_CHECK(jc_env_parse_size("2K", &v) == 0 && v == 2000.0);
    JC_CHECK(jc_env_parse_size("1.5k", &v) == 0 && v == 1500.0);

    JC_CHECK(jc_env_parse_size("", &v) == -1);
    JC_CHECK(jc_env_parse_size("k", &v) == -1);
    JC_CHECK(jc_env_parse_size("12x", &v) == -1);
    JC_CHECK(jc_env_parse_size("-5", &v) == -1);
}

static void test_parse_duration(void)
{
    long s = 0;

    JC_CHECK(jc_env_parse_duration("20m", &s) == 0 && s == 1200);
    JC_CHECK(jc_env_parse_duration("2h", &s) == 0 && s == 7200);
    JC_CHECK(jc_env_parse_duration("90s", &s) == 0 && s == 90);
    JC_CHECK(jc_env_parse_duration("45", &s) == 0 && s == 45);
    JC_CHECK(jc_env_parse_duration("7d", &s) == 0 && s == 604800); /* M158 */
    JC_CHECK(jc_env_parse_duration("1D", &s) == 0 && s == 86400);

    JC_CHECK(jc_env_parse_duration("", &s) == -1);
    JC_CHECK(jc_env_parse_duration("10x", &s) == -1);
    JC_CHECK(jc_env_parse_duration("h", &s) == -1);
}

static void test_glob(void)
{
    /* '*' does not cross '/'. */
    JC_CHECK(jc_glob_match("src/*.c", "src/a.c") == 1);
    JC_CHECK(jc_glob_match("src/*.c", "src/x/a.c") == 0);
    JC_CHECK(jc_glob_match("*.c", "a.c") == 1);
    JC_CHECK(jc_glob_match("*.c", "a.h") == 0);

    /* '**' crosses '/' (including zero directories). */
    JC_CHECK(jc_glob_match("src/**", "src/x/a.c") == 1);
    JC_CHECK(jc_glob_match("src/**", "src/a.c") == 1);
    JC_CHECK(jc_glob_match("src/**/test.c", "src/a/b/test.c") == 1);
    JC_CHECK(jc_glob_match("src/**/test.c", "src/test.c") == 1);
    JC_CHECK(jc_glob_match("**", "anything/at/all") == 1);

    /* '?' matches one non-slash char. */
    JC_CHECK(jc_glob_match("a?c", "abc") == 1);
    JC_CHECK(jc_glob_match("a?c", "a/c") == 0);

    /* literals */
    JC_CHECK(jc_glob_match("Makefile", "Makefile") == 1);
    JC_CHECK(jc_glob_match("Makefile", "README") == 0);
}

static void test_scope(void)
{
    struct jc_envelope e;
    char *p1 = (char *)"src/**";
    char *p2 = (char *)"tests/**";

    memset(&e, 0, sizeof(e));
    jc_vec_init(&e.edit_scope, sizeof(char *));

    /* No patterns => everything is in scope. */
    JC_CHECK(jc_env_path_in_scope(&e, NULL, "anything.txt") == 1);

    jc_vec_push(&e.edit_scope, &p1);
    jc_vec_push(&e.edit_scope, &p2);
    /* Relative paths (root NULL) match the relative globs as before. */
    JC_CHECK(jc_env_path_in_scope(&e, NULL, "src/chat/jc_agent.c") == 1);
    JC_CHECK(jc_env_path_in_scope(&e, NULL, "tests/test_x.c") == 1);
    JC_CHECK(jc_env_path_in_scope(&e, NULL, "README.md") == 0);
    /* A leading "./" is ignored. */
    JC_CHECK(jc_env_path_in_scope(&e, NULL, "./src/main.c") == 1);
    JC_CHECK(jc_env_path_in_scope(&e, NULL, NULL) == 0);

    /* Regression (the zigodot dogfood bug): an ABSOLUTE path under the workspace
     * root must be relativized so it matches a workspace-relative editScope glob
     * -- the file tools pass absolute paths, which previously were refused. */
    JC_CHECK(jc_env_path_in_scope(&e, "/home/u/proj",
                                  "/home/u/proj/src/chat/jc_agent.c") == 1);
    JC_CHECK(jc_env_path_in_scope(&e, "/home/u/proj",
                                  "/home/u/proj/README.md") == 0);
    /* A trailing slash on root is tolerated. */
    JC_CHECK(jc_env_path_in_scope(&e, "/home/u/proj/",
                                  "/home/u/proj/tests/test_x.c") == 1);
    /* A path outside root isn't relativized; it won't match the relative globs. */
    JC_CHECK(jc_env_path_in_scope(&e, "/home/u/proj",
                                  "/etc/passwd") == 0);
    /* Fence-bypass regression: a SIBLING directory sharing the root's name as a
     * prefix must NOT be relativized. "/home/u/projsrc/x.c" begins with the
     * bytes of root "/home/u/proj" but is a different directory; without a
     * '/'-boundary check it was mis-stripped to "src/x.c", wrongly matching the
     * first (src) glob and admitting an out-of-workspace edit. */
    JC_CHECK(jc_env_path_in_scope(&e, "/home/u/proj",
                                  "/home/u/projsrc/x.c") == 0);
    JC_CHECK(jc_env_path_in_scope(&e, "/home/u/proj",
                                  "/home/u/project/tests/t.c") == 0);

    jc_vec_free(&e.edit_scope);
}

static void test_budget(void)
{
    struct jc_envelope e;

    memset(&e, 0, sizeof(e));
    e.start_time = 1000;

    /* All limits unset => never over budget. */
    JC_CHECK(jc_env_over_budget(&e, 99999) == JC_BUDGET_NONE);

    e.budget_tokens = 100.0;
    e.tokens_used = 50.0;
    JC_CHECK(jc_env_over_budget(&e, 1000) == JC_BUDGET_NONE);
    e.tokens_used = 100.0;
    JC_CHECK(jc_env_over_budget(&e, 1000) == JC_BUDGET_TOKENS);
    e.tokens_used = 0.0;

    e.deadline_secs = 60;
    JC_CHECK(jc_env_over_budget(&e, 1059) == JC_BUDGET_NONE);
    JC_CHECK(jc_env_over_budget(&e, 1060) == JC_BUDGET_DEADLINE);
    /* M162: pause --extend credits paused seconds back to the deadline. */
    e.deadline_credit = 30;
    JC_CHECK(jc_env_over_budget(&e, 1060) == JC_BUDGET_NONE);
    JC_CHECK(jc_env_over_budget(&e, 1089) == JC_BUDGET_NONE);
    JC_CHECK(jc_env_over_budget(&e, 1090) == JC_BUDGET_DEADLINE);
    e.deadline_credit = 0;
    e.deadline_secs = 0;

    e.max_tool_calls = 3;
    e.tool_calls = 2;
    JC_CHECK(jc_env_over_budget(&e, 1000) == JC_BUDGET_NONE);
    e.tool_calls = 3;
    JC_CHECK(jc_env_over_budget(&e, 1000) == JC_BUDGET_TOOLCALLS);

    /* M98: per-run read budget trips like the others. */
    e.max_tool_calls = 0; /* clear so it doesn't mask reads */
    e.max_reads = 5;
    e.reads = 4;
    JC_CHECK(jc_env_over_budget(&e, 1000) == JC_BUDGET_NONE);
    e.reads = 5;
    JC_CHECK(jc_env_over_budget(&e, 1000) == JC_BUDGET_READS);

    JC_CHECK_STR(jc_env_budget_name(JC_BUDGET_TOKENS), "tokens");
    JC_CHECK_STR(jc_env_budget_name(JC_BUDGET_READS), "reads");
    JC_CHECK_STR(jc_env_outcome_name(JC_ENV_VERIFY_FAILED), "verify_failed");
}

/* M80: budget exhaustion rolls back ONLY when a verifier is configured and red;
 * never merely for running out of budget (which previously discarded valid work
 * -- e.g. a design phase's output doc). Args: rollback_on_fail, has_green,
 * has_verifier, verify_code. */
static void test_budget_rollback_decision(void)
{
    /* rollback disabled or no green checkpoint => never roll back. */
    JC_CHECK(jc_env_budget_rollback_decision(0, 1, 1, 1) == 0);
    JC_CHECK(jc_env_budget_rollback_decision(1, 0, 1, 1) == 0);

    /* No verifier => keep the work even with rollback armed + a green checkpoint
     * (the design-phase / --verify-less case). */
    JC_CHECK(jc_env_budget_rollback_decision(1, 1, 0, 0) == 0);

    /* Verifier green => keep the work. */
    JC_CHECK(jc_env_budget_rollback_decision(1, 1, 1, 0) == 0);

    /* Verifier RED => roll back to green (don't leave broken code). */
    JC_CHECK(jc_env_budget_rollback_decision(1, 1, 1, 1) == 1);
    JC_CHECK(jc_env_budget_rollback_decision(1, 1, 1, 2) == 1);

    /* M207: `has_green` means OBSERVED green. The caller now passes
     * env->green_verified, so an unverified baseline arrives as has_green == 0
     * and the work is KEPT. A real drive against an already-red gate had 12 real
     * edits discarded here in favour of an equally red baseline -- rolling back
     * to a state not known to be better is never an improvement. */
    JC_CHECK(jc_env_budget_rollback_decision(1, 0, 1, 1) == 0);
    JC_CHECK(jc_env_budget_rollback_decision(1, 0, 1, 2) == 0);
    /* ...and an observed green with a red tree still rolls back, unchanged. */
    JC_CHECK(jc_env_budget_rollback_decision(1, 1, 1, 1) == 1);
}

/* M96: the "all reads, no synthesis" guard -- a no-edit budget stop truncated
 * its only deliverable (the final answer). */
static void test_analysis_starved(void)
{
    /* The firing case: budget exhausted, snapshots on, no edit made. */
    JC_CHECK(jc_env_analysis_starved(JC_ENV_BUDGET_EXHAUSTED, 1, 0) == 1);

    /* An edit WAS made => M80 kept partial work; not the starved case. */
    JC_CHECK(jc_env_analysis_starved(JC_ENV_BUDGET_EXHAUSTED, 1, 1) == 0);

    /* Snapshots off => can't trust "no edits" (green_commit is NULL anyway). */
    JC_CHECK(jc_env_analysis_starved(JC_ENV_BUDGET_EXHAUSTED, 0, 0) == 0);

    /* Not a budget stop => never fires (a clean completion is JC_ENV_OK). */
    JC_CHECK(jc_env_analysis_starved(JC_ENV_OK, 1, 0) == 0);
    JC_CHECK(jc_env_analysis_starved(JC_ENV_VERIFY_FAILED, 1, 0) == 0);
    JC_CHECK(jc_env_analysis_starved(JC_ENV_RUNNING, 1, 0) == 0);
}

/* M83: the out-of-scope filter flags changed paths not covered by the edit
 * scope -- the shell-introduced changes the write-tool fence can't catch. */
static void test_out_of_scope(void)
{
    struct jc_envelope e;
    struct jc_vec oos;
    char *g1 = (char *)"src/gdscript/**";
    const char *paths[4];
    paths[0] = "src/gdscript/vm.zig";   /* in scope   */
    paths[1] = ".jichi/memory.md";        /* OUT (the rm incident) */
    paths[2] = "src/other/x.zig";       /* OUT        */
    paths[3] = "docs/notes.md";         /* OUT        */

    memset(&e, 0, sizeof(e));
    jc_vec_init(&e.edit_scope, sizeof(char *));
    jc_vec_init(&oos, sizeof(char *));

    /* No edit scope => nothing is out of scope (no fence). */
    jc_env_out_of_scope_paths(&e, "/root", paths, 4, &oos);
    JC_CHECK(oos.len == 0);

    /* With a scope, the three non-matching paths are flagged, not vm.zig. */
    jc_vec_push(&e.edit_scope, &g1);
    jc_env_out_of_scope_paths(&e, "/root", paths, 4, &oos);
    JC_CHECK(oos.len == 3);
    {
        jc_size i;
        int saw_mem = 0, saw_vm = 0;
        for (i = 0; i < oos.len; i++) {
            const char *p = *(const char **)jc_vec_at(&oos, i);
            if (strcmp(p, ".jichi/memory.md") == 0) saw_mem = 1;
            if (strcmp(p, "src/gdscript/vm.zig") == 0) saw_vm = 1;
        }
        JC_CHECK(saw_mem == 1);   /* the out-of-scope deletion is flagged */
        JC_CHECK(saw_vm == 0);    /* the in-scope edit is not */
    }
    jc_vec_free(&oos);
    jc_vec_free(&e.edit_scope);
}

/* M81: periodic mid-turn verify is due every `verify_every` tool calls. */
static void test_should_verify_now(void)
{
    JC_CHECK(jc_env_should_verify_now(0, 100, 0) == 0);   /* off */
    JC_CHECK(jc_env_should_verify_now(-1, 100, 0) == 0);  /* off */
    JC_CHECK(jc_env_should_verify_now(10, 5, 0) == 0);    /* 5 < 10 */
    JC_CHECK(jc_env_should_verify_now(10, 10, 0) == 1);   /* exactly due */
    JC_CHECK(jc_env_should_verify_now(10, 13, 0) == 1);   /* past due */
    JC_CHECK(jc_env_should_verify_now(10, 25, 20) == 0);  /* 5 since last */
    JC_CHECK(jc_env_should_verify_now(10, 30, 20) == 1);  /* 10 since last */
}

/* M347: the budget notice -- fires at exactly four fifths of an armed cap,
 * in jc_env_over_budget's order, and the render names armed budgets only. */
static void test_budget_notice(void)
{
    struct jc_envelope e;
    struct jc_sb sb;

    memset(&e, 0, sizeof e);
    JC_CHECK(jc_env_budget_notice_due(NULL, 0) == JC_BUDGET_NONE);
    JC_CHECK(jc_env_budget_notice_due(&e, 100) == JC_BUDGET_NONE); /* unarmed */

    e.max_tool_calls = 5;
    e.tool_calls = 3;
    JC_CHECK(jc_env_budget_notice_due(&e, 0) == JC_BUDGET_NONE);   /* 3 < 4 */
    e.tool_calls = 4;
    JC_CHECK(jc_env_budget_notice_due(&e, 0) == JC_BUDGET_TOOLCALLS);

    /* Order mirrors over_budget: tokens are checked first when both cross. */
    e.budget_tokens = 1000.0;
    e.tokens_used = 799.0;
    JC_CHECK(jc_env_budget_notice_due(&e, 0) == JC_BUDGET_TOOLCALLS);
    e.tokens_used = 800.0;
    JC_CHECK(jc_env_budget_notice_due(&e, 0) == JC_BUDGET_TOKENS);

    /* Deadline honours the M162 pause credit. */
    memset(&e, 0, sizeof e);
    e.deadline_secs = 100;
    e.start_time = 1000;
    JC_CHECK(jc_env_budget_notice_due(&e, 1079) == JC_BUDGET_NONE);
    JC_CHECK(jc_env_budget_notice_due(&e, 1080) == JC_BUDGET_DEADLINE);
    e.deadline_credit = 20;
    JC_CHECK(jc_env_budget_notice_due(&e, 1080) == JC_BUDGET_NONE);
    JC_CHECK(jc_env_budget_notice_due(&e, 1100) == JC_BUDGET_DEADLINE);

    memset(&e, 0, sizeof e);
    e.max_reads = 10;
    e.reads = 7;
    JC_CHECK(jc_env_budget_notice_due(&e, 0) == JC_BUDGET_NONE);
    e.reads = 8;
    JC_CHECK(jc_env_budget_notice_due(&e, 0) == JC_BUDGET_READS);

    /* Render: an unarmed budget is never named; the line leads with the
     * [envelope] tag the transcript convention uses for injected notices. */
    memset(&e, 0, sizeof e);
    e.max_tool_calls = 5;
    e.tool_calls = 4;
    jc_sb_init(&sb);
    jc_env_budget_notice_render(&e, 0, &sb);
    JC_CHECK(sb.data != NULL);
    JC_CHECK(strstr(sb.data, "4 of 5 tool calls") != NULL);
    JC_CHECK(strstr(sb.data, "tokens") == NULL);
    JC_CHECK(strstr(sb.data, "reads") == NULL);
    JC_CHECK(strncmp(sb.data, "[envelope]", 10) == 0);
    JC_CHECK(strstr(sb.data, "final answer") != NULL);
    jc_sb_free(&sb);

    memset(&e, 0, sizeof e);
    e.budget_tokens = 500000.0;
    e.tokens_used = 410000.0;
    e.deadline_secs = 600;
    e.start_time = 0;
    jc_sb_init(&sb);
    jc_env_budget_notice_render(&e, 480, &sb);
    JC_CHECK(sb.data != NULL);
    JC_CHECK(strstr(sb.data, "410000 of 500000 budget tokens used (82%)")
             != NULL);
    JC_CHECK(strstr(sb.data, "480 of 600 deadline seconds") != NULL);
    JC_CHECK(strstr(sb.data, "tool calls") == NULL);
    jc_sb_free(&sb);
}

/* M351: the hollow-green note -- the model-facing half of M86. SANE renders
 * nothing; each verdict names its finding and asks for checkable conduct.
 * Deref checks combined with their guards (the M349 rule). */
static void test_sanity_note(void)
{
    struct jc_sb sb;

    jc_sb_init(&sb);
    jc_env_sanity_note(JC_VERIFY_SANE, 10, 5, &sb);
    JC_CHECK(sb.len == 0);
    jc_env_sanity_note(JC_VERIFY_NO_TESTS, 0, 7, &sb);
    JC_CHECK(sb.data != NULL && strncmp(sb.data, "[envelope]", 10) == 0);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "0 tests") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "unverified") != NULL);
    jc_sb_free(&sb);

    jc_sb_init(&sb);
    jc_env_sanity_note(JC_VERIFY_FEWER_TESTS, 2, 5, &sb);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "ran 2 tests where an earlier green ran 5")
                 != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "stopped running") != NULL);
    jc_sb_free(&sb);

    jc_sb_init(&sb);
    jc_env_sanity_note(JC_VERIFY_TESTS_NOT_WIRED, 5, 5, &sb);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "did not grow (5, was 5)") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "never runs") != NULL);
    jc_sb_free(&sb);
}

/* M343: the declared-kind parser and the exhaustive baseline truth table.
 * The row that pays for the feature is (GOAL, exit 0) -> FORCES_NOTHING; the
 * UNSET rows must equal the INVARIANT rows, because pre-M343 behaviour was
 * written for invariants and an undeclared gate must keep it byte-for-byte. */
static void test_verify_kind(void)
{
    int k = -1;

    JC_CHECK(jc_env_verify_kind_parse("invariant", &k) == 1);
    JC_CHECK(k == JC_VERIFY_KIND_INVARIANT);
    JC_CHECK(jc_env_verify_kind_parse("goal", &k) == 1);
    JC_CHECK(k == JC_VERIFY_KIND_GOAL);
    JC_CHECK(jc_env_verify_kind_parse("Goal", &k) == 0);      /* exact only */
    JC_CHECK(jc_env_verify_kind_parse("sometimes", &k) == 0);
    JC_CHECK(jc_env_verify_kind_parse("", &k) == 0);
    JC_CHECK(jc_env_verify_kind_parse(NULL, &k) == 0);
    JC_CHECK(jc_env_verify_kind_parse("goal", NULL) == 0);

    JC_CHECK(strcmp(jc_env_verify_kind_name(JC_VERIFY_KIND_INVARIANT),
                    "invariant") == 0);
    JC_CHECK(strcmp(jc_env_verify_kind_name(JC_VERIFY_KIND_GOAL),
                    "goal") == 0);
    JC_CHECK(strcmp(jc_env_verify_kind_name(JC_VERIFY_KIND_UNSET),
                    "unset") == 0);
    JC_CHECK(strcmp(jc_env_verify_kind_name(999), "unset") == 0);

    /* All six rows: 3 kinds x {green, red}. */
    JC_CHECK(jc_env_baseline_check(JC_VERIFY_KIND_UNSET, 0)
             == JC_BASELINE_OK);
    JC_CHECK(jc_env_baseline_check(JC_VERIFY_KIND_UNSET, 1)
             == JC_BASELINE_NOT_KNOWN_GOOD);
    JC_CHECK(jc_env_baseline_check(JC_VERIFY_KIND_INVARIANT, 0)
             == JC_BASELINE_OK);
    JC_CHECK(jc_env_baseline_check(JC_VERIFY_KIND_INVARIANT, 2)
             == JC_BASELINE_NOT_KNOWN_GOOD);
    JC_CHECK(jc_env_baseline_check(JC_VERIFY_KIND_GOAL, 0)
             == JC_BASELINE_FORCES_NOTHING);
    JC_CHECK(jc_env_baseline_check(JC_VERIFY_KIND_GOAL, 1)
             == JC_BASELINE_EXPECTED_RED);
}

/* Deterministic integration: run real shell commands, no model/network. */
static void test_run_verify(void)
{
    struct jc_sb out;

    JC_CHECK(jc_env_run_verify("true", NULL, NULL, NULL, 0) == 0);
    JC_CHECK(jc_env_run_verify("false", NULL, NULL, NULL, 0) != 0);

    jc_sb_init(&out);
    JC_CHECK(jc_env_run_verify("printf hi", NULL, &out, NULL, 0) == 0);
    JC_CHECK(out.data != NULL && strcmp(out.data, "hi") == 0);
    jc_sb_free(&out);

    /* A non-zero exit with captured stderr. */
    jc_sb_init(&out);
    JC_CHECK(jc_env_run_verify("echo oops 1>&2; exit 2", NULL, &out, NULL, 0)
             == 2);
    JC_CHECK(out.data != NULL && strstr(out.data, "oops") != NULL);
    jc_sb_free(&out);

    JC_CHECK(jc_env_run_verify(NULL, NULL, NULL, NULL, 0) == -1);
    JC_CHECK(jc_env_run_verify("", NULL, NULL, NULL, 0) == -1);

    /* A command that overruns the timeout is killed (sentinel -2). */
    JC_CHECK(jc_env_run_verify("sleep 5", NULL, NULL, NULL, 1)
             == JC_VERIFY_TIMEOUT);
}

/* M86: the green-gate hollowness verdict. */
static void test_refuse_green(void)
{
    /* The case it exists for: on, something changed out of scope, verify passed. */
    JC_CHECK(jc_env_refuse_green(1, 1, JC_ENV_OK) == 1);
    JC_CHECK(jc_env_refuse_green(1, 7, JC_ENV_OK) == 1);

    /* Off by default: the same run is untouched unless the operator asked. */
    JC_CHECK(jc_env_refuse_green(0, 1, JC_ENV_OK) == 0);

    /* Nothing changed out of scope -- an ordinary green stands. This is the
     * false-positive case that decides whether the feature is usable. */
    JC_CHECK(jc_env_refuse_green(1, 0, JC_ENV_OK) == 0);

    /* Only a GREEN can be refused. A run that already failed keeps its own
     * verdict: relabelling a verify failure as tainted would lose information. */
    JC_CHECK(jc_env_refuse_green(1, 3, JC_ENV_VERIFY_FAILED) == 0);
    JC_CHECK(jc_env_refuse_green(1, 3, JC_ENV_BUDGET_EXHAUSTED) == 0);
    JC_CHECK(jc_env_refuse_green(1, 3, JC_ENV_RUNNING) == 0);

    /* Idempotent: refusing an already-refused outcome is not a second event. */
    JC_CHECK(jc_env_refuse_green(1, 3, JC_ENV_SCOPE_TAINTED) == 0);
}

static void test_gate_contract(void)
{
    struct jc_sb sb;

    /* Off: appends nothing at all, so the cached prompt prefix is byte-identical
     * for every user who did not opt in. */
    jc_sb_init(&sb);
    jc_sysmsg_append_gate_contract(&sb, 0, "make test");
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* On, but no verifier: nothing to declare. */
    jc_sb_init(&sb);
    jc_sysmsg_append_gate_contract(&sb, 1, NULL);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
    jc_sb_init(&sb);
    jc_sysmsg_append_gate_contract(&sb, 1, "");
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* On with a verifier: says the gate is a contract, says what to do instead
     * of changing it, and warns that changing it is refused. */
    jc_sb_init(&sb);
    jc_sysmsg_append_gate_contract(&sb, 1, "make test");
    JC_CHECK(sb.len > 0);
    JC_CHECK(strstr(sb.data, "CONTRACT") != NULL);
    JC_CHECK(strstr(sb.data, "final answer") != NULL);
    JC_CHECK(strstr(sb.data, "refused") != NULL);
    jc_sb_free(&sb);
}

static void test_verify_consistency(void)
{
    /* The case the check exists for: tests ran, none failed, gate still red.
     * Definitionally a harness fault -- nothing under test reported a problem. */
    JC_CHECK(jc_env_verify_consistency(1, 397, 0, -1) == JC_VERIFY_HOLLOW_RED);
    JC_CHECK(jc_env_verify_consistency(2, 1, 0, 50) == JC_VERIFY_HOLLOW_RED);

    /* An ordinary red verify: some tests failed. Silence. */
    JC_CHECK(jc_env_verify_consistency(1, 390, 7, -1) == JC_VERIFY_AGREES);

    /* Red with NO counts is a compile error -- the commonest failing verify
     * there is (26 of 67 measured). Warning here would cry wolf, so it must
     * stay silent even though the verdict has no evidence behind it. */
    JC_CHECK(jc_env_verify_consistency(1, 0, 0, -1) == JC_VERIFY_AGREES);
    JC_CHECK(jc_env_verify_consistency(1, -1, -1, 91) == JC_VERIFY_AGREES);
    JC_CHECK(jc_env_verify_consistency(1, 0, 0, 407) == JC_VERIFY_AGREES);

    /* Green is M86's territory; answering here too would warn twice for one
     * verify. Even a shrinking count stays quiet on green. */
    JC_CHECK(jc_env_verify_consistency(0, 376, 0, 407) == JC_VERIFY_AGREES);
    JC_CHECK(jc_env_verify_consistency(0, 0, 0, 407) == JC_VERIFY_AGREES);

    /* Red, real failures, and fewer tests than an earlier green: the shape of a
     * run that deleted or un-wired tests while the build was broken -- which M86
     * cannot see, because M86 only looks at green verifies. */
    JC_CHECK(jc_env_verify_consistency(1, 300, 2, 407)
             == JC_VERIFY_RED_TESTS_GONE);

    /* Precedence when both apply: HOLLOW_RED wins, because "your harness is
     * broken" is the more actionable sentence to put in front of the model. */
    JC_CHECK(jc_env_verify_consistency(1, 300, 0, 407) == JC_VERIFY_HOLLOW_RED);

    /* No prior baseline cannot trigger the shrink case. */
    JC_CHECK(jc_env_verify_consistency(1, 5, 1, -1) == JC_VERIFY_AGREES);
    JC_CHECK(jc_env_verify_consistency(1, 5, 1, 0) == JC_VERIFY_AGREES);

    /* Same or more tests than before is not a shrink. */
    JC_CHECK(jc_env_verify_consistency(1, 407, 3, 407) == JC_VERIFY_AGREES);
    JC_CHECK(jc_env_verify_consistency(1, 500, 3, 407) == JC_VERIFY_AGREES);
}

static void test_verify_sanity(void)
{
    /* Green but zero tests observed -> the gate ran nothing. */
    JC_CHECK(jc_env_verify_sanity(0, -1, 0) == JC_VERIFY_NO_TESTS);
    JC_CHECK(jc_env_verify_sanity(0, 50, 0) == JC_VERIFY_NO_TESTS);

    /* Green with fewer tests than an earlier green -> the gate shrank. */
    JC_CHECK(jc_env_verify_sanity(29, 91, 0) == JC_VERIFY_FEWER_TESTS);

    /* Healthy: same, more, or first-ever count. */
    JC_CHECK(jc_env_verify_sanity(91, 91, 0) == JC_VERIFY_SANE);
    JC_CHECK(jc_env_verify_sanity(120, 91, 0) == JC_VERIFY_SANE);
    JC_CHECK(jc_env_verify_sanity(50, -1, 0) == JC_VERIFY_SANE);

    /* Unknown count (no test signal) is never flagged. */
    JC_CHECK(jc_env_verify_sanity(-1, -1, 0) == JC_VERIFY_SANE);
    JC_CHECK(jc_env_verify_sanity(-1, 91, 0) == JC_VERIFY_SANE);

    /* No prior baseline (prev_max <= 0) can't trigger a shrink. */
    JC_CHECK(jc_env_verify_sanity(5, 0, 0) == JC_VERIFY_SANE);

    /* M86 blind spot (measured 2026-08-07): a run ADDED a 13-test file, the suite
     * stayed at exactly 253 passing because nothing referenced it, and both the
     * gate and a count-based check called that green. Narrowed to "a test file was
     * written AND the count did not grow", which only fires when the run itself
     * claimed to add tests. */
    JC_CHECK(jc_env_verify_sanity(253, 253, 1) == JC_VERIFY_TESTS_NOT_WIRED);
    JC_CHECK(jc_env_verify_sanity(266, 253, 1) == JC_VERIFY_SANE);  /* grew: fine */
    JC_CHECK(jc_env_verify_sanity(253, 253, 0) == JC_VERIFY_SANE);  /* no test edit */
    JC_CHECK(jc_env_verify_sanity(253, -1, 1) == JC_VERIFY_SANE);   /* no prior count */
    /* a SHRINK still reports the more specific fewer_tests, not the new verdict */
    JC_CHECK(jc_env_verify_sanity(29, 91, 1) == JC_VERIFY_FEWER_TESTS);

    /* the shared test-path predicate */
    JC_CHECK(jc_env_is_test_path("src/agent/test.zig") == 1);
    JC_CHECK(jc_env_is_test_path("tests/test_learn.c") == 1);
    JC_CHECK(jc_env_is_test_path("foo_spec.rb") == 1);
    JC_CHECK(jc_env_is_test_path("src/gdscript/vm.zig") == 0);
    JC_CHECK(jc_env_is_test_path(NULL) == 0);
}

/* M89: failure-signature extraction + repeated-error (stuck) detection. */
static void test_fail_signature(void)
{
    char buf[JC_ENV_SIG_MAX];
    struct jc_envelope e;

    /* Extract the first `error:` line, dropping the file:line prefix. */
    JC_CHECK(jc_env_fail_signature(
                 "src/x.zig:12:5: error: no member named 'argC'\nmore\n",
                 buf, sizeof buf) == 1);
    JC_CHECK(strcmp(buf, "error: no member named 'argC'") == 0);

    /* No error line -> no signature. */
    JC_CHECK(jc_env_fail_signature("all good\n1..3\nok 1\n", buf,
                                   sizeof buf) == 0);
    JC_CHECK(buf[0] == '\0');
    JC_CHECK(jc_env_fail_signature(NULL, buf, sizeof buf) == 0);

    /* note_failure: same error twice in a row -> repeat >= 2; a different
     * error resets to 1; unparseable output resets to 0. */
    memset(&e, 0, sizeof e);
    JC_CHECK(jc_env_note_failure(&e, "a.zig:1:1: error: bad thing\n") == 1);
    JC_CHECK(jc_env_note_failure(&e, "a.zig:9:2: error: bad thing\n") == 2);
    JC_CHECK(jc_env_note_failure(&e, "a.zig:9:2: error: bad thing\n") == 3);
    JC_CHECK(jc_env_note_failure(&e, "b.zig:1:1: error: other thing\n") == 1);
    JC_CHECK(jc_env_note_failure(&e, "nothing wrong here\n") == 0);
    JC_CHECK(e.repeat_fails == 0 && e.last_fail_sig[0] == '\0');
    JC_CHECK(jc_env_note_failure(NULL, "error: x") == 0);
}

static void test_test_assertion_edit(void)
{
    /* Modifying an existing assertion's expected value in a test file -> 1. */
    JC_CHECK(jc_env_test_assertion_edit(
                 "tests/test_math.zig",
                 "try expect(add(2, 2) == 5);",
                 "try expect(add(2, 2) == 4);") == 1);
    JC_CHECK(jc_env_test_assertion_edit(
                 "src/foo_spec.js",
                 "expect(x).toBe(1)",
                 "expect(x).toBe(2)") == 1);

    /* Not a test file -> 0 even if it looks assertion-like. */
    JC_CHECK(jc_env_test_assertion_edit(
                 "src/math.zig",
                 "assert(a == b);",
                 "assert(a == c);") == 0);

    /* A pure addition (new contains old) -> 0: adding a case isn't moving one. */
    JC_CHECK(jc_env_test_assertion_edit(
                 "tests/test_math.zig",
                 "try expect(add(2, 2) == 4);",
                 "try expect(add(2, 2) == 4);\ntry expect(add(1, 1) == 2);") == 0);

    /* Neither side assertion-like (a comment / setup edit) -> 0. */
    JC_CHECK(jc_env_test_assertion_edit(
                 "tests/test_math.zig",
                 "const a = 2;",
                 "const a = 3;") == 0);

    /* NULLs -> 0. */
    JC_CHECK(jc_env_test_assertion_edit(NULL, "expect(1)", "expect(2)") == 0);
    JC_CHECK(jc_env_test_assertion_edit("tests/t.zig", NULL, "x") == 0);
    JC_CHECK(jc_env_test_assertion_edit("tests/t.zig", "x", NULL) == 0);
}

static void test_disposition(void)
{
    /* M92: the outcome x disposition label matrix. The bare enum name is
     * unchanged; the disposition label distinguishes kept vs reverted. */
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_OK, 0), "completed");
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_OK, 1), "completed");
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_BUDGET_EXHAUSTED, 0),
                 "budget_exhausted (work kept)");
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_BUDGET_EXHAUSTED, 1),
                 "budget_exhausted (rolled back)");
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_VERIFY_FAILED, 1),
                 "verify_failed (rolled back)");
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_VERIFY_FAILED, 0),
                 "verify_failed (kept)");
    JC_CHECK_STR(jc_env_disposition_name(JC_ENV_RUNNING, 0), "running");
    /* The bare outcome name is untouched (used by the journal + exit codes). */
    JC_CHECK_STR(jc_env_outcome_name(JC_ENV_BUDGET_EXHAUSTED),
                 "budget_exhausted");
}

static void test_summarize_paths(void)
{
    struct jc_sb sb;

    /* NULL / empty -> 0, out untouched. */
    JC_CHECK(jc_env_summarize_paths(NULL, 8, NULL) == 0);
    JC_CHECK(jc_env_summarize_paths("", 8, NULL) == 0);

    /* Count-only (NULL out): trailing newline + a blank line ignored. */
    JC_CHECK(jc_env_summarize_paths("a.zig\nb.zig\n", 8, NULL) == 2);
    JC_CHECK(jc_env_summarize_paths("only.zig", 8, NULL) == 1);

    /* All names fit -> ", "-joined, no "(+ more)". */
    jc_sb_init(&sb);
    JC_CHECK(jc_env_summarize_paths("a.zig\nb.zig\n", 8, &sb) == 2);
    JC_CHECK(strcmp(sb.data, "a.zig, b.zig") == 0);
    jc_sb_free(&sb);

    /* Truncation: 5 names, max 2 -> count 5, first two + "(+3 more)". */
    jc_sb_init(&sb);
    JC_CHECK(jc_env_summarize_paths("1\n2\n3\n4\n5\n", 2, &sb) == 5);
    JC_CHECK(strcmp(sb.data, "1, 2 (+3 more)") == 0);
    jc_sb_free(&sb);
}

/* M289: the out-of-scope guard runs at every top-level turn end and diffs
 * against the FIXED run-start baseline, so a file changed once stays changed. It
 * used to be re-reported every turn after: one run logged 17 `out_of_scope`
 * events that were all the same path, which reads as 17 violations in `runs` and
 * is the noise that pushed a real user to widen `editScope` instead of looking at
 * the one file. */
static void test_oos_reported_set(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_envelope e;

    JC_CHECK(jc_env_init(&e, a, "run1", NULL) == JC_OK);

    /* Nothing reported yet. */
    JC_CHECK(jc_env_oos_reported(&e, "tests/xor_test.gd") == 0);

    /* Marking makes it suppressed on later turns. */
    jc_env_oos_mark(&e, "tests/xor_test.gd");
    JC_CHECK(jc_env_oos_reported(&e, "tests/xor_test.gd") == 1);

    /* A different path is independent -- suppression is per file, not global. */
    JC_CHECK(jc_env_oos_reported(&e, "scripts/gen.sh") == 0);
    jc_env_oos_mark(&e, "scripts/gen.sh");
    JC_CHECK(jc_env_oos_reported(&e, "scripts/gen.sh") == 1);
    JC_CHECK(jc_env_oos_reported(&e, "tests/xor_test.gd") == 1);

    /* Marking twice is idempotent (no duplicate entry, no leak). */
    jc_env_oos_mark(&e, "tests/xor_test.gd");
    JC_CHECK(e.oos_reported.len == 2);

    /* NULL-safe both ways. */
    JC_CHECK(jc_env_oos_reported(NULL, "x") == 0);
    JC_CHECK(jc_env_oos_reported(&e, NULL) == 0);
    jc_env_oos_mark(NULL, "x");
    jc_env_oos_mark(&e, NULL);
    JC_CHECK(e.oos_reported.len == 2);

    jc_env_free(&e);
    jc_arena_free(a);
}

/* M290: the journal WRITER stamps the build on the run-level `start` record --
 * and only there, since one journal is one run. The red-before-green pass for
 * M290 initially showed 0 failures for removing this stamp, because the reader
 * test fed a hand-written journal that already contained the field: the writer
 * was uncovered. A round-trip through the real writer is what closes that. */
static void test_journal_version_stamp(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_envelope e;
    const char *path = jc_test_tmp("jichi_test_env_ver.jsonl");
    struct jc_run_summary rs;
    char *text = NULL;
    int start_stamped = 0;
    int other_stamped = 0;

    remove(path);
    JC_CHECK(jc_env_init(&e, a, "runV", path) == JC_OK);
    jc_env_journal_end(&e, jc_env_journal_begin(&e, "start"));
    jc_env_journal_end(&e, jc_env_journal_begin(&e, "tool_call"));
    jc_env_free(&e);   /* closes the journal */

    JC_CHECK(jc_read_file(path, &text, NULL, a) == JC_OK);
    if (text != NULL) {
        const char *line = text;
        while (line != NULL && *line != '\0') {
            const char *nl = strchr(line, '\n');
            char buf[2048];
            jc_size len = (nl != NULL) ? (jc_size)(nl - line)
                                       : (jc_size)strlen(line);
            if (len < sizeof(buf)) {
                cJSON *o;
                memcpy(buf, line, len);
                buf[len] = '\0';
                o = cJSON_Parse(buf);
                if (o != NULL) {
                    const char *ev = jc_json_get_str(o, "event", "");
                    const char *jv = jc_json_get_str(o, "jichi", NULL);
                    if (strcmp(ev, "start") == 0 && jv != NULL) {
                        start_stamped = 1;
                    } else if (strcmp(ev, "start") != 0 && jv != NULL) {
                        other_stamped = 1;
                    }
                    cJSON_Delete(o);
                }
            }
            line = (nl != NULL) ? nl + 1 : NULL;
        }
    }
    JC_CHECK(start_stamped == 1);   /* the run-level record carries it */
    JC_CHECK(other_stamped == 0);   /* and per-event repetition is not noise */

    /* And the reader picks it up off what the writer actually wrote. */
    if (text != NULL) {
        JC_CHECK(jc_runsview_parse(text, &rs) == 0);
        JC_CHECK(rs.jichi[0] != '\0');
    }

    remove(path);
    jc_arena_free(a);
}


/* M329: a model call metered AFTER the outcome was decided is always a bug -- the
 * `end` event is already written, so the run's reported totals are short by whatever
 * follows. This is the state half of the guard; the reader half is in
 * test_runsview.c, and the two are what make the condition visible instead of
 * something to be found by hand-comparing three sinks (M328). */
static void test_post_outcome_tokens(void)
{
    struct jc_envelope e;
    memset(&e, 0, sizeof(e));
    e.outcome = JC_ENV_RUNNING;

    /* While RUNNING, nothing is flagged and the tokens count normally. */
    jc_env_record_tokens(&e, 100.0, 20.0);
    JC_CHECK(e.tokens_used == 120.0);
    JC_CHECK(e.post_outcome_calls == 0);
    JC_CHECK(e.post_outcome_tokens == 0.0);
    JC_CHECK(e.post_outcome_warned == 0);

    /* Once the run has concluded, later spend is counted AND flagged. It still goes
     * into tokens_used -- the number should be true even though the journal has
     * already reported a smaller one. */
    e.outcome = JC_ENV_BUDGET_EXHAUSTED;
    jc_env_record_tokens(&e, 500.0, 25.0);
    JC_CHECK(e.tokens_used == 645.0);
    JC_CHECK(e.post_outcome_calls == 1);
    JC_CHECK(e.post_outcome_tokens == 525.0);
    JC_CHECK(e.post_outcome_warned == 1);

    /* Warned exactly once, but still counted: one WARN is actionable, one per call
     * is noise, and the tally is what says how much was missed. */
    jc_env_record_tokens(&e, 10.0, 5.0);
    JC_CHECK(e.post_outcome_calls == 2);
    JC_CHECK(e.post_outcome_tokens == 540.0);
    JC_CHECK(e.post_outcome_warned == 1);

    /* A verify failure is a concluded run too, not just a budget stop. */
    {
        struct jc_envelope f;
        memset(&f, 0, sizeof(f));
        f.outcome = JC_ENV_VERIFY_FAILED;
        jc_env_record_tokens(&f, 7.0, 3.0);
        JC_CHECK(f.post_outcome_calls == 1);
    }

    /* NULL is tolerated, as everywhere else in this module. */
    jc_env_record_tokens(NULL, 1.0, 1.0);
}

/* ---- M501: path provenance -------------------------------------------------
 *
 * The defect: `revertOutOfScope` reverted every out-of-scope change since the
 * baseline, because "changed since my baseline" and "changed by me" were one
 * predicate -- so a colleague merging files into the tree during a run would
 * have had that merge reverted. The write set gives the second predicate, and
 * these pin the part that can silently break it: the two sides MUST normalise a
 * path identically, or the intersection is empty and the revert quietly becomes
 * a no-op. */
static void test_relpath_and_write_set(void)
{
    struct jc_envelope e;
    struct jc_arena *a = jc_arena_new(4096);
    JC_CHECK(a != NULL);
    JC_CHECK(jc_env_init(&e, a, "m501", NULL) == JC_OK);

    /* The normaliser: absolute-under-root becomes root-relative, "./" goes,
     * and a sibling directory sharing the root's name is NOT mis-stripped
     * (the fence is security-relevant, so this boundary matters). */
    JC_CHECK_STR(jc_env_relpath("/w", "/w/src/a.c"), "src/a.c");
    JC_CHECK_STR(jc_env_relpath("/w/", "/w/src/a.c"), "src/a.c");
    JC_CHECK_STR(jc_env_relpath("/w", "./src/a.c"), "src/a.c");
    JC_CHECK_STR(jc_env_relpath("/w", "src/a.c"), "src/a.c");
    JC_CHECK_STR(jc_env_relpath("/w", "/wsrc/a.c"), "/wsrc/a.c");
    JC_CHECK_STR(jc_env_relpath(NULL, "src/a.c"), "src/a.c");
    JC_CHECK(jc_env_relpath("/w", NULL) == NULL);

    /* Nothing is "ours" before anything is written. */
    JC_CHECK(!jc_env_wrote(&e, "/w", "src/a.c"));

    /* THE LOAD-BEARING CASE: a tool records an ABSOLUTE path, the git diff
     * later offers the REPO-RELATIVE one. If these did not compare equal the
     * revert would skip the run's own writes -- the failure mode that turns a
     * safety fix into a disabled feature. */
    jc_env_wrote_mark(&e, "/w", "/w/src/a.c");
    JC_CHECK(jc_env_wrote(&e, "/w", "src/a.c"));
    JC_CHECK(jc_env_wrote(&e, "/w", "./src/a.c"));
    JC_CHECK(jc_env_wrote(&e, "/w", "/w/src/a.c"));
    /* And a path nobody wrote stays foreign. */
    JC_CHECK(!jc_env_wrote(&e, "/w", "docs/notes.md"));

    /* Marking twice does not duplicate, and NULL/empty are no-ops. */
    jc_env_wrote_mark(&e, "/w", "src/a.c");
    jc_env_wrote_mark(&e, "/w", NULL);
    jc_env_wrote_mark(&e, "/w", "");
    JC_CHECK(e.wrote.len == 1);
    JC_CHECK(!jc_env_wrote(NULL, "/w", "src/a.c"));
    JC_CHECK(!jc_env_wrote(&e, "/w", NULL));

    jc_env_free(&e);
    jc_arena_free(a);
}

void test_envelope(void)
{
    test_relpath_and_write_set();
    test_oos_reported_set();
    test_journal_version_stamp();
    test_summarize_paths();
    test_parse_size();
    test_parse_duration();
    test_glob();
    test_scope();
    test_budget();
    test_budget_notice();
    test_budget_rollback_decision();
    test_analysis_starved();
    test_should_verify_now();
    test_verify_kind();
    test_out_of_scope();
    test_verify_sanity();
    test_sanity_note();
    test_verify_consistency();
    test_refuse_green();
    test_gate_contract();
    test_fail_signature();
    test_test_assertion_edit();
    test_disposition();
    test_run_verify();
    test_post_outcome_tokens();
    test_test_edit_note();
}

void test_env_panel(void)
{
    struct jc_envelope e;
    struct jc_sb sb;

    /* --- the cadence -------------------------------------------------------
     * The whole point is that this is NOT a per-round nag: M347's DECISIONS row
     * rejected that on the M323 evidence (1,038 warnings from one unthrottled
     * condition), so every assertion here is about the panel STAYING QUIET. */
    memset(&e, 0, sizeof(e));
    e.budget_panel = 1;
    e.budget_tokens = 1000.0;

    /* Off unless armed -- the default posture. */
    e.budget_panel = 0;
    e.tool_calls = 50;
    JC_CHECK(jc_env_panel_due(&e, 5) == 0);
    e.budget_panel = 1;

    /* Nothing has happened yet: the flight plan just stated the caps, so a
     * reading of zeroes would be noise. */
    e.tool_calls = 0;
    JC_CHECK(jc_env_panel_due(&e, 5) == 0);

    /* Below the cadence, quiet. At it, due. */
    e.tool_calls = 4;
    JC_CHECK(jc_env_panel_due(&e, 5) == 0);
    e.tool_calls = 5;
    JC_CHECK(jc_env_panel_due(&e, 5) == 1);

    /* Rendered at this boundary already: not twice for one call. */
    e.panel_last_call = 5;
    JC_CHECK(jc_env_panel_due(&e, 5) == 0);

    /* A quintile crossing fires EARLY, between cadence points -- so the reading
     * can never skip a fifth of the budget however long the tool calls run. */
    e.tool_calls = 6;                /* only 1 call since the last panel */
    e.panel_tokens = 100.0;          /* was in the first fifth */
    e.tokens_used = 100.0;
    JC_CHECK(jc_env_panel_due(&e, 5) == 0);
    e.tokens_used = 250.0;           /* now in the second fifth */
    JC_CHECK(jc_env_panel_due(&e, 5) == 1);

    /* A nonsensical cadence disables rather than divides by it. */
    e.tool_calls = 100;
    JC_CHECK(jc_env_panel_due(&e, 0) == 0);
    JC_CHECK(jc_env_panel_due(NULL, 5) == 0);

    /* --- the reading -------------------------------------------------------- */
    memset(&e, 0, sizeof(e));
    e.budget_panel = 1;
    e.budget_tokens = 1000.0;
    e.tokens_used = 400.0;
    e.tool_calls = 8;
    e.max_tool_calls = 20;
    e.model_calls = 10;              /* => 40 tokens/call */
    e.start_time = 1000;
    e.deadline_secs = 600;

    jc_sb_init(&sb);
    jc_env_panel_render(&e, 1120, &sb);
    JC_CHECK(sb.data != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "400/1000 tokens (40%)") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "8/20 tool calls") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "120/600 seconds") != NULL);
    /* The rate and the PROJECTION -- the two numbers M347's notice cannot give,
     * and the reason this exists: 600 tokens left at 40/call is ~15 calls. */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "~40 tokens/call") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "~15 calls left") != NULL);
    jc_sb_free(&sb);

    /* ARMED BUDGETS ONLY (the M347/M355 rule: a limit that does not exist is not
     * a fact about this run). With only a token budget, no call/read/time terms. */
    memset(&e, 0, sizeof(e));
    e.budget_panel = 1;
    e.budget_tokens = 500.0;
    e.tokens_used = 100.0;
    e.model_calls = 2;
    jc_sb_init(&sb);
    jc_env_panel_render(&e, 0, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "tool calls") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "reads") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "seconds") == NULL);
    jc_sb_free(&sb);

    /* No model calls yet: the rate is OMITTED, never guessed or divided by zero. */
    memset(&e, 0, sizeof(e));
    e.budget_panel = 1;
    e.budget_tokens = 500.0;
    e.tokens_used = 0.0;
    e.model_calls = 0;
    jc_sb_init(&sb);
    jc_env_panel_render(&e, 0, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "tokens/call") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "calls left") == NULL);
    jc_sb_free(&sb);
}

static void test_test_edit_note(void)
{
    char b[600];

    /* --- the first one: recorded, and the legitimate case left open ---------
     * M88 already routed a test-assertion edit to the journal, telemetry, a WARN
     * and the verdict. The model, which is the party that just did it, was told
     * nowhere -- and in ANECDOTES #51 that warning fired TEN times while the run
     * carried on. This note lands at the moment of the act instead.
     *
     * It must NOT read as a refusal. Fixing a genuinely wrong test is fair work,
     * and a note that forbade it would teach the model to route around jichi (an
     * `edit_file` becomes a `sed` in run_terminal_command, which the M83 guard
     * catches only at turn end). So: recorded, does not count, say so if the test
     * was wrong. */
    jc_env_test_edit_note(1, "tests/test_thing.c", b, sizeof b);
    JC_CHECK(strstr(b, "TEST ASSERTION") != NULL);
    JC_CHECK(strstr(b, "tests/test_thing.c") != NULL);
    JC_CHECK(strstr(b, "recorded") != NULL);
    JC_CHECK(strstr(b, "does not count") != NULL);
    /* the escape hatch, and the price of using it: say what the right value is */
    JC_CHECK(strstr(b, "genuinely wrong") != NULL);
    JC_CHECK(strstr(b, "final answer") != NULL);
    /* NOT a prohibition -- checked as an absence, since the wording is what makes
     * the difference between a rule and a threat */
    JC_CHECK(strstr(b, "forbidden") == NULL);
    JC_CHECK(strstr(b, "not allowed") == NULL);

    /* --- the second and later: escalates, and names the count ---------------
     * Deliberately not once-per-turn. Each assertion edit is a distinct decision
     * rather than a repetition of one, so the M432 throttle does not apply; what
     * does apply is that the note must get SHORTER on the pattern and name the
     * consequence, which is the tainted verdict M410 already renders. */
    jc_env_test_edit_note(4, "tests/other.c", b, sizeof b);
    JC_CHECK(strstr(b, "4") != NULL);
    JC_CHECK(strstr(b, "tests/other.c") != NULL);
    JC_CHECK(strstr(b, "tainted") != NULL);
    /* and it still offers the honest exit rather than only a warning */
    JC_CHECK(strstr(b, "say that plainly") != NULL);

    /* An ordinal is not built by hand: "the 3th" is what %d + "th" produces, the
     * bug this project already made once in jc_toolloop_render. */
    jc_env_test_edit_note(3, "t.c", b, sizeof b);
    JC_CHECK(strstr(b, "3th") == NULL);
    JC_CHECK(strstr(b, "2th") == NULL);

    /* --- robustness: a NULL path, and a refusal to write past the cap -------
     * apply_patch passes the first test path it found across the patch, which is
     * NULL-able in principle; and the caller's buffer is a fixed 600 bytes on the
     * stack in two tools. */
    jc_env_test_edit_note(1, NULL, b, sizeof b);
    JC_CHECK(strstr(b, "a test file") != NULL);

    b[0] = 'x';
    b[1] = '\0';
    jc_env_test_edit_note(1, "t.c", b, 0);
    JC_CHECK(b[0] == 'x');   /* cap 0 must write nothing at all */
    jc_env_test_edit_note(1, "t.c", NULL, sizeof b);   /* must not crash */
}
