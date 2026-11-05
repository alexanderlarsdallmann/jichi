/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_constraint.c - the M110 constraint pure core. */

#include "jc_test.h"
#include "jc_constraint.h"
#include "jc_mem.h"
#include "jc_str.h"

#include <string.h>

static int has_kind_subj(const struct jc_constraint *cs, int n,
                         enum jc_constraint_kind k, const char *subj)
{
    int i;
    for (i = 0; i < n; i++) {
        if (cs[i].kind != k) continue;
        if (subj == NULL) return 1;
        if (cs[i].subject != NULL && strcmp(cs[i].subject, subj) == 0) return 1;
    }
    return 0;
}

static void test_scan(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* The user's real pain phrasing. */
    n = jc_constraint_scan("do not run the build, nor the tests",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "run_tests"));

    /* read-only phrasing */
    n = jc_constraint_scan("please keep this read-only", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* commit + push */
    n = jc_constraint_scan("never commit or push my work", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));

    /* must NOT false-positive: a question / a conversational "don't we". */
    n = jc_constraint_scan("should I run the build first?", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("why don't we run the tests now", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* a plain request to build is not a constraint */
    n = jc_constraint_scan("go ahead and build it", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    /* A DESCRIPTION of a broken build is not an instruction to avoid building.
     * Found by driving jichi on a real project (2026-09-21): the prompt opened
     * "Two tests ... do not compile:" -- a statement of the state the agent was
     * asked to fix -- and that inferred "do not run build commands", which then
     * REFUSED the agent's own `zig build test` for the rest of the session.
     * Measured: the same tool call ran with 0 refused without the sentence and
     * 1 refused with it. The agent could not verify its own work, looped, and
     * the run ended verify_failed after 42 tool calls and 2,215,762 tokens.
     * "the tests do not compile" is the most natural way there is to tell a
     * coding agent what is wrong, so this is not an exotic phrasing. */
    n = jc_constraint_scan("the tests do not compile", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("Two tests do not compile:", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("the build does not compile", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    /* ...and the IMPERATIVE forms must still be heard. A guard that silences
     * the line above by silencing these has not fixed anything. */
    n = jc_constraint_scan("do not compile anything", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 1);
    n = jc_constraint_scan("please do not run the build", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 1);
    /* two, and both are right: the TOOL `git_commit` and the shell command. */
    n = jc_constraint_scan("do not commit", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 2);
    n = jc_constraint_scan("make the change, but do not push", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 1);

    /* M1069 (card 77): the scanner must not infer a blanket rule from
     * (a) a bare "root" that is a scope noun, (b) a prohibition with a
     * NAMED object, or (c) a "never" clause whose first word is the
     * subject's own finite verb. Red-first: the three checks below fail
     * on the unfixed core. */
    n = jc_constraint_scan("Never a shared REPORT.md, and never a report "
                           "at the repository root.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("never commit a REPORT.md at the root.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "git_commit"));
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));
    n = jc_constraint_scan("The branches never run at runtime (only the "
                           "unit tests reach them).", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    jc_arena_free(a);
}

/* M1069-b (card 77, follow-up -b): G3 reworked to classify a "never" cue by
 * what PRECEDES it, per SV:1359. A real ORDER starts the clause, follows the
 * READER ("You must never ..."), or follows an opener/conjunction ("please
 * never", "and never"); a DESCRIPTION has a SUBJECT NOUN of a third party
 * ("The branches never run"). The four lost orders below sit four+ words out
 * from "never" (or the target is only implied) so the old "what follows never"
 * G3 skipped them -- RED on the unfixed core. The two subject descriptions
 * (SV:1359's still-wrong-in-both) must infer NOTHING. */
static void test_scan_m1069b(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- the four lost ORDERS: MUST STILL INFER (red-first) --- */
    n = jc_constraint_scan("Never run any command with sudo.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));

    n = jc_constraint_scan("Never run any of these commands as root.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));

    n = jc_constraint_scan("Never run anything that needs root.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));

    n = jc_constraint_scan("You must never run the full unit test suite here.",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "run_tests"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    /* --- the two DESCRIPTIONS (SV:1359, still wrong in both): MUST INFER
     * NOTHING --- */
    n = jc_constraint_scan("The job never uses sudo.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    n = jc_constraint_scan("The hooks never run at commit time.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    jc_arena_free(a);
}

/* M1069-c (card 77, follow-up -c): a DEONTIC modal (must/should/shall/may)
 * between a third-party subject and "never" is a NORM the agent is among,
 * not a description of that subject's habits. "Developers should never run
 * the deploy script by hand." binds the developers -- and an agent doing
 * developer work is among them -- so the window must be KEPT as an order and
 * scanned (inferring deploy). SV:1458 measured it inferring nothing.
 *
 * The guards below pin the over-keep edge: a reader subject with a plain
 * present ("we never run") is already an order (keep); a third-party subject
 * with a PLAIN PRESENT and NO deontic modal ("The CI never runs") is a
 * description of that subject's habits and must stay skipped. */
static void test_scan_m1069c(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- the deontic NORM: MUST INFER (red-first: M1069-b infers nothing) --- */
    n = jc_constraint_scan("Developers should never run the deploy script "
                           "by hand.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "deploy"));

    /* --- the reader + plain present: MUST STILL INFER (guard; already green) --- */
    n = jc_constraint_scan("We never run the tests on Fridays.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    /* --- the third-party + PLAIN PRESENT, no deontic modal: MUST INFER
     * NOTHING (a description of the CI's habits -- the over-keep guard) --- */
    n = jc_constraint_scan("The CI never runs the integration tests on "
                           "forks.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    jc_arena_free(a);
}

/* M1069-d (card 77, follow-up -d): two G3 refinements found by the review of
 * M1069-c.
 *
 * (1) LOST-ORDER. "it says never run the tests" inferred NOTHING. The word
 * before "never" is "says", which is not a MODAL, so the backward walk stopped
 * there and classified the VERB "says" as a third-party subject noun ->
 * description -> window skipped. A REPORTING VERB must be crossed like a modal:
 * the reporting SUBJECT is what classifies ("it" = third party = the window is
 * kept; it is not a reader word, so the reported order stays an order). This is
 * RED on the unfixed core. (Only THIS row exercises the crossing; the
 * "spec states" row below is a green guard kept by gate (1), not by it.)
 *
 * (2) UNPINNED MAY. "The CI may never run the integration tests" infers the
 * test ban, kept as an order by the deontic "may". By the drive's decision
 * must/should/shall/may are ALL deontic norms (the agent is bound by a norm
 * whose subject includes the work), so this verdict is INTENDED. It is GREEN
 * now; pinning it stops a future change to the walk from silently unpinning
 * the deontic-may behaviour.
 *
 * The "spec states" row is GREEN on the unfixed core -- "commit" is not a FIN
 * verb, so gate (1) keeps the window before the walk looks at "states" at
 * all. It is a green guard for gate (1), NOT the reported crossing (it
 * exercises none of the M1069-d reporting-verb mechanism; only the "it says"
 * row does). */
static void test_scan_m1069d(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- (1) a REPORTING VERB before "never" must be CROSSED (RED) --- */
    n = jc_constraint_scan("it says never run the tests.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "run_tests"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    /* F1: a green guard kept by gate (1) ("commit" is not a FIN verb), NOT
     * by the reported crossing -- it exercises none of the M1069-d mechanism */
    n = jc_constraint_scan("The spec states never commit to the remote.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));

    /* F3 (M1069-e): adjacency must-skip guard -- GREEN regression pin, not
     * red-first. The reported-verb keep is ADJACENT to "never" only: here
     * "says" is NOT adjacent (the subject "the job" stands between "says" and
     * "never"), so the walk does not cross it; "job" is reached as a
     * third-party subject and the window is skipped -- a reported
     * DESCRIPTION of the job's habits, the operator's measured 'still none'
     * case (SV:1737). */
    n = jc_constraint_scan("The log says the job never runs the tests.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);

    /* --- (2) the deontic "may" verdict is INTENDED (green; pinned) --- */
    n = jc_constraint_scan("The CI may never run the integration tests.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    jc_arena_free(a);
}

static void test_cmd_hits(void)
{
    JC_CHECK(jc_constraint_cmd_hits("build", "make -j4") == 1);
    JC_CHECK(jc_constraint_cmd_hits("build", "zig build") == 1);
    JC_CHECK(jc_constraint_cmd_hits("build", "cargo build --release") == 1);
    JC_CHECK(jc_constraint_cmd_hits("test", "cargo test") == 1);
    JC_CHECK(jc_constraint_cmd_hits("test", "zig build test") == 1);
    JC_CHECK(jc_constraint_cmd_hits("test", "pytest -q") == 1);
    /* word boundary: "latest" must not hit "test" */
    JC_CHECK(jc_constraint_cmd_hits("test", "git pull latest") == 0);
    JC_CHECK(jc_constraint_cmd_hits("build", "echo hello") == 0);
    JC_CHECK(jc_constraint_cmd_hits("push", "git push origin") == 1);
    JC_CHECK(jc_constraint_cmd_hits(NULL, "make") == 0);

    /* M155: the `privilege` key -- and its launcher-word aliases -- now bind
     * (before M155 `deny-cmd sudo` resolved to nothing and was inert). */
    JC_CHECK(jc_constraint_cmd_hits("privilege", "sudo apt-get update") == 1);
    JC_CHECK(jc_constraint_cmd_hits("sudo", "sudo apt-get update") == 1);
    JC_CHECK(jc_constraint_cmd_hits("sudo", "doas pkg_add x") == 1);
    JC_CHECK(jc_constraint_cmd_hits("pkexec", "pkexec whatever") == 1);
    JC_CHECK(jc_constraint_cmd_hits("sudo", "make install") == 0);
}

static void test_blocks(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;
    char reason[256];

    n = jc_constraint_scan("do not run the build, nor the tests",
                           cs, JC_CONSTRAINT_MAX, a);
    /* a build shell command is refused */
    JC_CHECK(jc_constraint_blocks(cs, n, "run_terminal_command",
                                  "make test", 0, reason, sizeof reason) == 1);
    JC_CHECK(strstr(reason, "constraint") != NULL);
    /* the dedicated test tool is refused regardless of args */
    JC_CHECK(jc_constraint_blocks(cs, n, "run_tests", NULL, 0,
                                  reason, sizeof reason) == 1);
    /* an innocent shell command is allowed */
    JC_CHECK(jc_constraint_blocks(cs, n, "run_terminal_command",
                                  "ls -la", 0, reason, sizeof reason) == 0);
    /* a read tool is allowed */
    JC_CHECK(jc_constraint_blocks(cs, n, "read_file", NULL, 1,
                                  reason, sizeof reason) == 0);

    /* M155: `deny-cmd sudo` (parsed subject "sudo") blocks a privileged
     * command, and natural-language "don't use sudo" scans to it too. */
    n = jc_constraint_scan("do not use sudo", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(jc_constraint_blocks(cs, n, "run_terminal_command",
                                  "sudo apt-get upgrade", 0,
                                  reason, sizeof reason) == 1);
    JC_CHECK(jc_constraint_blocks(cs, n, "run_terminal_command",
                                  "apt-get upgrade", 0,
                                  reason, sizeof reason) == 0);

    /* read-only blocks any mutating tool */
    n = jc_constraint_scan("read-only please", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(jc_constraint_blocks(cs, n, "write_file", NULL, 0,
                                  reason, sizeof reason) == 1);
    JC_CHECK(jc_constraint_blocks(cs, n, "read_file", NULL, 1,
                                  reason, sizeof reason) == 0);

    jc_arena_free(a);
}

static void test_parse_roundtrip(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    struct jc_constraint cs2[JC_CONSTRAINT_MAX];
    struct jc_sb sb;
    int n, n2;

    n = jc_constraint_scan("do not build or test; never push", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n > 0);

    /* M169: a scan produces INFERRED constraints, and those are session-scoped --
     * serializing them writes the header and nothing else. This is the whole
     * point of the change: a guess must not reach the durable store. */
    {
        int k;
        for (k = 0; k < n; k++) {
            JC_CHECK(cs[k].origin == JC_CONSTRAINT_INFERRED);
        }
        jc_sb_init(&sb);
        jc_constraint_serialize(cs, n, &sb);
        JC_CHECK(jc_constraint_parse(sb.data, cs2, JC_CONSTRAINT_MAX, a) == 0);
        jc_sb_free(&sb);
    }

    /* The serialize<->parse round-trip itself still has to be lossless, which is
     * what this test was originally for -- so promote the set to AUTHORED (what
     * an explicit `/constraints add` does) and check it survives. */
    {
        int k;
        for (k = 0; k < n; k++) {
            cs[k].origin = JC_CONSTRAINT_AUTHORED;
        }
    }
    jc_sb_init(&sb);
    jc_constraint_serialize(cs, n, &sb);
    n2 = jc_constraint_parse(sb.data, cs2, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n2 == n);
    /* the same set survives a round-trip */
    JC_CHECK(has_kind_subj(cs2, n2, JC_CONSTRAINT_DENY_CMD, "build"));
    JC_CHECK(has_kind_subj(cs2, n2, JC_CONSTRAINT_DENY_CMD, "push"));
    /* anything read back off disk is by definition authored */
    {
        int k;
        for (k = 0; k < n2; k++) {
            JC_CHECK(cs2[k].origin == JC_CONSTRAINT_AUTHORED);
        }
    }
    jc_sb_free(&sb);

    /* A mixed set writes only the authored half. It reads cs[0..2], so it needs
     * the scan above to have produced three -- JC_REQUIRE, not JC_CHECK: with an
     * empty scan the block serialised uninitialised structs and the suite died
     * of a segfault (found by M734's perturbation of the scanner; M729's shape). */
    if (JC_REQUIRE(n >= 3)) {
        struct jc_constraint mixed[3];
        int got;
        mixed[0] = cs[0];
        mixed[0].origin = JC_CONSTRAINT_AUTHORED;
        mixed[1] = cs[1];
        mixed[1].origin = JC_CONSTRAINT_INFERRED;
        mixed[2] = cs[2];
        mixed[2].origin = JC_CONSTRAINT_AUTHORED;
        jc_sb_init(&sb);
        jc_constraint_serialize(mixed, 3, &sb);
        got = jc_constraint_parse(sb.data, cs2, JC_CONSTRAINT_MAX, a);
        JC_CHECK(got == 2);
        jc_sb_free(&sb);
    }

    /* parse tolerates bullets, comments, blanks, and human directives */
    n2 = jc_constraint_parse(
        "# my rules\n- deny-cmd build\n\nread-only\nnote be gentle\n",
        cs2, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs2, n2, JC_CONSTRAINT_DENY_CMD, "build"));
    JC_CHECK(has_kind_subj(cs2, n2, JC_CONSTRAINT_READ_ONLY, NULL));
    JC_CHECK(has_kind_subj(cs2, n2, JC_CONSTRAINT_NOTE, NULL));

    jc_arena_free(a);
}

static void test_render(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    struct jc_sb sb;
    int n;

    /* M734: a scanned constraint is a guess, and is shown to the model as one. */
    n = jc_constraint_scan("do not run tests", cs, JC_CONSTRAINT_MAX, a);
    jc_sb_init(&sb);
    jc_constraint_render(cs, n, &sb);
    if (JC_REQUIRE(sb.data != NULL)) {
        JC_CHECK(strstr(sb.data, "ADVISORY") != NULL);
        JC_CHECK(strstr(sb.data, "REFUSED") == NULL);
    }
    jc_sb_free(&sb);

    /* The same rules written by the operator are limits, and say so. */
    {
        int k;
        for (k = 0; k < n; k++) cs[k].origin = JC_CONSTRAINT_AUTHORED;
    }
    jc_sb_init(&sb);
    jc_constraint_render(cs, n, &sb);
    if (JC_REQUIRE(sb.data != NULL)) {
        JC_CHECK(strstr(sb.data, "Active constraints") != NULL);
        JC_CHECK(strstr(sb.data, "REFUSED") != NULL);
        JC_CHECK(strstr(sb.data, "ADVISORY") == NULL);
    }
    jc_sb_free(&sb);

    /* Both at once: each under its own heading, the guess never among the
     * limits. */
    cs[0].text = (char *)"LIMIT_TEXT";
    cs[0].origin = JC_CONSTRAINT_AUTHORED;
    cs[1].text = (char *)"GUESS_TEXT";
    cs[1].origin = JC_CONSTRAINT_INFERRED;
    jc_sb_init(&sb);
    jc_constraint_render(cs, 2, &sb);
    {
        const char *lim = sb.data != NULL ? strstr(sb.data, "LIMIT_TEXT") : NULL;
        const char *adv = sb.data != NULL ? strstr(sb.data, "ADVISORY") : NULL;
        const char *gss = sb.data != NULL ? strstr(sb.data, "GUESS_TEXT") : NULL;
        JC_CHECK(lim != NULL && adv != NULL && gss != NULL &&
                 lim < adv && gss > adv);
    }
    jc_sb_free(&sb);

    /* empty renders nothing */
    jc_sb_init(&sb);
    jc_constraint_render(cs, 0, &sb);
    JC_CHECK(sb.data == NULL || sb.data[0] == '\0');
    jc_sb_free(&sb);

    jc_arena_free(a);
}

/* M167: the target word must be used as a VERB, not merely mentioned.
 *
 * The bare-noun test was over-broad: "do not change the test file" put the word
 * "test" in the negation window, so the scanner adopted `deny-tool run_tests` +
 * `deny-cmd test` and banned the whole suite -- a prohibition the user never
 * expressed. It was then persisted to .jichi/constraints.md and silently inherited
 * by every later run in that directory, which is how a live bench run lost a
 * task (docs/analysis/2026-07-27-local-gpu-bench.md, finding F2). */
static void test_verb_position(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- noun readings: the object is a FILE, so nothing is prohibited --- */
    n = jc_constraint_scan("Fix it. Do not change the test file.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "run_tests"));

    n = jc_constraint_scan("do not edit the build script", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));

    n = jc_constraint_scan("never touch the deploy config", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "deploy"));

    n = jc_constraint_scan("do not rename the commit hook", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));

    /* --- verb readings: still prohibited, exactly as before --- */
    n = jc_constraint_scan("do not test", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    n = jc_constraint_scan("do not run the test suite", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    n = jc_constraint_scan("do not ever build", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));

    n = jc_constraint_scan("avoid running the tests", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    /* coordinated verb lists: every item counts, not only the first */
    n = jc_constraint_scan("never commit or push my work", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));

    n = jc_constraint_scan("do not commit, push, or deploy", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "deploy"));

    /* The privilege group keeps the bare-mention rule: sudo/root are naturally
     * nouns and adjectives, so requiring a verb would silently weaken a
     * safety-relevant constraint. */
    n = jc_constraint_scan("never run as root", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));
    n = jc_constraint_scan("do not use sudo", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));

    jc_arena_free(a);
}

/* M168: "read-only" must be an INSTRUCTION, not a description.
 *
 * The first string below is the verbatim line from a real `--auto` brief. It
 * describes some oracle inputs; the scanner read it as an order and put the whole
 * run in read-only, so a 1.56M-token drive explored for twenty minutes and could
 * not perform its task. The outcome looked like a lazy model. */
static void test_read_only_instruction_vs_description(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- descriptive: must NOT adopt read-only --- */
    n = jc_constraint_scan(
        "Oracle files (read-only, outside the edit scope):\n"
        "  ../godot/tests/scripts/runtime/features/x.gd", cs,
        JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("the corpus is mounted read-only on that host", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* A cue in a PREVIOUS sentence does not reach across the boundary. */
    n = jc_constraint_scan("Keep going. The reference tree (read-only) is here.",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* --- instructions: must still adopt --- */
    n = jc_constraint_scan("please keep this read-only", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("stay read-only for this task", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("work in read-only mode", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("read-only please", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* the exact multi-sentence shape an operator types */
    n = jc_constraint_scan("Please stay read-only. Describe a.txt.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* The unambiguous imperatives need no cue -- they can only be orders. */
    n = jc_constraint_scan("do not edit anything", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("analysis only, no edits", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    jc_arena_free(a);
}

/* M207: an edit prohibition is adopted as blanket read-only only when its OBJECT
 * is broad. M168 fixed descriptive-vs-imperative and left scoped-vs-unscoped
 * open, asserting that "do not edit" can only be an order -- true, and beside
 * the point, because an order has an object and a named object scopes it.
 *
 * The first string below is the verbatim sentence from a real `--auto` brief
 * whose whole purpose was to edit one file. It cost a 1.5M-token drive: all 21
 * edit and shell calls refused below the verdict, 64 tool calls, zero progress,
 * and in headless mode no `/constraints clear` to lift it. */
static void test_edit_prohibition_scope(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- scoped: a NAMED object must NOT adopt blanket read-only --- */
    n = jc_constraint_scan(
        "a failure that indicates a real bug in the PRODUCTION pipeline. Do NOT "
        "edit the pipeline. STOP and describe the failure instead.", cs,
        JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("do not edit src/gdscript/parser.zig", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("don't edit the vendored headers", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* A broad object re-narrowed by a preposition is still scoped. */
    n = jc_constraint_scan("do not edit any files in third_party/", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("do not edit anything except the tests", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("do not make any changes to build.zig", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* --- unscoped: must still adopt --- */
    n = jc_constraint_scan("do not edit anything", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("do not edit files or make changes", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("do not edit any files", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("analysis only, no edits", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("just describe it; do not edit.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    n = jc_constraint_scan("do not make any change", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* One unscoped occurrence anywhere is enough, even beside a scoped one. */
    n = jc_constraint_scan("do not edit the pipeline. in fact, do not edit "
                           "anything at all.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    jc_arena_free(a);
}

/* M167: an inferred or inherited constraint must be NAMED where it is
 * announced -- reporting only a count is what made the misparse above a lasting
 * mystery rather than an obvious mistake. */
static void test_join_text(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    char buf[512];
    char tiny[12];
    int n;

    n = jc_constraint_scan("do not commit or push", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n >= 2);
    jc_constraint_join_text(cs, 0, n, buf, sizeof buf);
    JC_CHECK(strstr(buf, "do not commit") != NULL);
    JC_CHECK(strstr(buf, "do not push") != NULL);
    JC_CHECK(strstr(buf, ", ") != NULL);

    /* `from` skips already-announced entries, so an adopt notice lists only
     * what this turn added. */
    jc_constraint_join_text(cs, n, n, buf, sizeof buf);
    JC_CHECK(buf[0] == '\0');

    /* Never overruns, always NUL-terminated. */
    jc_constraint_join_text(cs, 0, n, tiny, sizeof tiny);
    JC_CHECK(strlen(tiny) < sizeof(tiny));

    /* Degenerate inputs are safe. */
    jc_constraint_join_text(NULL, 0, 3, buf, sizeof buf);
    JC_CHECK(buf[0] == '\0');
    jc_constraint_join_text(cs, -5, n, buf, sizeof buf);
    JC_CHECK(buf[0] != '\0');

    jc_arena_free(a);
}


/* M459: an explicit --edit-scope outranks an INFERRED read-only, and only an
 * inferred one. The composition defect this pins cost 250,288 tokens and 11
 * model calls on a run that was told exactly which file it could write. */
static void test_blocks_ex_scope_exempt(void)
{
    struct jc_constraint cs[2];
    char reason[256];

    cs[0].kind = JC_CONSTRAINT_READ_ONLY;
    cs[0].subject = NULL;
    cs[0].text = (char *)"read-only: do not edit files or make changes";
    cs[0].origin = JC_CONSTRAINT_INFERRED;

    /* Baseline: without the operator's declaration it still blocks, so the
     * exemption is what changes the answer and not some other edit. */
    JC_CHECK(jc_constraint_blocks(cs, 1, "write_file", NULL, 0,
                                  reason, sizeof reason) == 1);
    JC_CHECK(jc_constraint_blocks_ex(cs, 1, "write_file", NULL, 0, 0,
                                     reason, sizeof reason) == 1);
    /* With it: a guess yields to a typed flag. */
    JC_CHECK(jc_constraint_blocks_ex(cs, 1, "write_file", NULL, 0, 1,
                                     reason, sizeof reason) == 0);

    /* An AUTHORED read-only does NOT yield -- two explicit declarations in
     * conflict are the operator's to resolve, and silently picking one would
     * hide the conflict rather than settle it. */
    cs[0].origin = JC_CONSTRAINT_AUTHORED;
    JC_CHECK(jc_constraint_blocks_ex(cs, 1, "write_file", NULL, 0, 1,
                                     reason, sizeof reason) == 1);

    /* The exemption is scoped to read-only. A DENY_TOOL naming the very tool
     * still binds however the path is declared: the operator said not this
     * TOOL, which an edit-scope does not speak to. */
    cs[0].kind = JC_CONSTRAINT_DENY_TOOL;
    cs[0].subject = (char *)"write_file";
    cs[0].text = (char *)"do not write files";
    cs[0].origin = JC_CONSTRAINT_INFERRED;
    JC_CHECK(jc_constraint_blocks_ex(cs, 1, "write_file", NULL, 0, 1,
                                     reason, sizeof reason) == 1);

    /* ...and so does a DENY_CMD, which has no path to be in scope. */
    cs[0].kind = JC_CONSTRAINT_DENY_CMD;
    cs[0].subject = (char *)"build";
    cs[0].text = (char *)"do not run the build";
    JC_CHECK(jc_constraint_blocks_ex(cs, 1, "run_terminal_command", "make all",
                                     0, 1, reason, sizeof reason) == 1);

    /* A read-only tool was never blocked and still is not -- the exemption
     * must not be what makes reads work. */
    cs[0].kind = JC_CONSTRAINT_READ_ONLY;
    cs[0].subject = NULL;
    cs[0].text = (char *)"read-only";
    JC_CHECK(jc_constraint_blocks_ex(cs, 1, "read_file", NULL, 1, 0,
                                     reason, sizeof reason) == 0);

    /* An inferred read-only sitting BESIDE an authored one: the authored one
     * still blocks, so the exemption cannot be used to slip past a policy by
     * getting a guess adopted alongside it. */
    cs[0].origin = JC_CONSTRAINT_INFERRED;
    cs[1].kind = JC_CONSTRAINT_READ_ONLY;
    cs[1].subject = NULL;
    cs[1].text = (char *)"authored read-only";
    cs[1].origin = JC_CONSTRAINT_AUTHORED;
    JC_CHECK(jc_constraint_blocks_ex(cs, 2, "write_file", NULL, 0, 1,
                                     reason, sizeof reason) == 1);
}

/* M734 (plan D2): a negation reaches to the end of its sentence. The first four
 * are M730's measurement: its three misparses, bisected to the shortest span that
 * still fired, and the one prompt it found right. */
static void test_sentence_scope(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    n = jc_constraint_scan("Do not touch build.zig. Verify by running", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("never runs. Wire exactly three of the simplest ones "
                           "into build", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("never the body of a `pub fn` nothing calls. A "
                           "subsystem is proven when a test", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("Do not run build. Do not run tests!", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 3);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "run_tests"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    /* A dot inside a word is not the end of a sentence. (Not "make.sh": "make"
     * alone names the build key, so that example inferred it with the dot rule
     * removed, and proved nothing.) */
    n = jc_constraint_scan("Never touch build.zig or run the tests", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    /* A closing bracket or quote AFTER the full stop does not keep the sentence
     * open. (The first version of this check put the stop outside the quote --
     * `"notes.txt".` -- where the closer rule is never consulted, and it stayed
     * green with that rule removed.) */
    n = jc_constraint_scan("Never touch it (the file is done.) Run the tests.",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    n = jc_constraint_scan("Never say \"done.\" Run the tests.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    /* A blank line ends it, and so does the next list item... */
    n = jc_constraint_scan("Do not change the API\n\nrun the tests", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    n = jc_constraint_scan("Rules:\n- do not touch the docs\n- run the tests",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    /* ...but a single newline does not: a hard-wrapped sentence goes on. */
    n = jc_constraint_scan("Do not run the\ntests", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));

    jc_arena_free(a);
}

/* M743: the scanner measured over every distinct user message the session store
 * holds (626, three formats) and the two zigodot drives' task files (30). It
 * inferred 73 rules; 23 were wrong, and the drive's 16 read-only tasks inferred
 * none. Every sentence below is verbatim from that corpus unless it says
 * otherwise, and each check is the one its guard's removal turns red. */
static void test_scan_corpus_m743_read_only(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- recall: the orders that were not heard --- */
    /* The last sentence of 16 read-only drive tasks: "modify" was not a verb. */
    n = jc_constraint_scan("Do not modify any file.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* The object follows a preposition; the object read used to be "to". */
    n = jc_constraint_scan("You are performing a READ-ONLY code review. Make NO "
                           "edits to any file.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* Not in the corpus: the other two verbs, and the plural "make any change"
     * never matched because word_offset() wants a boundary after the phrase. */
    n = jc_constraint_scan("Do not change anything.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Do not touch any files.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Do not make any changes.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* A verb list before the one object, still heard as blanket. (The corpus
     * sentence opens "This is a STRICTLY READ-ONLY task.", which infers the rule
     * on its own -- so that half is left out, or this could not go red.) */
    n = jc_constraint_scan("Do not edit, create, or modify any file.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* ...and Markdown emphasis on the object is not a name (not in the corpus;
     * the named-object rule below would otherwise take "*" for one). */
    n = jc_constraint_scan("Do not edit *any* file.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    /* --- precision: the orders that were scoped, read as blanket --- */
    /* "other" narrows; it was skipped as a determiner. */
    n = jc_constraint_scan("Do not modify any other file. When finished, make "
                           "sure `zig build test` passes,", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* "else" and "but" narrow as "except" does. */
    n = jc_constraint_scan("your two files must compile and your new tests must "
                           "pass. Do not touch anything else.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Do not edit anything but parser.zig.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* A KIND of file is a scope: this brief writes a design doc. */
    n = jc_constraint_scan("DESIGN PHASE (analysis + plan only). Do NOT edit any "
                           "source; your only file\noutput is the design doc "
                           "named below.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Do not touch src/gdscript/codegen.zig.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* A backtick opens a NAMED object; it read as "nothing follows". */
    n = jc_constraint_scan("Hard constraints:\n- Do NOT modify `src/root.zig`. "
                           "The `test \"gate: ...\"` aggregator block there is "
                           "INTENTIONAL", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* The verb list reaches its object, and a named one scopes it (not in the
     * corpus: the comma used to read as "nothing follows" here too). */
    n = jc_constraint_scan("Do not edit, create, or modify the parser.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* A subject in front makes it a statement (not in the corpus). */
    n = jc_constraint_scan("These commands do not modify any files.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    /* An article makes "read-only" an adjective, whatever cue precedes it. */
    n = jc_constraint_scan("-- **Line 3 (memory.md):** Confirm the upstream Godot "
                           "path remains the read-only reference and is not "
                           "symlinked elsewhere.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    jc_arena_free(a);
}

static void test_scan_corpus_m743_targets(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* A cue glued to a hyphen is a compound adjective: five stored prompts. */
    n = jc_constraint_scan("- Wiring a GDScript feature that references core code "
                           "(a String method, an opcode) forces never-gated dead "
                           "code to COMPILE/RUN for the first time", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* M433's own example, the brief that built brief-check. */
    n = jc_constraint_scan("Goal: force never-compiled core code to compile.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* ...and one glued to a letter is inside a word (not in the corpus). */
    n = jc_constraint_scan("Nevertheless, run the build.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* "not just" asks for more; it forbids nothing. */
    n = jc_constraint_scan("CHECK YOUR WORK as you go with the runner (do not just "
                           "build blindly):", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    /* A spaced "--" ends the clause: this brief asks for the test to be run. */
    n = jc_constraint_scan("Read it with a path RELATIVE to this source file (do "
                           "NOT hardcode an external\n   absolute path -- the test "
                           "must be portable), run it through\n   "
                           "runConformanceTest, and assert it PASSES", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    /* So does an em dash, and a semicolon (neither in a misparse in the corpus;
     * the em dash is how the corpus's "the verify gate runs" briefs are set). */
    n = jc_constraint_scan("Do NOT hardcode the path \xe2\x80\x94 the test must be "
                           "portable, run it through the runner.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    n = jc_constraint_scan("Do not run the build; run the tests instead.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    /* The ban before the dash is kept. */
    n = jc_constraint_scan("Do NOT run zig build or the tests yourself \xe2\x80\x94 "
                           "the verify gate runs `zig build test` automatically",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "test"));
    /* "make" is the build tool only when it is run. The first sentence is a
     * person's, from a Continue-format session; the second is not in the corpus. */
    n = jc_constraint_scan("Include plantuml code, and diagrams, as well. Do not "
                           "make changes to prepare_sourcedata.py.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    n = jc_constraint_scan("Do not make any changes.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    n = jc_constraint_scan("Do not run make.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    n = jc_constraint_scan("Never invoke `make` directly.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    /* A keyword inside a file name is not the word: seven stored prompts. */
    n = jc_constraint_scan("- Modify ONLY those two files. Do NOT edit root.zig, "
                           "build.zig, or any other\n  file under any "
                           "circumstances.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));
    /* The privilege group still fires on the word itself. (Not a check of the
     * extension rule: a sentence's own full stop never reaches it, because the
     * window ends before the stop.) */
    n = jc_constraint_scan("Never run as root.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "privilege"));

    jc_arena_free(a);
}

/* M734 (plan D2 (c)): a rule the operator wrote refuses; a guessed one advises. */
static void test_judge(void)
{
    struct jc_constraint cs[2];
    char reason[256];
    int rule = 7;

    cs[0].kind = JC_CONSTRAINT_DENY_TOOL;
    cs[0].subject = (char *)"run_tests";
    cs[0].text = (char *)"do not run tests";
    cs[0].origin = JC_CONSTRAINT_INFERRED;
    JC_CHECK(jc_constraint_judge(cs, 1, "run_tests", NULL, 0, 0, reason,
                                 sizeof reason, &rule) == JC_CONSTRAINT_ADVISE);
    JC_CHECK(rule == 0);
    JC_CHECK(strcmp(reason, "do not run tests") == 0);
    /* The pure predicate did not change: the rule still FORBIDS the call. What
     * changed is what follows from a guess forbidding it. */
    JC_CHECK(jc_constraint_blocks(cs, 1, "run_tests", NULL, 0, reason,
                                  sizeof reason) == 1);

    /* The same rule, written by the operator, refuses as it always did. */
    cs[0].origin = JC_CONSTRAINT_AUTHORED;
    JC_CHECK(jc_constraint_judge(cs, 1, "run_tests", NULL, 0, 0, reason,
                                 sizeof reason, &rule) == JC_CONSTRAINT_REFUSE);
    JC_CHECK(strstr(reason, "blocked by an active constraint") != NULL);

    /* A call no rule speaks to is allowed, and names no rule. */
    JC_CHECK(jc_constraint_judge(cs, 1, "read_file", NULL, 1, 0, reason,
                                 sizeof reason, &rule) == JC_CONSTRAINT_ALLOW);
    JC_CHECK(rule == -1);

    /* A guess beside a policy cannot soften it, whichever comes first. */
    cs[0].origin = JC_CONSTRAINT_INFERRED;
    cs[1] = cs[0];
    cs[1].origin = JC_CONSTRAINT_AUTHORED;
    JC_CHECK(jc_constraint_judge(cs, 2, "run_tests", NULL, 0, 0, reason,
                                 sizeof reason, &rule) == JC_CONSTRAINT_REFUSE);
    JC_CHECK(rule == 1);

    /* M459 still holds, now for advice: an inferred read-only is not even
     * advised against on a path the operator's --edit-scope names. */
    cs[0].kind = JC_CONSTRAINT_READ_ONLY;
    cs[0].subject = NULL;
    cs[0].text = (char *)"read-only";
    cs[0].origin = JC_CONSTRAINT_INFERRED;
    JC_CHECK(jc_constraint_judge(cs, 1, "write_file", NULL, 0, 1, reason,
                                 sizeof reason, &rule) == JC_CONSTRAINT_ALLOW);
    JC_CHECK(jc_constraint_judge(cs, 1, "write_file", NULL, 0, 0, reason,
                                 sizeof reason, &rule) == JC_CONSTRAINT_ADVISE);
}

/* M872 (N45 (a)): a cue the request quotes or asks about is not an order.
 *
 * The demos drive's run 0091 (2026-10-02) carried its supervisor's question,
 * the first case below, and told a lead agent whose job is to commit merges, for
 * the whole session, not to commit. Each case is checked twice -- the quoted or
 * questioning form infers nothing, and the same words SAID infer the rule --
 * since a guard that silences the first by silencing the second fixes nothing. */
static void test_quoted_and_asked(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* the drive's sentence, as its prompt carried it */
    n = jc_constraint_scan("should a task text say \"write REPORT.md, do not "
                           "commit it\", or name a per-task file in /tmp?",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* the note's red-first case, and its control */
    n = jc_constraint_scan("Should a task say \"do not commit it\"?", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("Do not commit.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 2 && has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));
    /* quoted, no question: mentioned, not said */
    n = jc_constraint_scan("The note says \"do not commit it\".", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* a question, no quotes */
    n = jc_constraint_scan("Do not commit?", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* the other quoting forms: single, typographic double and single, inline
     * code, a fenced block -- and JSON-escaped double quotes */
    n = jc_constraint_scan("The note says 'do not commit it'.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("The note says \xe2\x80\x9c" "do not commit it"
                           "\xe2\x80\x9d.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("The note says \xe2\x80\x98" "do not commit it"
                           "\xe2\x80\x99.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("The hook prints `do not push` and exits.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* (with a blank line inside: without one the inline-code rule, a run of
     * three backticks closed by the next, would mask the block by itself) */
    n = jc_constraint_scan("Fix the hook:\n```\ngit fetch\n\n# never push from "
                           "here\n```\nThen report.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("say \\\"do not commit it\\\", or not", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* ...and a cue OUTSIDE the quotes in the same sentence is still said */
    n = jc_constraint_scan("The note says \"keep it short\", so do not push.",
                           cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 1 && has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));
    /* a target quoted after an unquoted cue still counts (M743) */
    n = jc_constraint_scan("never invoke \"make\"", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));

    /* An apostrophe is not a quote: neither opens a span that would swallow
     * the order after it. */
    n = jc_constraint_scan("It's the users' choice; don't push.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));
    n = jc_constraint_scan("Don\xe2\x80\x99t worry about style. Never push.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));
    /* An apostrophe after a letter opens nothing, though a closing-type mark
     * ("users' ") follows it: the span would swallow the order between. */
    n = jc_constraint_scan("It's fine, don't push the users' branch.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));
    /* ...and one inside a quoted phrase does not close it early, so the order
     * after it stays quoted. */
    n = jc_constraint_scan("The note says 'don't run, and never push'.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    n = jc_constraint_scan("The note says \xe2\x80\x98" "don\xe2\x80\x99t run, "
                           "and never push\xe2\x80\x99.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 0);
    /* ...nor is an inch mark, and a lone quote quotes nothing */
    n = jc_constraint_scan("The 3\" screen is fine. Do not push.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));
    n = jc_constraint_scan("He said \"wait.\n\nDo not push.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));

    /* A question elsewhere does not silence an order: the sentence decides. */
    n = jc_constraint_scan("Can you fix the parser? Do not push.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 1 && has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "push"));

    /* The read-only paths are asked the same two questions. */
    n = jc_constraint_scan("Should the brief say \"do not edit any file\"?", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("The brief says \"do not edit any file\".", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Do not edit any file.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Should I keep this read-only?", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("The task says 'keep this read-only'.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    n = jc_constraint_scan("Please keep this read-only.", cs,
                           JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));

    jc_arena_free(a);
}

/* M878 (the demos drive's N45, continued): a read-only phrase STATED of someone
 * else, or ordered FOR someone else, is not an order to the reader. A
 * supervisor's draft for the drive's lead said a child "runs in read-only mode";
 * M871's scan adopted a session-wide read-only rule from it, for a lead whose job
 * is to merge. Each rule the subject test applies has a statement it must not
 * adopt and an order it must still hear. */
static void test_said_of_other(void)
{
    static const char *const statements[] = {
        "The child runs in read-only mode.",
        "Children run in read-only mode, so they cannot commit.",
        "It answers tool disabled in read-only mode.",
        "Workers stay read-only.",
        "They work read-only.",
        "A write: false child stays read-only; you merge.",
        /* markdown: a heading, a blockquote, a table's cell, a list item */
        "## Notes\nThe child runs in read-only mode.",
        "> The child runs in read-only mode.",
        "| child | runs in read-only mode |",
        "Rules:\n- The child runs in read-only mode.",
        "Stay calm\n- The child runs in read-only mode.",
        "Stay focused. The child runs in read-only mode.",
        /* a verb that puts someone else in the mode */
        "Run the children in read-only mode.",
        "Keep the workers read-only.",
        "You must keep the children read-only.",
        "Let's keep the workers read-only.",
        "Your children run in read-only mode.",
        "All children run in read-only mode.",
        /* the clause "make sure" or "check that" takes */
        "Make sure the child runs in read-only mode.",
        "Check that the child stays read-only.",
        /* a role after the phrase; an aside */
        "Read-only children have no shell.",
        "All figures from this machine, read-only, 2026-08-27.",
        /* U+3053 ends in the byte 0x93, as an en dash does: no boundary */
        "The children \xe3\x81\x93 keep it read-only.",
        NULL
    };
    static const char *const orders[] = {
        "Work in read-only mode.",
        "Please stay read-only.",
        "You must stay read-only.",
        "This session is read-only.",
        "Everything stays read-only.",
        "Run everything in read-only mode.",
        "When the child runs, keep it read-only.",
        "Mode: read-only",
        "Read-only, please.",
        "Keep this read-only.",
        "Let's keep it read-only.",
        "- Stay read-only.",
        "1. Stay read-only.",
        /* boundaries that keep an order whole */
        "## Rules\nStay read-only.",
        "Some context first\n\nStay read-only.",
        "| Rule | Stay read-only |",
        "The child is slow -- stay read-only.",
        "The child is slow \xe2\x80\x94 stay read-only.",
        /* "make sure" taking the reader; the reader's own scope as the object */
        "Make sure you stay read-only.",
        "Remember to stay read-only.",
        "Ensure everything stays read-only.",
        "Keep all files read-only.",
        "Your work stays read-only.",
        "Review the code in read-only mode.",
        /* an aside: the clause it interrupts decides */
        "Stay quiet, focused, read-only.",
        "Work in plan mode (read-only).",
        NULL
    };
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int i;
    int n;

    for (i = 0; statements[i] != NULL; i++) {
        n = jc_constraint_scan(statements[i], cs, JC_CONSTRAINT_MAX, a);
        JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    }
    for (i = 0; orders[i] != NULL; i++) {
        n = jc_constraint_scan(orders[i], cs, JC_CONSTRAINT_MAX, a);
        JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_READ_ONLY, NULL));
    }
    jc_arena_free(a);
}

static const char *source_of(const struct jc_constraint *cs, int n,
                             enum jc_constraint_kind k, const char *subj)
{
    int i;
    for (i = 0; i < n; i++) {
        if (cs[i].kind != k) continue;
        if (subj == NULL ||
            (cs[i].subject != NULL && strcmp(cs[i].subject, subj) == 0)) {
            return cs[i].source;
        }
    }
    return NULL;
}

/* M872 (N45 (b)): each inferred rule carries the sentence it was read from, as
 * the request wrote it. The canonical text ("do not commit") is in no message,
 * so without the sentence a misparse could not be seen where it was announced. */
static void test_source_sentence(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    const char *s;
    int n;
    int k;

    /* the sentence, not its neighbours; case kept; the terminator kept */
    n = jc_constraint_scan("Fix the parser. Never install anything.\nThen "
                           "report back.", cs, JC_CONSTRAINT_MAX, a);
    s = source_of(cs, n, JC_CONSTRAINT_DENY_CMD, "install");
    JC_CHECK(s != NULL && strcmp(s, "Never install anything.") == 0);

    /* every rule one cue produced carries the same sentence */
    n = jc_constraint_scan("Build it. Do not commit.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 2);
    for (k = 0; k < n; k++) {
        JC_CHECK(cs[k].source != NULL &&
                 strcmp(cs[k].source, "Do not commit.") == 0);
    }

    /* a hard-wrapped sentence is one sentence, its line breaks one space; a
     * list item's marker is not part of it */
    n = jc_constraint_scan("Rules:\n- Please do not push\n  to the remote.\n"
                           "- Be brief.", cs, JC_CONSTRAINT_MAX, a);
    s = source_of(cs, n, JC_CONSTRAINT_DENY_CMD, "push");
    JC_CHECK(s != NULL && strcmp(s, "Please do not push to the remote.") == 0);

    /* the read-only rule names its sentence too */
    n = jc_constraint_scan("Read the logs. Do not edit any file. Report.", cs,
                           JC_CONSTRAINT_MAX, a);
    s = source_of(cs, n, JC_CONSTRAINT_READ_ONLY, NULL);
    JC_CHECK(s != NULL && strcmp(s, "Do not edit any file.") == 0);
    n = jc_constraint_scan("First, please keep this read-only. Then report.", cs,
                           JC_CONSTRAINT_MAX, a);
    s = source_of(cs, n, JC_CONSTRAINT_READ_ONLY, NULL);
    JC_CHECK(s != NULL &&
             strcmp(s, "First, please keep this read-only.") == 0);

    /* a sentence past the cap: a window that keeps the cue, "..." where cut,
     * never longer than the cap */
    {
        static const char longmsg[] =
            "This is a deliberately long sentence that goes on and on about the "
            "parser, the lexer, the evaluator, the runtime and every other part "
            "of the system, and somewhere in the middle of it all it says never "
            "push anything to the remote while the work is under review by the "
            "team, after which it keeps going for a good while longer about "
            "unrelated matters such as formatting, naming, and the weather.";
        n = jc_constraint_scan(longmsg, cs, JC_CONSTRAINT_MAX, a);
        s = source_of(cs, n, JC_CONSTRAINT_DENY_CMD, "push");
        JC_CHECK(s != NULL && strstr(s, "never push anything") != NULL);
        JC_CHECK(s != NULL && strlen(s) <= JC_CONSTRAINT_SOURCE_MAX);
        JC_CHECK(s != NULL && strncmp(s, "...", 3) == 0);
        JC_CHECK(s != NULL && strlen(s) >= 3 &&
                 strcmp(s + strlen(s) - 3, "...") == 0);
    }

    /* a parsed (authored) rule has none */
    n = jc_constraint_parse("deny-cmd push\n", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n == 1 && cs[0].source == NULL);

    /* the system prompt shows it beside an inferred rule */
    {
        struct jc_sb sb;
        n = jc_constraint_scan("Never install anything.", cs, JC_CONSTRAINT_MAX,
                               a);
        jc_sb_init(&sb);
        jc_constraint_render(cs, n, &sb);
        JC_CHECK(sb.data != NULL &&
                 strstr(sb.data, "do not install packages (read from: "
                                 "\"Never install anything.\")") != NULL);
        jc_sb_free(&sb);
    }

    /* join_sourced: one sentence per run of rules read from it */
    {
        char buf[512];
        n = jc_constraint_scan("Do not commit. Never install anything.", cs,
                               JC_CONSTRAINT_MAX, a);
        jc_constraint_join_sourced(cs, 0, n, buf, sizeof buf);
        JC_CHECK(strcmp(buf, "do not use the tool `git_commit`, do not commit "
                             "(from \"Do not commit.\"); do not install "
                             "packages (from \"Never install anything.\")") == 0);
        /* out of room: whole rules only, then the marker */
        jc_constraint_join_sourced(cs, 0, n, buf, 40);
        JC_CHECK(strlen(buf) < 40 && strstr(buf, ", ...") != NULL);
        JC_CHECK(strstr(buf, "(from \"Do not") == NULL ||
                 strstr(buf, "commit.\")") != NULL);
    }

    jc_arena_free(a);
}

/* L-19 (batch 0, with L-21): a named target must not widen to a class.
 *
 * The prompt's "Never run `make ci` here." was inferred as "do not run build
 * commands (make / cmake / compile / ...)", which then flagged every `make`
 * the gate needs. The named target is the rule: an inference must not be
 * broader than its source clause. After the fix, a rule inferred from that
 * sentence must not match `make test` (keep the named target or infer nothing).
 *
 * M968: the same fix must apply to unbackticked "make" when preceded by a run
 * verb and followed by a target word. */
static void test_named_target_not_widen(void)
{
    struct jc_arena *a = jc_arena_new(64 * 1024);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* --- L-19: backticked make + target word must NOT infer build --- */
    /* The measured prompt sentence. It names one target (`make ci`); the
     * scanner must not widen it to the whole class (build). L-19 says:
     * "an inferred rule must not be broader than the text it came from --
     * keep the named target (`make ci`) or infer nothing". */
    n = jc_constraint_scan("Never run `make ci` here.", cs, JC_CONSTRAINT_MAX, a);
    /* After the fix: no constraints are inferred for a named target. */
    JC_CHECK(n == 0);

    /* --- M968: unbackticked make + target word must NOT infer build --- */
    /* Case (a): "Never run make ci here." with unbackticked make. */
    n = jc_constraint_scan("Never run make ci here.", cs, JC_CONSTRAINT_MAX, a);
    /* After the M968 fix: no constraints are inferred because "make ci" is a
     * named target, not the build tool. */
    JC_CHECK(n == 0);

    /* --- Control: bare "make" must still infer build --- */
    /* Case (b): "Do not run make." (bare make, nothing after) should still
     * infer the build class. */
    n = jc_constraint_scan("Do not run make.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));

    /* --- L-19: backticked make alone must still infer build --- */
    /* Case (c): "Never run \`make\` directly." (backticked but no target) should
     * still infer the build class. */
    n = jc_constraint_scan("Never invoke `make` directly.", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "build"));

    jc_arena_free(a);
}

/* L-21 (batch 0): negation must not cross a colon boundary.
 *
 * The prompt's "Never end a run with a dirty tree: commit the work, or say
 * in the log why it was discarded." currently infers two advisory rules -
 * "do not use the tool git_commit" and "do not commit" - the opposite of
 * the instruction. The negation belongs to "end a run with a dirty tree";
 * the scan joined it to "commit" across the colon. After the fix: that
 * sentence infers no rule that matches git_commit or a commit. */
static void test_negation_no_cross_colon(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;

    /* The measured prompt sentence. The negation "Never" is in the first
     * clause, "commit" is after the colon. After the fix, the negation
     * must not cross the colon boundary. */
    n = jc_constraint_scan("Never end a run with a dirty tree: commit the work, "
                           "or say in the log why it was discarded.", cs,
                           JC_CONSTRAINT_MAX, a);
    /* After the fix: no constraint that matches git_commit or commit. */
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_TOOL, "git_commit"));
    JC_CHECK(!has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));

    /* A plain "do not commit" in the same clause as the negation must still work. */
    n = jc_constraint_scan("do not commit the work", cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(has_kind_subj(cs, n, JC_CONSTRAINT_DENY_CMD, "commit"));

    /* A colon that is NOT inside a clause boundary should still allow
     * the negation to reach if it's within the same clause. */
    /* This is a control: a semicolon or dash already ends a clause. */

    jc_arena_free(a);
}

/* M873 (the demos drive's N40): a rule jichi INFERRED is matched against what a
 * command RUNS. Every run of the drive inferred "do not install packages" from
 * "Never install anything", and was then told its grep for the word went against
 * it. Each case is checked both ways where it matters: a command that runs the
 * key, and one that only mentions it. A rule the operator wrote keeps the word
 * match (jc_constraint_cmd_hits), which errs toward refusing. */
static void test_cmd_runs(void)
{
    /* N40's red-first: a grep for the word runs grep (the drive's own command,
     * in the portable -E form posix_utils_lint asks of the tree) */
    JC_CHECK(jc_constraint_cmd_runs("install",
             "grep -n -E \"install|zigodot\" build.zig") == 0);
    JC_CHECK(jc_constraint_cmd_runs("install", "grep -n install build.zig") == 0);
    JC_CHECK(jc_constraint_cmd_runs("install", "pip install x") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install", "apt-get install y") == 1);
    /* ...while a rule the operator wrote still matches the word */
    JC_CHECK(jc_constraint_cmd_hits("install", "grep -n install build.zig") == 1);

    /* installing, through wrappers, paths, interpreters and shells */
    JC_CHECK(jc_constraint_cmd_runs("install",
             "sudo apt-get -y install build-essential") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install",
             "python3 -m pip install --user x") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install", "uv pip install x") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install",
             "cd build && make -C out install") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install", "./install.sh --prefix /opt") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install", "bash -c 'npm install'") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install",
             "install -m 0755 jichi /usr/local/bin") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install",
             "env CC=gcc /usr/bin/make install") == 1);
    JC_CHECK(jc_constraint_cmd_runs("install", "timeout 60 pip install x") == 1);
    /* ...and mentioning it */
    JC_CHECK(jc_constraint_cmd_runs("install", "cat INSTALL.md docs/install.md") == 0);
    JC_CHECK(jc_constraint_cmd_runs("install", "echo 'run make install later'") == 0);
    JC_CHECK(jc_constraint_cmd_runs("install", "git log --grep=install") == 0);
    JC_CHECK(jc_constraint_cmd_runs("install", "ls ${HOME}/install") == 0);
    JC_CHECK(jc_constraint_cmd_runs("install", "find . -name install -exec ls {} +")
             == 0);

    /* build */
    JC_CHECK(jc_constraint_cmd_runs("build", "zig build") == 1);
    JC_CHECK(jc_constraint_cmd_runs("build", "npm run build") == 1);
    JC_CHECK(jc_constraint_cmd_runs("build", "make -j8") == 1);
    JC_CHECK(jc_constraint_cmd_runs("build", "{ make; }") == 1);
    JC_CHECK(jc_constraint_cmd_runs("build", "cat build.zig") == 0);
    JC_CHECK(jc_constraint_cmd_runs("build", "du -sh build") == 0);
    JC_CHECK(jc_constraint_cmd_hits("build", "cat build.zig") == 1);

    /* test: the shell's own `test` and `[` are conditionals, not test runs */
    JC_CHECK(jc_constraint_cmd_runs("test", "zig build test --summary all") == 1);
    JC_CHECK(jc_constraint_cmd_runs("test", "cargo test") == 1);
    JC_CHECK(jc_constraint_cmd_runs("test", "python3 -m pytest -q") == 1);
    /* an interpreter's -c argument is code, never a command, quoted or not */
    JC_CHECK(jc_constraint_cmd_runs("build", "python3 -c make") == 0);
    JC_CHECK(jc_constraint_cmd_runs("test", "./test.sh") == 1);
    JC_CHECK(jc_constraint_cmd_runs("test", "test -f build.zig && echo yes") == 0);
    JC_CHECK(jc_constraint_cmd_runs("test", "[ -d tests ] || mkdir tests") == 0);
    JC_CHECK(jc_constraint_cmd_runs("test", "cat tests/x_test.zig") == 0);
    JC_CHECK(jc_constraint_cmd_runs("test", "git commit -m \"add a test\"") == 0);

    /* commit and push: the action, after the options and their values */
    JC_CHECK(jc_constraint_cmd_runs("commit", "git -C /tmp/wt commit -am x") == 1);
    JC_CHECK(jc_constraint_cmd_runs("commit", "git log --grep=commit") == 0);
    JC_CHECK(jc_constraint_cmd_runs("push", "git push origin master") == 1);
    JC_CHECK(jc_constraint_cmd_runs("push", "git stash push -m wip") == 0);

    /* deploy */
    JC_CHECK(jc_constraint_cmd_runs("deploy", "npm run deploy") == 1);
    JC_CHECK(jc_constraint_cmd_runs("deploy", "cat deploy.md") == 0);

    /* privilege: a launcher that begins a segment, as M152 reads it */
    JC_CHECK(jc_constraint_cmd_runs("privilege", "sudo apt-get update") == 1);
    JC_CHECK(jc_constraint_cmd_runs("privilege", "grep sudo /etc/group") == 0);

    /* degenerate input */
    JC_CHECK(jc_constraint_cmd_runs(NULL, "make") == 0);
    JC_CHECK(jc_constraint_cmd_runs("build", NULL) == 0);
    JC_CHECK(jc_constraint_cmd_runs("build", "") == 0);
    JC_CHECK(jc_constraint_cmd_runs("build", "\"make") == 0);
    JC_CHECK(jc_constraint_cmd_runs("no_such_key", "make") == 0);
}

/* ...and what follows at the gate: an inferred rule advises only on a run, an
 * authored one refuses on the word, as before. */
static void test_judge_runs(void)
{
    struct jc_constraint cs[1];
    char reason[256];
    int rule = 7;

    memset(cs, 0, sizeof cs);
    cs[0].kind = JC_CONSTRAINT_DENY_CMD;
    cs[0].subject = (char *)"install";
    cs[0].text = (char *)"do not install packages";
    cs[0].origin = JC_CONSTRAINT_INFERRED;
    JC_CHECK(jc_constraint_judge(cs, 1, "run_terminal_command",
             "grep -n install build.zig", 0, 0, reason, sizeof reason, &rule)
             == JC_CONSTRAINT_ALLOW);
    JC_CHECK(jc_constraint_judge(cs, 1, "run_terminal_command",
             "pip install x", 0, 0, reason, sizeof reason, &rule)
             == JC_CONSTRAINT_ADVISE);
    cs[0].origin = JC_CONSTRAINT_AUTHORED;
    JC_CHECK(jc_constraint_judge(cs, 1, "run_terminal_command",
             "grep -n install build.zig", 0, 0, reason, sizeof reason, &rule)
             == JC_CONSTRAINT_REFUSE);
}

void test_constraint(void)
{
    test_cmd_runs();
    test_judge_runs();
    test_quoted_and_asked();
    test_said_of_other();
    test_source_sentence();
    test_scan_corpus_m743_read_only();
    test_scan_corpus_m743_targets();
    test_sentence_scope();
    test_judge();
    test_blocks_ex_scope_exempt();
    test_scan();
    test_scan_m1069b();
    test_scan_m1069c();
    test_scan_m1069d();
    test_verb_position();
    test_read_only_instruction_vs_description();
    test_edit_prohibition_scope();
    test_join_text();
    test_cmd_hits();
    test_blocks();
    test_parse_roundtrip();
    test_render();
    test_named_target_not_widen();
    test_negation_no_cross_colon();
}


void test_constraint_source_line(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;
    int i;
    int found_build = 0;
    int found_push = 0;

    /* The measured brief. Line 7 was a description of the GOAL that inferred a
     * ban on the very thing the task requires -- "force never-compiled core code
     * to compile", the failure that cost one driven run its whole deliverable --
     * and naming the LINE is most of this feature's value: the canonical
     * constraint text ("do not run build commands") appears nowhere in the brief,
     * so a list alone leaves the author hunting. M743 stopped that misparse (a
     * cue glued to a hyphen is a compound adjective; test_scan_corpus_m743), so
     * line 7 now carries a build ban the brief means, and the property under
     * test is unchanged: the lines ABOVE it say "never compiled" too, and the
     * attribution must still skip them. */
    const char *brief =
        "# Task: repair the animation evaluator\n"          /* 1 */
        "\n"                                                /* 2 */
        "Background: 47 of 88 files were never compiled.\n" /* 3 */
        "The body has never been compiled, so its\n"        /* 4 */
        "HashMap field predates the 4-arg API.\n"           /* 5 */
        "\n"                                                /* 6 */
        "Goal: fix it, but never compile the core code.\n"  /* 7 */
        "Do not push anything.\n";                          /* 8 */

    n = jc_constraint_scan(brief, cs, JC_CONSTRAINT_MAX, a);
    JC_CHECK(n >= 2);
    for (i = 0; i < n; i++) {
        int ln = jc_constraint_source_line(brief, &cs[i], a);
        if (cs[i].subject != NULL && strcmp(cs[i].subject, "build") == 0) {
            found_build = 1;
            JC_CHECK(ln == 7);   /* the goal statement, not the background lines */
        }
        if (cs[i].subject != NULL && strcmp(cs[i].subject, "push") == 0) {
            found_push = 1;
            JC_CHECK(ln == 8);
        }
    }
    JC_CHECK(found_build == 1);
    JC_CHECK(found_push == 1);

    /* A constraint no single line reproduces returns 0 rather than guessing a line.
     * Claiming a wrong line would be worse than admitting none: the author would
     * reword text that was never responsible. */
    {
        struct jc_constraint fake;
        memset(&fake, 0, sizeof(fake));
        fake.kind = JC_CONSTRAINT_DENY_TOOL;
        fake.subject = "no_such_tool_anywhere";
        JC_CHECK(jc_constraint_source_line(brief, &fake, a) == 0);
    }

    /* Degenerate input must not crash. */
    JC_CHECK(jc_constraint_source_line(NULL, &cs[0], a) == 0);
    JC_CHECK(jc_constraint_source_line(brief, NULL, a) == 0);
    JC_CHECK(jc_constraint_source_line(brief, &cs[0], NULL) == 0);
    /* A one-line brief with no trailing newline still attributes to line 1. */
    {
        struct jc_constraint one[JC_CONSTRAINT_MAX];
        int m = jc_constraint_scan("do not push", one, JC_CONSTRAINT_MAX, a);
        if (m > 0) {
            JC_CHECK(jc_constraint_source_line("do not push", &one[0], a) == 1);
        }
    }

    jc_arena_free(a);
}
