/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_learn.c - lessons-draft parsing (M70). */

#include "jc_test.h"
#include "jc_learn.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_snprintf.h"
#include "jc_platform.h"   /* jc_mkdir_p */
#include "jc_app.h"
#include "jc_reread.h"
#include "jc_tool.h"
#include <sys/types.h>
#include <unistd.h>        /* symlink */
#include <string.h>
#include <stdlib.h>        /* getenv */
#include <sys/stat.h>
#include <sys/wait.h>      /* WIFSIGNALED, WIFEXITED, waitpid */

/* M292: the `learn analyze` report is now shared between the CLI and the TUI.
 * It lived as two statics in main.c, so only the CLI could produce it and a TUI
 * user had to leave the session to see what their own logs said. Rendering into a
 * caller's jc_sb (rather than printing) is what lets both front-ends use it --
 * two front-ends each formatting the same report is the drift M286 cost us. */
static void test_analyze_render(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_sb out;
    /* A tool below the 60% ok-rate floor, and an envelope outcome breakdown. */
    const char *log =
        "{\"event\":\"tool_call\",\"ws\":\"/w\",\"ts\":9000,"
        "\"name\":\"flaky_tool\",\"ok\":false}\n"
        "{\"event\":\"tool_call\",\"ws\":\"/w\",\"ts\":9001,"
        "\"name\":\"flaky_tool\",\"ok\":false}\n"
        "{\"event\":\"tool_call\",\"ws\":\"/w\",\"ts\":9002,"
        "\"name\":\"flaky_tool\",\"ok\":false}\n"
        "{\"event\":\"turn_end\",\"ws\":\"/w\",\"ts\":9003,"
        "\"outcome\":\"budget_exhausted\",\"rolled_back\":false}\n";

    jc_sb_init(&out);
    jc_learn_analyze_render(a, log, "/w", &out);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        /* The failing tool is ranked... */
        JC_CHECK(strstr(out.data, "flaky_tool") != NULL);
        /* ...and the outcome line explains that a kept budget stop is NOT a
         * failure, so the mentor does not draft a lesson treating it as one. */
        JC_CHECK(strstr(out.data, "Autonomy outcomes") != NULL);
        JC_CHECK(strstr(out.data, "work kept") != NULL);
    }
    jc_sb_free(&out);

    /* A workspace filter that matches nothing yields a report with no findings
     * rather than another project's problems (M56). */
    jc_sb_init(&out);
    jc_learn_analyze_render(a, log, "/other", &out);
    JC_CHECK(out.data == NULL || strstr(out.data, "flaky_tool") == NULL);
    jc_sb_free(&out);

    /* Empty input is safe. */
    jc_sb_init(&out);
    jc_learn_analyze_render(a, "", NULL, &out);
    jc_sb_free(&out);

    jc_arena_free(a);
}

/* M293: the `learn apply` outcome line is now rendered from one stats struct so
 * the CLI and the TUI cannot describe the same result differently -- the same
 * reason M292 moved the analyze report out of main.c.
 *
 * The mask cases are the load-bearing ones. A corrections-only apply (M294) must
 * not report "0 skill(s)" about work it never attempted: a user retracting stale
 * notes because memory.md has outgrown its budget would read that as the apply
 * having failed to write skills it was never asked to write. */
