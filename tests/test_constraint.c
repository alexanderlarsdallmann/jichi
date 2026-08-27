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

    /* A mixed set writes only the authored half. */
    {
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

    n = jc_constraint_scan("do not run tests", cs, JC_CONSTRAINT_MAX, a);
    jc_sb_init(&sb);
    jc_constraint_render(cs, n, &sb);
    JC_CHECK(strstr(sb.data, "Active constraints") != NULL);
    JC_CHECK(strstr(sb.data, "REFUSED") != NULL);
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

void test_constraint(void)
{
    test_blocks_ex_scope_exempt();
    test_scan();
    test_verb_position();
    test_read_only_instruction_vs_description();
    test_edit_prohibition_scope();
    test_join_text();
    test_cmd_hits();
    test_blocks();
    test_parse_roundtrip();
    test_render();
}


void test_constraint_source_line(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_constraint cs[JC_CONSTRAINT_MAX];
    int n;
    int i;
    int found_build = 0;
    int found_push = 0;

    /* The measured brief. Line 7 is a description of the GOAL that infers a ban on
     * the very thing the task requires -- the failure that cost one driven run its
     * whole deliverable, and the reason naming the LINE is most of this feature's
     * value: the canonical constraint text ("do not run build commands") appears
     * nowhere in the brief, so a list alone leaves the author hunting. */
    const char *brief =
        "# Task: repair the animation evaluator\n"          /* 1 */
        "\n"                                                /* 2 */
        "Background: 47 of 88 files were never compiled.\n" /* 3 */
        "The body has never been compiled, so its\n"        /* 4 */
        "HashMap field predates the 4-arg API.\n"           /* 5 */
        "\n"                                                /* 6 */
        "Goal: force never-compiled core code to compile.\n" /* 7 */
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