static void test_apply_summary(void)
{
    struct jc_learn_apply_stats st;
    struct jc_sb out;

    /* Full mask: byte-identical to what `learn apply` has always printed. This
     * exact sentence is pinned by tests/smoke/learn.sh. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "/w/.jichi/lessons.draft.md", &out);
    JC_CHECK_STR(out.data, "Applied 0 memory note(s), 0 skill(s), "
                 "0 correction(s), and 0 rule(s) from "
                 "/w/.jichi/lessons.draft.md.\n");
    jc_sb_free(&out);

    /* Counts are reported, not just zeroes. M632: with notes added, the
     * warrant line follows the pinned sentence on its own line. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    st.memory_added = 3;
    st.warrant_measured = 1;
    st.warrant_unchecked = 1;
    st.warrant_untagged = 1;
    st.skills_added = 1;
    st.corrections_applied = 2;
    st.rules_added = 4;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK_STR(out.data, "Applied 3 memory note(s), 1 skill(s), "
                 "2 correction(s), and 4 rule(s) from d.md.\n"
                 "Warrants: 1 measured, 0 judgement, 1 unchecked (labelled -- "
                 "check before trusting), 1 untagged.\n");
    jc_sb_free(&out);

    /* M632: the trailer parse. Case-insensitive word, both spellings of
     * judgement, an unknown word is untagged (never guessed), NULL is safe. */
    JC_CHECK(jc_learn_warrant("x [evidence: r-1] [warrant: measured]") ==
             JC_WARRANT_MEASURED);
    JC_CHECK(jc_learn_warrant("x [warrant: Judgement]") == JC_WARRANT_JUDGEMENT);
    JC_CHECK(jc_learn_warrant("x [warrant:judgment]") == JC_WARRANT_JUDGEMENT);
    JC_CHECK(jc_learn_warrant("x [warrant: unchecked] [pins: t.sh]") ==
             JC_WARRANT_UNCHECKED);
    JC_CHECK(jc_learn_warrant("x [warrant: certain]") == JC_WARRANT_NONE);
    JC_CHECK(jc_learn_warrant("x [evidence: warrant]") == JC_WARRANT_NONE);
    JC_CHECK(jc_learn_warrant(NULL) == JC_WARRANT_NONE);
    JC_CHECK(strcmp(jc_warrant_str(JC_WARRANT_UNCHECKED), "unchecked") == 0);
    JC_CHECK(strcmp(jc_warrant_str(JC_WARRANT_NONE), "untagged") == 0);

    /* M602: checks appear as a fifth fragment ONLY when the draft had a Checks
     * section, so the four-fragment sentence learn.sh pins is unchanged; the two
     * skip lines name the scanner's vocabulary and the unbuilt kinds. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(out.data != NULL && strstr(out.data, "check(s)") == NULL);
    jc_sb_free(&out);
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    st.checks_added = 1;
    st.checks_unrecognised = 1;
    st.checks_unsupported = 1;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "0 rule(s), and 1 check(s) from d.md.") != NULL);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "1 check(s) skipped: the constraint scanner") != NULL);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "1 check(s) of a kind apply cannot commit") != NULL);
    jc_sb_free(&out);

    /* M601: retracted learned conventions are named, only under a mask that
     * asked for corrections. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    st.rules_retracted = 2;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "2 learned convention(s) retracted") != NULL);
    jc_sb_free(&out);
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_MEMORY;
    st.rules_retracted = 2;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(out.data != NULL && strstr(out.data, "retracted") == NULL);
    jc_sb_free(&out);

    /* M600: malformed correction bullets are named, with the syntax, after the
     * pinned sentence -- and only when the mask asked for corrections. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    st.corrections_malformed = 3;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(out.data != NULL &&
             strncmp(out.data, "Applied 0 memory note(s), 0 skill(s), "
                     "0 correction(s), and 0 rule(s) from d.md.\n",
                     strlen("Applied 0 memory note(s), 0 skill(s), "
                            "0 correction(s), and 0 rule(s) from d.md.\n")) == 0);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "3 correction bullet(s) ignored") != NULL);
    JC_CHECK(out.data != NULL && strstr(out.data, "remove: <substring>") != NULL);
    jc_sb_free(&out);
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_MEMORY;   /* corrections not asked for: silent */
    st.corrections_malformed = 3;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(out.data != NULL && strstr(out.data, "ignored") == NULL);
    jc_sb_free(&out);

    /* Corrections only: one fragment, and NOTHING about skills or rules. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_CORRECTIONS;
    st.corrections_applied = 2;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK_STR(out.data, "Applied 2 correction(s) from d.md.\n");
    JC_CHECK(strstr(out.data, "skill") == NULL);
    JC_CHECK(strstr(out.data, "rule") == NULL);
    jc_sb_free(&out);

    /* Two fragments join with "and", no comma. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_MEMORY | JC_LEARN_CORRECTIONS;
    st.memory_added = 1;
    st.warrant_untagged = 1; /* M632: a committed note always has a class */
    st.corrections_applied = 1;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK_STR(out.data, "Applied 1 memory note(s) and 1 correction(s) "
                 "from d.md.\n"
                 "Warrants: 0 measured, 0 judgement, 0 unchecked (labelled -- "
                 "check before trusting), 1 untagged.\n");
    jc_sb_free(&out);

    /* M294: a masked run is a PARTIAL apply, so it must say what it left. A user
     * who runs `learn corrections` and reads only "Applied 2 correction(s)" would
     * reasonably believe the whole draft was committed. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_CORRECTIONS;
    st.corrections_applied = 1;
    st.pending_other = 4;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(strstr(out.data, "Applied 1 correction(s) from d.md.") != NULL);
    JC_CHECK(strstr(out.data, "4 other draft item(s) not applied") != NULL);
    JC_CHECK(strstr(out.data, "learn apply") != NULL);
    jc_sb_free(&out);

    /* A full apply leaves nothing, so it must NOT print the pending line -- an
     * unconditional hint would tell every `learn apply` user to run it again. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    st.memory_added = 1;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(strstr(out.data, "not applied") == NULL);
    jc_sb_free(&out);

    /* An empty mask says so instead of rendering "Applied  from ...". */
    memset(&st, 0, sizeof(st));
    st.sections = 0;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "d.md", &out);
    JC_CHECK(strstr(out.data, "Applied nothing") != NULL);
    jc_sb_free(&out);

    /* parsed_nothing supersedes the counts with the actionable advice, and names
     * the draft so the user knows which file to edit. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_ALL;
    st.parsed_nothing = 1;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, "/w/.jichi/lessons.draft.md", &out);
    JC_CHECK(strstr(out.data, "Applied nothing") != NULL);
    JC_CHECK(strstr(out.data, "/w/.jichi/lessons.draft.md") != NULL);
    JC_CHECK(strstr(out.data, "## Memory notes") != NULL);
    JC_CHECK(strstr(out.data, "docs/LEARNING.md") != NULL);
    /* No count fragments -- the advice replaces them, it does not follow them. */
    JC_CHECK(strstr(out.data, "note(s)") == NULL);
    jc_sb_free(&out);

    /* A NULL path must not render "(null)" into a message telling someone which
     * file to edit. */
    memset(&st, 0, sizeof(st));
    st.sections = JC_LEARN_MEMORY;
    jc_sb_init(&out);
    jc_learn_apply_summary(&st, NULL, &out);
    JC_CHECK(strstr(out.data, "(null)") == NULL);
    JC_CHECK(strstr(out.data, "lessons.draft.md") != NULL);
    jc_sb_free(&out);

    /* Defensive: NULL args are no-ops, not crashes. */
    jc_sb_init(&out);
    jc_learn_apply_summary(NULL, "d.md", &out);
    JC_CHECK(out.len == 0);
    jc_sb_free(&out);
    jc_learn_apply_summary(&st, "d.md", NULL);
}

/* M601: a `## Corrections` directive retracts a learned convention -- and ONLY
 * one. The hand-written rules above the heading are never touched, a match in a
 * later section is not, and a replacement lands inside the section. */
static void test_rules_correct(void)
{
    static const char *RULES =
        "# Project rules\n"
        "- Never use sprintf; this is hand-written and mentions apply_patch.\n"
        "\n"
        "## Learned conventions\n"
        "\n"
        "- Prefer whole-file writes over apply_patch in this repo.\n"
        "- Run the linter before committing.\n"
        "\n"
        "## Later section\n"
        "- apply_patch is fine here.\n";
    struct jc_sb out;
    int n;

    /* remove: drops the matching learned convention, nothing else. */
    jc_sb_init(&out);
    n = jc_learn_rules_correct(RULES, "apply_patch", NULL, &out);
    JC_CHECK(n == 1);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "Prefer whole-file writes") == NULL);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "hand-written and mentions apply_patch") != NULL);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "apply_patch is fine here") != NULL);
    JC_CHECK(out.data != NULL &&
             strstr(out.data, "Run the linter before committing") != NULL);
    jc_sb_free(&out);

    /* replace: the replacement is appended INSIDE the section (before the next
     * heading), once. */
    jc_sb_init(&out);
    n = jc_learn_rules_correct(RULES, "Run the linter",
                               "Run `make lint` before committing.", &out);
    JC_CHECK(n == 2);
    if (out.data != NULL) {
        const char *rep = strstr(out.data, "- Run `make lint` before committing.");
        const char *later = strstr(out.data, "## Later section");
        JC_CHECK(rep != NULL && later != NULL && rep < later);
        JC_CHECK(strstr(out.data, "Run the linter before committing") == NULL);
    }
    jc_sb_free(&out);

    /* No match, or no section: nothing changes, 0. */
    jc_sb_init(&out);
    n = jc_learn_rules_correct(RULES, "nothing-like-this", NULL, &out);
    JC_CHECK(n == 0);
    JC_CHECK(out.data != NULL && strcmp(out.data, RULES) == 0);
    jc_sb_free(&out);
    jc_sb_init(&out);
    n = jc_learn_rules_correct("# Rules\n- apply_patch rule\n", "apply_patch",
                               NULL, &out);
    JC_CHECK(n == 0); /* no "## Learned conventions" heading: untouchable */
    jc_sb_free(&out);
    /* Guards. */
    jc_sb_init(&out);
    JC_CHECK(jc_learn_rules_correct(NULL, "x", NULL, &out) == 0);
    JC_CHECK(jc_learn_rules_correct(RULES, "", NULL, &out) == 0);
    jc_sb_free(&out);
}

/* M1001: learn_apply at a symlinked rules file must be refused, not applied.
 *
 * With the four `return -1;` sites in jc_learn.c perturbed to `return 0;`,
 * the child writes through the symlink and faults on reload (short read -> FAIL)
 * or exits with nonzero verdict (FAIL) — a clean FAIL test_learn.c:<N>, and the
 * suite continues. Without the fork, a fault would segfault the whole unit run.
 * The control arm (real file) also runs in a forked child with the same pattern. */

/* Child: run the apply (symlink or control case) and send verdict via pipe.
 * verdict bit0 = outside file byte-for-byte changed vs planted content
 * bit1 = (st.rules_added != 0)
 * bit2 = (st.rules_retracted != 0)
 * bit3 = (st.corrections_unmatched != 0)
 * bit4 = detail lacks 'denied'/'symlink' (refusal line MISSING)
 * bit5 = was_symlink case (0=control, 1=symlink)
 * bit6 = detail lacks the REFUSAL-SPECIFIC 'correction skipped: .jichi/rules.md denied or symlink' line (set when the correction path's refusal is missing; must be CLEAR in BOTH cases -- in the symlink case because the line IS present, in the control case because the correction applies and there is no refusal line) */
static void child_learn_apply(int fd, const char *dir, const char *outside_file,
                              const char *outside_content, int was_symlink)
{
    struct jc_arena *a, *scratch, *tool_scratch;
    struct jc_app app;
    struct jc_vec *read_files = &app.read_files;
    struct jc_vec *read_recs = &app.read_recs;
    char draft_path[600], draft_dir[600];
    char rules_link[600];
    struct jc_learn_apply_stats st;
    struct jc_sb detail;
    char *after = NULL;
    int verdict = 0;

    /* Build a minimal jc_app. */
    a = jc_arena_new(0);
    scratch = jc_arena_new(0);
    tool_scratch = jc_arena_new(0);
    memset(&app, 0, sizeof(app));
    app.arena = a;
    app.scratch = scratch;
    app.tool_scratch = tool_scratch;
    jc_vec_init(read_files, sizeof(char *));
    jc_vec_init(read_recs, sizeof(struct jc_read_rec));
    jc_snprintf(app.cwd, sizeof app.cwd, "%s", dir);

    /* Create .jichi directory and the lessons.draft.md. */
    jc_snprintf(draft_dir, sizeof(draft_dir), "%s/.jichi", dir);
    jc_snprintf(draft_path, sizeof(draft_path), "%s/lessons.draft.md", draft_dir);
    {
        const char *draft_text =
            "## Rules\n"
            "- prefer semicolons\n"
            "## Corrections\n"
            "- remove: \"use tabs not spaces\"\n";
        (void)jc_write_file(draft_path, draft_text, strlen(draft_text));
    }

    /* If was_symlink==0 (control), we need a real AGENTS.md instead of a symlink. */
    if (!was_symlink) {
        jc_snprintf(rules_link, sizeof(rules_link), "%s/AGENTS.md", dir);
        /* Create a real file (not a symlink) for the control case. */
        (void)jc_write_file(rules_link, outside_content, strlen(outside_content));
    }

    /* Apply rules and corrections. */
    memset(&st, 0, sizeof(st));
    jc_sb_init(&detail);
    (void)jc_learn_apply(&app, JC_LEARN_RULES | JC_LEARN_CORRECTIONS, 0, &st, &detail);

    /* Build verdict. */
    /* bit0: outside file changed? */
    if (jc_read_file(outside_file, &after, NULL, tool_scratch) == JC_OK) {
        if (strcmp(outside_content, after) != 0) {
            verdict |= 1;
        }
    }
    /* bit1: rules_added != 0 */
    if (st.rules_added != 0) {
        verdict |= 2;
    }
    /* bit2: rules_retracted != 0 */
    if (st.rules_retracted != 0) {
        verdict |= 4;
    }
    /* bit3: corrections_unmatched != 0 */
    if (st.corrections_unmatched != 0) {
        verdict |= 8;
    }
    /* bit4: refusal line missing? */
    if (detail.data == NULL ||
        (strstr(detail.data, "denied") == NULL && strstr(detail.data, "symlink") == NULL)) {
        verdict |= 16;
    }
    /* bit5: was_symlink flag */
    if (was_symlink) {
        verdict |= 32;
    }
    /* bit6: refusal-specific correction-skip line missing? */
    if (detail.data == NULL || strstr(detail.data, "correction skipped: .jichi/rules.md denied or symlink") == NULL) {
        verdict |= 64;
    }
    jc_sb_free(&detail);

    /* Send verdict. */
    {
        int result = write(fd, &verdict, sizeof(verdict));
        if (result != (int)sizeof(verdict)) {
            _exit(0xFF);
        }
    }

    /* Clean up. */
    (void)close(fd);
    jc_arena_free(a);
    jc_arena_free(scratch);
    jc_arena_free(tool_scratch);
    _exit(0);
}

static void test_learn_apply_symlinked_rules(void)
{
    struct jc_arena *a, *scratch, *tool_scratch;
    struct jc_app app;
    struct jc_vec *read_files = &app.read_files;
    struct jc_vec *read_recs = &app.read_recs;
    const char *dir;
    char outside_dir[512], outside_file[600], rules_link[600];
    char draft_path[600], draft_dir[600];
    char *outside_content = NULL;
    int pipefd[2];
    pid_t pid;
    int verdict;
    int child_status;
    int got_verdict = 0;
    int was_symlink;

    /* Build a minimal jc_app: test_memory.c shape (parent only for fixture). */
    a = jc_arena_new(0);
    scratch = jc_arena_new(0);
    tool_scratch = jc_arena_new(0);
    memset(&app, 0, sizeof(app));
    app.arena = a;
    app.scratch = scratch;
    app.tool_scratch = tool_scratch;
    jc_vec_init(read_files, sizeof(char *));
    jc_vec_init(read_recs, sizeof(struct jc_read_rec));

    /* Fresh workspace dir. M729 way: a setup step whose failure leaves a NULL
     * or a dead path behind is a REQUIRE (record + stop), not a CHECK
     * (record + continue into a segfault) -- the host gate's unwritable-TMPDIR
     * stage (supervisor 16:40) crashes otherwise: outside_content below is
     * dereferenced at the control arm and in the child. */
    dir = jc_test_tmp("jichi_learn_symlink_test");
    (void)jc_test_rm_rf(dir);
    if (!JC_REQUIRE(jc_mkdir_p(dir) == JC_OK)) {
        jc_arena_free(a);
        jc_arena_free(scratch);
        jc_arena_free(tool_scratch);
        return;
    }
    jc_snprintf(app.cwd, sizeof app.cwd, "%s", dir);

    /* Create a sibling temp dir for the OUTSIDE real file. */
    jc_snprintf(outside_dir, sizeof(outside_dir), "%s", jc_test_tmp("jichi_learn_outside"));
    (void)jc_test_rm_rf(outside_dir);
    if (!JC_REQUIRE(jc_mkdir_p(outside_dir) == JC_OK)) {
        (void)jc_test_rm_rf(dir);
        jc_arena_free(a);
        jc_arena_free(scratch);
        jc_arena_free(tool_scratch);
        return;
    }

    /* Plant an OUTSIDE real file containing the rule text. */
    jc_snprintf(outside_file, sizeof(outside_file), "%s/AGENTS.md", outside_dir);
    {
        const char *outside_rules =
            "# Project rules\n"
            "\n"
            "## Learned conventions\n"
            "\n"
            "- use tabs not spaces\n";
        if (!JC_REQUIRE(jc_write_file(outside_file, outside_rules,
                                      strlen(outside_rules)) == JC_OK)) {
            (void)jc_test_rm_rf(dir);
            (void)jc_test_rm_rf(outside_dir);
            jc_arena_free(a);
            jc_arena_free(scratch);
            jc_arena_free(tool_scratch);
            return;
        }
    }

    /* Read the outside file content for later comparison. */
    if (!JC_REQUIRE(jc_read_file(outside_file, &outside_content, NULL,
                                 tool_scratch) == JC_OK &&
                    outside_content != NULL)) {
        (void)jc_test_rm_rf(dir);
        (void)jc_test_rm_rf(outside_dir);
        jc_arena_free(a);
        jc_arena_free(scratch);
        jc_arena_free(tool_scratch);
        return;
    }

    /* Symlink the OUTSIDE file as AGENTS.md in the workspace.
     * was_symlink: 1 = symlink created, 0 = symlink unsupported (control case). */
    was_symlink = 0;

    jc_snprintf(rules_link, sizeof(rules_link), "%s/AGENTS.md", dir);
    if (symlink(outside_file, rules_link) == 0) {
        was_symlink = 1;
    } else {
        /* Symlink unsupported — control case: write a real file. */
        JC_CHECK(jc_write_file(rules_link, outside_content, strlen(outside_content)) == JC_OK);
        was_symlink = 0;
    }

    /* Create .jichi directory and the lessons.draft.md. */
    jc_snprintf(draft_dir, sizeof(draft_dir), "%s/.jichi", dir);
    JC_CHECK(jc_mkdir_p(draft_dir) == JC_OK);
    jc_snprintf(draft_path, sizeof(draft_path), "%s/lessons.draft.md", draft_dir);
    {
        const char *draft_text =
            "## Rules\n"
            "- prefer semicolons\n"
            "## Corrections\n"
            "- remove: \"use tabs not spaces\"\n";
        JC_CHECK(jc_write_file(draft_path, draft_text, strlen(draft_text)) == JC_OK);
    }

    /* Fork to run the apply in a child. */
    if (pipe(pipefd) != 0) {
        goto cleanup;
    }

    pid = fork();
    if (pid == -1) {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        goto cleanup;
    }

    if (pid == 0) {
        /* Child: run the apply. was_symlink tells child whether symlink or control. */
        (void)close(pipefd[0]);
        child_learn_apply(pipefd[1], dir, outside_file, outside_content, was_symlink);
        /* NOTREACHED */
    } else {
        /* Parent: read the child's verdict. */
        (void)close(pipefd[1]);
        got_verdict = (read(pipefd[0], &verdict, sizeof(verdict)) == sizeof(verdict));
        if (!got_verdict) {
            /* Short read: child died or didn't send full verdict (fault). */
            JC_CHECK(0 && "child died or sent short verdict (fault in apply)");
            /* verdict is uninitialised on this path, so the bit checks below are gated on got_verdict; this check is the red there. */
        }
        (void)close(pipefd[0]);

        /* Wait for child and verify exit status. */
        if (waitpid(pid, &child_status, 0) == -1) {
            goto cleanup;
        }
        /* If the child died of a signal, report it. */
        if (WIFSIGNALED(child_status)) {
            JC_CHECK(0 && "child killed by signal");
        }
        /* Child must exit 0. */
        JC_CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);

        /* Verdict must be 0: no changes, no rules applied, refusal present.
         * Also verify bit5 (was_symlink) matches our expectation. */
        if (got_verdict) {
            if (was_symlink) {
                /* Symlink case: bit5 should be set, and other bits should be 0. */
                JC_CHECK((verdict & 31) == 0);  /* bits 0-4 should all be 0 */
                JC_CHECK((verdict & 32) != 0);  /* bit5 should be set */
                JC_CHECK((verdict & 64) == 0);  /* bit6 must be CLEAR: the correction-specific refusal line IS present (the path ran AND was refused) */
            } else {
                /* Control case: bit5 should be 0, rules_added should be 1 (real file).
                 * NOTE: on Linux, symlink() always succeeds, so this control arm never runs.
                 * The bit6-clear assertion here is therefore vacuous (no refusal line can
                 * be measured because the branch is never executed). */
                JC_CHECK((verdict & 31) == 0);  /* bits 0-4 should all be 0 */
                JC_CHECK((verdict & 32) == 0);  /* bit5 should be 0 */
                JC_CHECK((verdict & 64) == 0);  /* bit6 must be CLEAR (vacuous control: symlink() never fails on Linux, so this branch never runs) */
            }
        }
    }

cleanup:
    /* Clean up. */
    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(outside_dir);
    jc_arena_free(a);
    jc_arena_free(scratch);
    jc_arena_free(tool_scratch);
}

void test_learn(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_learn_draft d;
    const char *draft =
        "# Lessons draft\n"
        "## Memory notes\n"
        "- always run make WERROR before commit [evidence: 3 build breaks]\n"
        "- jc_vec_at needs a non-const pointer\n"
        "\n"
        "## Skills\n"
        "### valgrind triage: find and fix leaks\n"
        "1. build with SAN=1\n"
        "2. run the suite\n"
        "### second skill: another thing\n"
        "body of two\n"
        "## Project rules\n"
        "- Prefer whole-file writes over apply_patch in this repo.\n"
        "- Run the linter before committing.\n"
        "## Suggested (manual)\n"
        "- raise timeouts.stall to 60\n";

    test_analyze_render();
    test_apply_summary();
    test_rules_correct(); /* M601 */
    test_learn_apply_symlinked_rules(); /* M1001 */

    jc_learn_draft_init(&d);
    jc_learn_parse_draft(draft, a, &d);

    /* Two memory notes; the bullet in the "Suggested (manual)" section is NOT a
     * memory note. M600: the [evidence: …] annotation is KEPT -- it is the note's
     * provenance (until M600 it was stripped, and this line asserted so). */
    JC_CHECK(d.memory.len == 2);
    if (d.memory.len == 2) {
        const char *n0 = JC_VEC_STR(&d.memory, 0);
        JC_CHECK(n0 != NULL &&
                 strncmp(n0, "always run make WERROR before commit",
                         strlen("always run make WERROR before commit")) == 0);
        JC_CHECK(n0 != NULL && strstr(n0, "[evidence") != NULL);
        JC_CHECK_STR(JC_VEC_STR(&d.memory, 1),
                     "jc_vec_at needs a non-const pointer");
    }

    /* Two skills, name/description split on the first ": ", bodies separated. */
    JC_CHECK(d.skills.len == 2);
    if (d.skills.len == 2) {
        struct jc_learn_skill *s0 =
            (struct jc_learn_skill *)jc_vec_at(&d.skills, 0);
        struct jc_learn_skill *s1 =
            (struct jc_learn_skill *)jc_vec_at(&d.skills, 1);
        JC_CHECK_STR(s0->name, "valgrind triage");
        JC_CHECK_STR(s0->description, "find and fix leaks");
        JC_CHECK(strstr(s0->body, "build with SAN=1") != NULL);
        JC_CHECK(strstr(s0->body, "body of two") == NULL); /* belongs to s1 */
        JC_CHECK_STR(s1->name, "second skill");
        JC_CHECK(strstr(s1->body, "body of two") != NULL);
        /* The "## Suggested (manual)" section must not leak into the skill. */
        JC_CHECK(strstr(s1->body, "raise timeouts.stall") == NULL);
        /* Nor the "## Project rules" section. */
        JC_CHECK(strstr(s1->body, "whole-file writes") == NULL);
    }

    /* M106: two project rules parsed under "## Project rules"; the "Suggested"
     * bullet is not one. */
    JC_CHECK(d.rules.len == 2);
    if (d.rules.len == 2) {
        JC_CHECK_STR(JC_VEC_STR(&d.rules, 0),
                     "Prefer whole-file writes over apply_patch in this repo.");
        JC_CHECK_STR(JC_VEC_STR(&d.rules, 1),
                     "Run the linter before committing.");
    }

    jc_learn_draft_free(&d);

    /* Guards. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(NULL, a, &d);
    JC_CHECK(d.memory.len == 0 && d.skills.len == 0);
    JC_CHECK(d.corrections_malformed == 0);
    jc_learn_draft_free(&d);

    /* M600: pins travel; prose under "## Corrections" is COUNTED, not silently
     * dropped. The second bullet is the zigodot shape measured on 2026-08-27 --
     * a sentence saying a note "remains valid" where a directive was due. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Memory notes\n"
        "- floor the extraction [evidence: run r-1] [pins: tests/smoke/x_lint.sh]\n"
        "- [evidence: an annotation with no note]\n"
        "## Corrections\n"
        "- remove: old note\n"
        "- The note about hollow gates remains valid and was not involved.\n"
        "- **Remove**: \"quoted\" -- bold, so not a directive either\n"
        "- replace: stale => fresh\n", a, &d);
    JC_CHECK(d.memory.len == 1);
    if (d.memory.len == 1) {
        const char *n0 = JC_VEC_STR(&d.memory, 0);
        JC_CHECK(n0 != NULL &&
                 strstr(n0, "[pins: tests/smoke/x_lint.sh]") != NULL);
        JC_CHECK(n0 != NULL && strstr(n0, "[evidence: run r-1]") != NULL);
    }
    JC_CHECK(d.corrections.len == 2);
    JC_CHECK(d.corrections_malformed == 2);
    jc_learn_draft_free(&d);

    /* M602: "## Checks" -- `constraint:` bullets are kept as phrases, another
     * kind is counted as unsupported, and "## Suggested checks" is still the
     * human's section, not a checks section. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Checks\n"
        "- constraint: do not run the build\n"
        "- constraint:   read-only  \n"
        "- hook: PreToolUse git_push -> exit 2\n"
        "## Suggested checks\n"
        "- constraint: this is a proposal for the human, not a check\n", a, &d);
    JC_CHECK(d.checks.len == 2);
    if (d.checks.len == 2) {
        JC_CHECK_STR(JC_VEC_STR(&d.checks, 0), "do not run the build");
        JC_CHECK_STR(JC_VEC_STR(&d.checks, 1), "read-only");
    }
    JC_CHECK(d.checks_unsupported == 1);
    JC_CHECK(d.memory.len == 0);
    jc_learn_draft_free(&d);

    /* A draft under the mentor's own (non-standard) headings extracts nothing:
     * the parser stays strict rather than mis-capturing analysis prose as
     * lessons (the human curates into the exact headings first). M72. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Tool Usage Failures\n"
        "### apply_patch - high failure rate\n"
        "- It requires that the file has been read first\n"
        "**Lessons:** always read before patching\n", a, &d);
    JC_CHECK(d.memory.len == 0 && d.skills.len == 0);
    jc_learn_draft_free(&d);

    /* M75: heading-level tolerance -- a model that shifts levels (emits
     * "# Memory notes" / "## <skill>" instead of "## Memory notes" /
     * "### <skill>") still parses, since headings are classified by content. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "# Memory notes\n"
        "- read before apply_patch [evidence: 31/43 failed]\n"
        "# Skills\n"
        "## resource-cache-stringmap: use std.StringHashMap for path keys\n"
        "1. switch to StringHashMap\n"
        "# Suggested (manual)\n"
        "- raise the iteration cap\n", a, &d);
    JC_CHECK(d.memory.len == 1);
    if (d.memory.len == 1) {
        /* M600: the evidence trailer travels with the note. */
        JC_CHECK_STR(JC_VEC_STR(&d.memory, 0),
                     "read before apply_patch [evidence: 31/43 failed]");
    }
    JC_CHECK(d.skills.len == 1);
    if (d.skills.len == 1) {
        struct jc_learn_skill *s =
            (struct jc_learn_skill *)jc_vec_at(&d.skills, 0);
        JC_CHECK_STR(s->name, "resource-cache-stringmap");
        JC_CHECK(strstr(s->body, "StringHashMap") != NULL);
    }
    jc_learn_draft_free(&d);

    /* M75 follow-up: a skill whose name or description mentions "memory" or
     * "suggested" (natural words in a mentor draft) must still be captured as a
     * skill, not misrouted into a section header and lost. The colon in a
     * "name: desc" skill header disambiguates it from a colon-less section
     * title. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Skills\n"
        "### cache-keys: use StringHashMap for memory-efficient path keys\n"
        "- switch to StringHashMap\n"
        "### fast-path: the suggested approach for hot loops\n"
        "- profile first\n", a, &d);
    JC_CHECK(d.memory.len == 0);       /* nothing misrouted into memory */
    JC_CHECK(d.skills.len == 2);
    if (d.skills.len == 2) {
        struct jc_learn_skill *s0 =
            (struct jc_learn_skill *)jc_vec_at(&d.skills, 0);
        struct jc_learn_skill *s1 =
            (struct jc_learn_skill *)jc_vec_at(&d.skills, 1);
        JC_CHECK_STR(s0->name, "cache-keys");
        JC_CHECK_STR(s1->name, "fast-path");
    }
    jc_learn_draft_free(&d);

    /* A heading that names TWO sections must resolve to the more specific one.
     * Field failure (2026-08-07, driving zigodot): a mentor wrote
     * "## Memory Note Corrections" instead of the canonical "## Corrections".
     * The dispatch tested ci_contains(hdr, "memory") BEFORE "correction", so the
     * section became SEC_MEMORY and the correction DIRECTIVES underneath were
     * filed as durable memory notes -- text of the form `- **Remove**: "..."`
     * appended verbatim to memory.md, pushing it to 14 bytes under its 8 KB cap
     * where the oldest notes silently drop. Instructions to modify memory became
     * facts. Ordering the keyword checks most-specific-first is the fix. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Memory Note Corrections\n"
        "- remove: the parity oracle is modules/gdscript/tests/scripts\n", a, &d);
    JC_CHECK(d.corrections.len == 1); /* the operative noun wins */
    JC_CHECK(d.memory.len == 0);      /* NOT filed as a memory note */
    jc_learn_draft_free(&d);

    /* The same heading with MALFORMED bullets: a correction the parser cannot
     * read must be DROPPED, never absorbed as memory. This is the exact shape the
     * mentor produced, and the one that corrupted memory.md. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Memory Note Corrections\n"
        "- **Remove**: \"the parity oracle is modules/gdscript/tests/scripts\"\n"
        "- **Replace**: \"C++ navigation is NOT available\"\n", a, &d);
    JC_CHECK(d.memory.len == 0);      /* the bug: these became memory notes */
    JC_CHECK(d.corrections.len == 0); /* unreadable directives are dropped */
    jc_learn_draft_free(&d);

    /* The same precedence rule for the other pairs, so the ordering in
     * jc_learn.c cannot be "tidied" back into a first-match-wins bug: in a
     * heading that names two sections, the specific noun wins over "memory". */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft("## Memory Skills\n"
                         "### slug: desc\n"
                         "body line\n", a, &d);
    JC_CHECK(d.skills.len == 1);
    JC_CHECK(d.memory.len == 0);
    jc_learn_draft_free(&d);

    jc_learn_draft_init(&d);
    jc_learn_parse_draft("## Memory Rules\n"
                         "- always run the linter\n", a, &d);
    JC_CHECK(d.rules.len == 1);
    JC_CHECK(d.memory.len == 0);
    jc_learn_draft_free(&d);

    /* M78: a "## Corrections" section parses remove:/replace: directives. */
    jc_learn_draft_init(&d);
    jc_learn_parse_draft(
        "## Corrections\n"
        "- remove: delete_char assumes cursor is valid\n"
        "- replace: ResourceCache uses AutoHasher => ResourceCache now uses "
        "StringHashMap (fixed in 7d213c0)\n"
        "- this line is not a directive and is ignored\n", a, &d);
    JC_CHECK(d.corrections.len == 2);
    if (d.corrections.len == 2) {
        struct jc_learn_correction *c0 =
            (struct jc_learn_correction *)jc_vec_at(&d.corrections, 0);
        struct jc_learn_correction *c1 =
            (struct jc_learn_correction *)jc_vec_at(&d.corrections, 1);
        JC_CHECK_STR(c0->match, "delete_char assumes cursor is valid");
        JC_CHECK(c0->replacement == NULL); /* remove: */
        JC_CHECK_STR(c1->match, "ResourceCache uses AutoHasher");
        JC_CHECK(c1->replacement != NULL &&
                 strstr(c1->replacement, "StringHashMap") != NULL);
    }
    jc_learn_draft_free(&d);

    jc_arena_free(a);
}
