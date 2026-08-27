/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sysmsg.c - system-prompt fragments.
 *
 * M87: jc_sysmsg_append_verify_gate emits AUTO-mode guidance telling the model
 * not to re-run the verify command itself (the dominant no-prompt-cache cost).
 */

#include "jc_test.h"
#include "jc_toolcaps.h"
#include "jc_sysmsg.h"
#include "jc_envelope.h"
#include "jc_app.h"
#include "jc_assign.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_perm.h"

#include <stdlib.h>
#include <time.h>
#include "jc_skill.h"
#include "jc_output_style.h"

#include <string.h>

void test_sysmsg_verify_gate(void)
{
    struct jc_sb sb;

    /* AUTO + a verify command + a PERIODIC gate: the M87 guidance, naming the
     * command, the cadence, and telling the model not to run it itself. */
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 1, "zig build test", 5);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "zig build test") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "Do NOT run") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "run_terminal_command") != NULL);
    /* M543: the cadence is STATED, because "after your tool calls" was the
     * wording that made the default case a lie. */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "every 5 tool calls") != NULL);
    jc_sb_free(&sb);

    /* M543: AUTO + a verify command + NO periodic gate -- the DEFAULT. The gate
     * runs once, when the turn ends, so the text must say so and must NOT tell
     * the model to rely on feedback that will not arrive. Before M543 this case
     * emitted the periodic wording: it promised a gate "after your tool calls"
     * and forbade the model from checking its own work, which is advice to edit
     * blind. */
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 1, "zig build test", 0);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "zig build test") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "when your turn ENDS") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "does NOT run between your tool calls") != NULL);
    /* The prohibition must be ABSENT here: with no periodic gate, running the
     * verifier once after a batch of edits is the correct thing to do. */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "Do NOT run") == NULL);
    /* ...but the cost lesson survives in the form that still applies. */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "not after every edit") != NULL);
    jc_sb_free(&sb);

    /* A cadence of 1 is grammatical ("every 1 tool call"). */
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 1, "make ci", 1);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "every 1 tool call ") != NULL);
    jc_sb_free(&sb);

    /* Not AUTO: no-op (the guidance is AUTO-only). */
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 0, "zig build test", 5);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* AUTO but no verify command configured: no-op. */
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 1, NULL, 5);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* AUTO with an empty verify command: no-op, at either cadence. */
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 1, "", 5);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
    jc_sb_init(&sb);
    jc_sysmsg_append_verify_gate(&sb, 1, "", 0);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
}

void test_sysmsg_scope_reach(void)
{
    struct jc_sb sb;

    /* M387 STATE-THE-REACH: with an edit scope armed, the model is told the
     * scope covers the file tools and the shell reaches past it, DETECTED
     * afterward (deterrent framing, not an invitation). */
    struct jc_vec scope;
    char *g1 = "docs/plan.md";
    char *g2 = "src/*.c";

    jc_vec_init(&scope, sizeof(char *));
    jc_vec_push(&scope, &g1);
    jc_vec_push(&scope, &g2);

    jc_sb_init(&sb);
    jc_sysmsg_append_scope_reach(&sb, &scope);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "edit-scope") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "run_terminal_command") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "detected") != NULL);
    /* Must NOT phrase it as a way out (no "escape"/"bypass" invitation). */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "escape the") == NULL);
    /* M431: the globs THEMSELVES must be in the prompt. This paragraph said
     * "the edit scope above" while nothing above named it, so the only way to
     * learn the writable paths was to violate the fence and read the refusal
     * (one run guessed 177 times before M333 put the list in that refusal). */
    JC_CHECK(sb.data != NULL && strstr(sb.data, "docs/plan.md") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "src/*.c") != NULL);
    jc_sb_free(&sb);

    /* The shared list renderer, which the out-of-scope refusal also uses so the
     * two lists cannot differ: " a b" with a leading space per entry. */
    jc_sb_init(&sb);
    jc_sysmsg_append_scope_list(&sb, &scope);
    JC_CHECK(sb.data != NULL && strcmp(sb.data, " docs/plan.md src/*.c") == 0);
    jc_sb_free(&sb);
    jc_vec_free(&scope);

    /* No edit scope: no-op (the note is meaningless without a scope to reach
     * past, and it must not churn the cached prefix on unscoped runs). Both an
     * empty vec and a NULL one, since the caller passes NULL when env is unset. */
    jc_vec_init(&scope, sizeof(char *));
    jc_sb_init(&sb);
    jc_sysmsg_append_scope_reach(&sb, &scope);
    JC_CHECK(sb.len == 0);
    jc_sysmsg_append_scope_reach(&sb, NULL);
    JC_CHECK(sb.len == 0);
    jc_sysmsg_append_scope_list(&sb, NULL);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
    jc_vec_free(&scope);
}

void test_sysmsg_design(void)
{
    struct jc_sb sb;

    /* A design doc: the authoritative-plan section is emitted, containing the
     * header, the "surface the conflict" preamble, and the design body. */
    jc_sb_init(&sb);
    jc_sysmsg_append_design(&sb, "Reuse loadMember; do not re-emit get_member.", 0);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "# Design specification") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "authoritative") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "SURFACE the conflict") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "Reuse loadMember") != NULL);
    jc_sb_free(&sb);

    /* NULL / empty design: no-op (no spurious section on a normal run). */
    jc_sb_init(&sb);
    jc_sysmsg_append_design(&sb, NULL, 0);
    JC_CHECK(sb.len == 0);
    jc_sysmsg_append_design(&sb, "", 0);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* M462: a cap truncates the body and says so, while the preamble -- which
     * is what makes the section authoritative -- always survives intact. A cap
     * that could eat the preamble would leave an unlabelled block of text the
     * model has no instruction about. */
    jc_sb_init(&sb);
    jc_sysmsg_append_design(&sb, "AAAABBBBCCCCDDDDEEEEFFFF", 8);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "SURFACE the conflict") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "AAAABBBB") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "EEEE") == NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "truncated") != NULL);
    jc_sb_free(&sb);

    /* cap 0 means "no limit" downstream, so a long design is emitted whole --
     * the contract fit_caps relies on when the budget is unknown. */
    jc_sb_init(&sb);
    jc_sysmsg_append_design(&sb, "AAAABBBBCCCCDDDDEEEEFFFF", 0);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "EEEEFFFF") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "truncated") == NULL);
    jc_sb_free(&sb);
}

void test_sysmsg_language(void)
{
    struct jc_sb sb;

    /* A configured language: the section is emitted, naming the language and
     * telling the model to keep code/identifiers untranslated. */
    jc_sb_init(&sb);
    jc_sysmsg_append_language(&sb, "Japanese");
    JC_CHECK(sb.data != NULL && strstr(sb.data, "# Language") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "Respond in Japanese") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "identifiers") != NULL);
    jc_sb_free(&sb);

    /* The value is free-form and passed verbatim (a native name works). */
    jc_sb_init(&sb);
    jc_sysmsg_append_language(&sb, "Deutsch");
    JC_CHECK(sb.data != NULL && strstr(sb.data, "Respond in Deutsch") != NULL);
    jc_sb_free(&sb);

    /* NULL / empty: no-op (no spurious section on a normal run). */
    jc_sb_init(&sb);
    jc_sysmsg_append_language(&sb, NULL);
    JC_CHECK(sb.len == 0);
    jc_sysmsg_append_language(&sb, "");
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
}

/* M173b: the three assignment stances are mutually exclusive.
 *
 * The hazard this pins: the authoring stance instructs the model to write a
 * REFERENCE SOLUTION. Fired while a learner works the task, it hands them the
 * answer; fired while the model IS the learner (attempt), it invites cheating
 * against its own grader. So: authoring only when nothing is active; solving
 * when the model is the learner; tutor (guide, never solve) when a human is. */
void test_sysmsg_stances(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_assign_spec spec;
    char *msg;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, jc_test_tmpdir());
    app.config.assignments = 1;

    memset(&spec, 0, sizeof(spec));
    spec.title = "T-STANCE-1";
    spec.task = "do the thing";

    /* No active assignment -> authoring stance only. */
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Assignments mode") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Tutor stance") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Solving an assignment") == NULL);

    /* Active + the model is the learner (attempt) -> solving stance only. */
    app.assignment = &spec;
    app.assignment_tutor = 0;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Solving an assignment") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Assignments mode") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Tutor stance") == NULL);

    /* Active + a HUMAN is the learner (TUI /assignment) -> tutor stance only,
     * naming the assignment and forbidding the solution. */
    app.assignment_tutor = 1;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Tutor stance") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "T-STANCE-1") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "NEVER write the solution") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Assignments mode") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Solving an assignment") == NULL);

    /* Feature off entirely -> none of the three. */
    app.config.assignments = 0;
    app.assignment = NULL;
    app.assignment_tutor = 0;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Assignments mode") == NULL);

    jc_arena_free(a);
}

/* M302: a specialist's tone, by NAME. Agent profiles and skills gained a `style:`
 * key that names an existing OUTPUT STYLE rather than carrying prose -- reusing the
 * M28 mechanism so "blunt reviewer" is configured once and shared, instead of a
 * second persona path drifting from the first. */
void test_sysmsg_style(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    const char *msg;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, "/w");
    strcpy(app.root, "/w");
    jc_output_style_set_init(&app.output_styles);

    /* jc_output_style_parse fills ONE style; the set owns a vector of them. */
    {
        struct jc_output_style st1;
        struct jc_output_style st2;
        memset(&st1, 0, sizeof(st1));
        memset(&st2, 0, sizeof(st2));
        jc_output_style_parse("---\ndescription: calm\n---\nBe gentle and patient.",
                              "gentle", a, &st1);
        jc_output_style_parse("---\ndescription: terse\n---\nBe blunt. No hedging.",
                              "blunt", a, &st2);
        jc_vec_push(&app.output_styles.styles, &st1);
        jc_vec_push(&app.output_styles.styles, &st2);
    }

    /* No style anywhere: no section at all. */
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "# Output style") == NULL);

    /* The session style applies, as before M302. */
    JC_CHECK(jc_output_style_set_active(&app.output_styles, "gentle") == 1);
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Be gentle") != NULL);

    /* THE PRECEDENCE: a specialist's own style beats the session's. */
    app.style_override = "blunt";
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Be blunt") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Be gentle") == NULL);

    /* A name that resolves to NOTHING falls back to the session style rather than
     * emitting an empty section -- the dead name is doctor's business (M285's
     * lesson: a declared-but-dead name is worse than an absent one, so it gets a
     * check, not silent breakage). */
    app.style_override = "no-such-style";
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Be gentle") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "# Output style") != NULL);

    /* An override with no session style and no match: no section, no crash. */
    app.output_styles.active = NULL;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "# Output style") == NULL);

    /* An empty override string is treated as absent, not as a name to look up. */
    app.style_override = "";
    JC_CHECK(jc_output_style_set_active(&app.output_styles, "blunt") == 1);
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Be blunt") != NULL);

    jc_output_style_set_free(&app.output_styles);
    jc_arena_free(a);
}

/* M299: the craft section -- design-first and honesty, stated as behaviour an
 * observer could check rather than as vocabulary. Two properties matter: it is on
 * by default, and it survives a persona override, because HOW to work is not the
 * same thing as WHO is working (a reviewer profile still analyses before it
 * concludes). */
void test_sysmsg_craft(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    const char *msg;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, "/w");
    strcpy(app.root, "/w");

    /* Default: on, without any config being set. */
    app.config.craft = 1;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "How to work (the craft)") != NULL);
    /* The load-bearing lines, each an instruction rather than an adjective. */
    JC_CHECK(msg != NULL && strstr(msg, "Design before implementing") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "short design note") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "alternatives you rejected") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "never seen failing") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "did and did not do") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "without hurry") != NULL);

    /* Off: the whole section disappears, for a small window or a token-cost
     * measurement. */
    app.config.craft = 0;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "How to work (the craft)") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "Design before implementing") == NULL);

    /* A persona override replaces WHO is working; the craft still applies. This is
     * the asymmetry with the base persona, which the override does replace. */
    app.config.craft = 1;
    app.persona_override = "You are a blunt reviewer.";
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "blunt reviewer") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "How to work (the craft)") != NULL);

    /* M300: the untrusted-content rule is UNCONDITIONAL -- unlike the craft
     * section it is a safety rule, and a user who turns off prose guidance must
     * not silently turn off the injection warning with it. This is the one
     * jc_untrusted call site a unit test can reach. */
    app.persona_override = NULL;
    app.config.craft = 0;
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "How to work (the craft)") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "# Untrusted content") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "UNTRUSTED") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "do not comply") != NULL);

    jc_arena_free(a);
}

/* M312: the system-prompt breakdown. jc_sysmsg_build_parts reports each section's
 * byte count as it appends, and the ONE property that keeps that honest is that
 * the parts SUM to the built prompt.
 *
 * This is the test, not a nicety: a future section appended without a
 * `mark(parts, ...)` call makes the sum fall short here, which is the whole
 * reason the breakdown is produced by the builder rather than by a second pass
 * that re-measures each piece (that second pass is what left "the base persona +
 * section headers" as an unnamed remainder, and it explained nothing at all on a
 * graded attempt where rules and repo map are both absent).
 *
 * Checked across a spread of configurations, because a missing mark inside a
 * conditional block is invisible when the condition is false. */
static void check_parts_sum(struct jc_app *app, const char *what)
{
    struct jc_sysmsg_parts p;
    char *msg = jc_sysmsg_build_parts(app, &p);
    jc_size sum = 0;
    int i;

    (void)what;
    for (i = 0; i < JC_SYSPART_COUNT; i++) {
        sum += p.bytes[i];
    }
    /* total is the built length, and every byte is charged to exactly one part. */
    JC_CHECK(msg != NULL);
    JC_CHECK(p.total == (msg != NULL ? (jc_size)strlen(msg) : 0));
    JC_CHECK(sum == p.total);
}

void test_sysmsg_parts(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_assign_spec spec;
    struct jc_sysmsg_parts p;
    char *msg;
    char *rbuf;
    char *mbuf;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, "/w");
    strcpy(app.root, "/w");
    jc_output_style_set_init(&app.output_styles);
    jc_skill_set_init(&app.skills);

    /* The default shape: persona + craft + safety + env, and EXACTLY nothing
     * else. The exhaustive zero list is the load-bearing part.
     *
     * `sum == total` alone is weaker than it looks: it catches a section appended
     * after the LAST mark, but a section inserted between two marks is silently
     * charged to the following slot and the sum still balances. (Verified by
     * inserting one before the skills catalog: the sum check stayed green.) The
     * thirteen must-be-zero assertions are what close most of that gap -- an
     * unmarked append anywhere upstream of an inactive slot lands in it and turns
     * a 0 into a positive number. What remains uncaught is an insertion between
     * two *active* slots, where it is credited to a neighbour: a misattribution
     * rather than an unexplained remainder, and the honest limit of this
     * approach. Stated in jc_sysmsg.h too. */
    app.config.craft = 1;
    check_parts_sum(&app, "default");
    msg = jc_sysmsg_build_parts(&app, &p);
    JC_CHECK(msg != NULL);
    JC_CHECK(p.bytes[JC_SYSPART_PERSONA] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_CRAFT] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_SAFETY] > 0);   /* unconditional (M300) */
    /* M440: zero HERE because the fixture's model has prompt caching on (the
     * default), and `auto` emits the cost section only when caching is off. A new
     * slot missing from this list would be the exact hole the list exists to
     * close: the sum still balances, so nothing else would notice. */
    JC_CHECK(p.bytes[JC_SYSPART_COST] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_ENV] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_EXTRA] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_LANGUAGE] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_STYLE] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_RULES] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_DESIGN] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_CONSTRAINTS] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_MEMORY] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_GLOSSARY] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_BOARD] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_REPOMAP] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_ASSIGNMENT] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_SKILLS] == 0);

    /* craft off: that slot empties, the safety rule does NOT (M300's asymmetry,
     * asserted here because the breakdown is where someone would notice it). */
    app.config.craft = 0;
    msg = jc_sysmsg_build_parts(&app, &p);
    JC_CHECK(msg != NULL);
    JC_CHECK(p.bytes[JC_SYSPART_CRAFT] == 0);
    JC_CHECK(p.bytes[JC_SYSPART_SAFETY] > 0);
    check_parts_sum(&app, "craft off");
    app.config.craft = 1;

    /* Rules and repo map are charged AS CAPPED, which is what the model gets:
     * with a tiny context limit the M73 fit bites, and the recorded size must be
     * the truncated one, not strlen(app->rules). The pre-M312 report measured the
     * raw string and so described text that was not in the prompt. */
    /* Casts are for the C++ build (make CC=g++): C converts void* implicitly and
     * C++ refuses to. This file compiled for months because the multi-toolchain
     * gate is not run routinely -- see M332. */
    rbuf = (char *)jc_arena_alloc(a, 4001);
    memset(rbuf, 'r', 4000);
    rbuf[4000] = '\0';
    app.rules = rbuf;
    mbuf = (char *)jc_arena_alloc(a, 4001);
    memset(mbuf, 'm', 4000);
    mbuf[4000] = '\0';
    app.repo_map = mbuf;
    app.config.context_limit = 1000;    /* 45% of 4000 bytes: both must shrink */
    msg = jc_sysmsg_build_parts(&app, &p);
    JC_CHECK(msg != NULL);
    JC_CHECK(p.bytes[JC_SYSPART_RULES] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_RULES] < 4000);   /* capped, not raw */
    JC_CHECK(p.bytes[JC_SYSPART_REPOMAP] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_REPOMAP] < 4000);
    check_parts_sum(&app, "capped rules + map");

    /* Uncapped: a generous window leaves both in full, so the recorded size is
     * at least the raw length (plus this section's own header bytes). */
    app.config.context_limit = 200000;
    msg = jc_sysmsg_build_parts(&app, &p);
    JC_CHECK(msg != NULL);
    JC_CHECK(p.bytes[JC_SYSPART_RULES] >= 4000);
    JC_CHECK(p.bytes[JC_SYSPART_REPOMAP] >= 4000);
    check_parts_sum(&app, "uncapped rules + map");

    /* The optional sections, one at a time, each into its own slot. */
    app.memory = "- a durable note";
    app.glossary = "jichi: this program";
    app.config.system_prompt_extra = "house style: terse";
    app.config.language = "German";
    msg = jc_sysmsg_build_parts(&app, &p);
    JC_CHECK(msg != NULL);
    JC_CHECK(p.bytes[JC_SYSPART_MEMORY] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_GLOSSARY] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_EXTRA] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_LANGUAGE] > 0);
    check_parts_sum(&app, "optional sections");

    /* A persona override replaces the persona; craft survives it (M299). */
    app.persona_override = "You are a reviewer.";
    msg = jc_sysmsg_build_parts(&app, &p);
    JC_CHECK(msg != NULL);
    JC_CHECK(p.bytes[JC_SYSPART_PERSONA] > 0);
    JC_CHECK(p.bytes[JC_SYSPART_CRAFT] > 0);
    check_parts_sum(&app, "persona override");
    app.persona_override = NULL;

    /* Each mode, since PLAN/AUTO append into the ENV slot. */
    app.mode = JC_MODE_PLAN;
    check_parts_sum(&app, "plan mode");
    app.mode = JC_MODE_AUTO;
    check_parts_sum(&app, "auto mode");
    app.mode = JC_MODE_CHAT;

    /* Each assignment stance, all three sharing the ASSIGNMENT slot. */
    memset(&spec, 0, sizeof(spec));
    spec.title = "T-PARTS";
    spec.task = "do the thing";
    app.config.assignments = 1;
    msg = jc_sysmsg_build_parts(&app, &p);      /* authoring */
    JC_CHECK(msg != NULL && p.bytes[JC_SYSPART_ASSIGNMENT] > 0);
    check_parts_sum(&app, "assignment authoring");
    app.assignment = &spec;
    check_parts_sum(&app, "assignment solving");
    app.assignment_tutor = 1;
    msg = jc_sysmsg_build_parts(&app, &p);      /* tutor */
    JC_CHECK(msg != NULL && p.bytes[JC_SYSPART_ASSIGNMENT] > 0);
    check_parts_sum(&app, "assignment tutor");

    /* NULL parts is the plain build, and must not crash or differ. */
    {
        char *plain = jc_sysmsg_build(&app);
        msg = jc_sysmsg_build_parts(&app, &p);
        JC_CHECK(plain != NULL && msg != NULL);
        JC_CHECK(strcmp(plain, msg) == 0);
    }

    /* Every slot has a label, and an out-of-range index is "other" not a crash. */
    {
        int i;
        for (i = 0; i < JC_SYSPART_COUNT; i++) {
            JC_CHECK(jc_sysmsg_part_name(i) != NULL);
            JC_CHECK(jc_sysmsg_part_name(i)[0] != '\0');
        }
        JC_CHECK(strcmp(jc_sysmsg_part_name(-1), "other") == 0);
        JC_CHECK(strcmp(jc_sysmsg_part_name(JC_SYSPART_COUNT), "other") == 0);
    }

    jc_output_style_set_free(&app.output_styles);
    jc_skill_set_free(&app.skills);
    jc_arena_free(a);
}

/* M319: the solving stance must name only the support tools that are ACTUALLY
 * advertised. Under `--tool-profile core` none of hint/ask_for_help/
 * spawn_subagent is sent, and the block used to instruct the model to call all
 * three -- the M285 class of defect (a declared name that resolves to nothing)
 * one layer up from a tool fence.
 *
 * Asserted here rather than by grepping `sysmsg` output: the solving stance is
 * emitted only while an assignment is ACTIVE, which the subcommand never is, so
 * a grep against `sysmsg` matches unrelated prose ("research hints", a rules
 * file) and reports a defect that is not there. That is exactly what happened
 * while writing this milestone. */
void test_sysmsg_solving_stance_tools(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_assign_spec spec;
    char *msg;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, "/w");
    strcpy(app.root, "/w");
    jc_output_style_set_init(&app.output_styles);
    jc_skill_set_init(&app.skills);
    memset(&spec, 0, sizeof(spec));
    spec.title = "T-STANCE-TOOLS";
    spec.task = "solve the thing";
    app.config.assignments = 1;
    app.assignment = &spec;
    app.assignment_tutor = 0;

    /* full profile: the three support tools are advertised, so name them. */
    app.config.tool_profile = 0;            /* full */
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Solving an assignment") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "`hint`") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "`ask_for_help`") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "`spawn_subagent`") != NULL);

    /* core profile: none of them is advertised, so none may be promised -- and
     * the absence is STATED, not left as a heading with nothing under it. */
    app.config.tool_profile = 1;            /* core */
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "Solving an assignment") != NULL);
    JC_CHECK(msg != NULL && strstr(msg, "`hint`") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "`ask_for_help`") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "`spawn_subagent`") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "no hint or help tools") != NULL);
    /* The grading sentence survives in both: it is not about tools. */
    JC_CHECK(msg != NULL && strstr(msg, "verification command") != NULL);

    /* `auto` below the threshold resolves to core too, so the same must hold
     * without anyone setting the profile by hand -- which is the configuration a
     * small-window user actually lands in. */
    app.config.tool_profile = -1;           /* auto */
    app.config.context_limit = 8000;        /* < JC_TOOL_PROFILE_AUTO_BELOW */
    msg = jc_sysmsg_build(&app);
    JC_CHECK(msg != NULL && strstr(msg, "`hint`") == NULL);
    JC_CHECK(msg != NULL && strstr(msg, "no hint or help tools") != NULL);

    jc_output_style_set_free(&app.output_styles);
    jc_skill_set_free(&app.skills);
    jc_arena_free(a);
}

/* M352: the one line of clock -- ISO only (LC_TIME is deliberately localized
 * for display, so the canonical prompt must use the numeric form), pinned by
 * epoch + TZ so the test is deterministic anywhere. */
void test_sysmsg_date(void)
{
    struct jc_sb sb;

    setenv("TZ", "UTC", 1);
    tzset();
    jc_sb_init(&sb);
    /* 1786320000 = 2026-08-10 06:40 UTC. */
    jc_sysmsg_append_date(&sb, 1786320000L);
    JC_CHECK(sb.data != NULL &&
             strcmp(sb.data, "- Today's date: 2026-08-10\n") == 0);
    jc_sb_free(&sb);

    /* A different day renders differently (the line is live, not baked). */
    jc_sb_init(&sb);
    jc_sysmsg_append_date(&sb, 1786320000L + 86400L);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "2026-08-11") != NULL);
    jc_sb_free(&sb);

    /* NULL sb: no crash. */
    jc_sysmsg_append_date(NULL, 1786320000L);
}

/* M358: the context gauge's static half -- present with the configured
 * number and the reading habit when the limit is a fact, absent when only
 * the built-in default would apply (the M310 presence/absence pairing). */
void test_sysmsg_context_window(void)
{
    struct jc_sb sb;

    jc_sb_init(&sb);
    jc_sysmsg_append_context_window(&sb, 16000L);
    JC_CHECK(sb.data != NULL
             && strstr(sb.data, "- Context window: ~16000 tokens") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "offset/limit") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "search_code") != NULL);
    jc_sb_free(&sb);

    /* Unknown limit: NOTHING -- never the built-in default's number. */
    jc_sb_init(&sb);
    jc_sysmsg_append_context_window(&sb, 0L);
    JC_CHECK(sb.len == 0);
    jc_sysmsg_append_context_window(&sb, -1L);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* NULL sb: no crash. */
    jc_sysmsg_append_context_window(NULL, 16000L);
}

/* M355: the flight plan -- armed limits only, nothing when nothing is armed.
 * Deref checks combined with their guards (the M349 rule). */
void test_sysmsg_envelope(void)
{
    struct jc_envelope e;
    struct jc_sb sb;

    jc_sb_init(&sb);
    jc_sysmsg_append_envelope(&sb, NULL);
    JC_CHECK(sb.len == 0);
    memset(&e, 0, sizeof e);
    jc_sysmsg_append_envelope(&sb, &e);
    JC_CHECK(sb.len == 0);              /* nothing armed => nothing said */

    e.max_tool_calls = 30;
    jc_sysmsg_append_envelope(&sb, &e);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "This run is bounded") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "- tool calls: 30") != NULL);
    JC_CHECK(sb.data == NULL || strstr(sb.data, "token budget") == NULL);
    JC_CHECK(sb.data == NULL || strstr(sb.data, "deadline") == NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "budget check arrives when ~80%") != NULL);
    jc_sb_free(&sb);

    jc_sb_init(&sb);
    e.budget_tokens = 500000.0;
    e.deadline_secs = 1800;
    e.max_reads = 40;
    jc_sysmsg_append_envelope(&sb, &e);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "- token budget: 500000") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "- wall-clock deadline: 1800 seconds") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "- read-tool calls: 40") != NULL);
    jc_sb_free(&sb);
}

/* M365: the fit-budget deadband. The M77 ratio wobbles on every call and the
 * M73 caps derive linearly from this value; unheld, the truncation byte of
 * the rules/repo-map moved each turn and the M31 cached prefix re-billed
 * silently. A stateless quantization was the first design and only RELOCATES
 * the flapping to step boundaries -- these cases pin the hysteresis instead:
 * hold through jitter smaller than the band, refit once on a real shift,
 * refit on a limit change, and stay exact when uncalibrated. */
void test_sysmsg_fit_budget(void)
{
    long held = 0, hlim = 0;

    /* Uncalibrated (cal <= 1.0): the limit itself, held. */
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.0, &held, &hlim) == 32000L);
    /* Small upward jitter past 1.0 stays HELD (within the 1024 band),
     * even though the raw deflation would be 31372. */
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.02, &held, &hlim) == 32000L);

    /* A real ratio arrives: refit once... */
    held = 0; hlim = 0;
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.30, &held, &hlim) == 24615L);
    /* ...then jitter in both directions is held (drift 188 and 191). */
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.31, &held, &hlim) == 24615L);
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.29, &held, &hlim) == 24615L);
    /* A genuine shift (drift 2547 >= 1024) refits and re-holds. */
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.45, &held, &hlim) == 22068L);
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.46, &held, &hlim) == 22068L);

    /* A limit change always refits (the hold was for another window). */
    JC_CHECK(jc_sysmsg_fit_budget(16000L, 1.45, &held, &hlim) == 11034L);

    /* Small limits scale the band (4000/8 = 500). */
    held = 0; hlim = 0;
    JC_CHECK(jc_sysmsg_fit_budget(4000L, 2.0, &held, &hlim) == 2000L);
    JC_CHECK(jc_sysmsg_fit_budget(4000L, 2.6, &held, &hlim) == 2000L);
    JC_CHECK(jc_sysmsg_fit_budget(4000L, 3.4, &held, &hlim) == 1176L);

    /* Tiny limits keep a floor band of 64. */
    held = 0; hlim = 0;
    JC_CHECK(jc_sysmsg_fit_budget(400L, 2.0, &held, &hlim) == 200L);
    JC_CHECK(jc_sysmsg_fit_budget(400L, 2.5, &held, &hlim) == 200L);
    JC_CHECK(jc_sysmsg_fit_budget(400L, 4.0, &held, &hlim) == 100L);

    /* No/unknown limit passes through untouched; NULL state = stateless. */
    JC_CHECK(jc_sysmsg_fit_budget(0L, 2.0, &held, &hlim) == 0L);
    JC_CHECK(jc_sysmsg_fit_budget(-5L, 2.0, &held, &hlim) == -5L);
    JC_CHECK(jc_sysmsg_fit_budget(32000L, 1.30, NULL, NULL) == 24615L);
}


/* M440: the `# Cost model` section -- the gate and the text.
 *
 * WHAT THIS IS FOR. docs/TOOL_OUTPUT_COST.md §6 items 5-8 are measured advice
 * addressed to a HUMAN, to be copied by hand into an AGENTS.md, while jichi knew
 * every input at runtime. The section closes that gap. Two properties matter more
 * than the prose: the GATE (the right read policy is opposite on the two backend
 * classes, so unconditional frugality prose is wrong on one of them) and PREFIX
 * STABILITY (every input is config-derived, because the tempting thing to state
 * here -- the observed cache hit-rate -- would change the cached prefix every turn
 * and destroy the caching it describes). */
void test_sysmsg_cost_model(void)
{
    struct jc_sb sb;
    struct jc_sb sb2;

    /* --- the gate, exhaustively ---------------------------------------------
     * An explicit 0/1 wins over the cache verdict either way: an operator who has
     * measured their backend outranks a heuristic -- which matters because a
     * backend can silently ignore a caching request, a measured case the
     * configured-value gate cannot see. */
    JC_CHECK(jc_config_cost_model_on(1, 1) == 1);   /* forced on, caching on  */
    JC_CHECK(jc_config_cost_model_on(1, 0) == 1);   /* forced on, caching off */
    JC_CHECK(jc_config_cost_model_on(0, 0) == 0);   /* forced off, caching off*/
    JC_CHECK(jc_config_cost_model_on(0, 1) == 0);   /* forced off, caching on */
    /* auto: on iff caching is off -- where §1's multiplier applies to every byte.
     * With a cached prefix the same prose is billed once and buys little, which is
     * why the section is not unconditional. */
    JC_CHECK(jc_config_cost_model_on(-1, 0) == 1);
    JC_CHECK(jc_config_cost_model_on(-1, 1) == 0);

    /* --- off emits NOTHING, not a shorter section -------------------------- */
    jc_sb_init(&sb);
    jc_sysmsg_append_cost_model(&sb, 0, 65536, 16384, 32768, 16384, 8192);
    JC_CHECK(sb.data == NULL || sb.data[0] == '\0');
    jc_sb_free(&sb);

    /* --- on: the NUMBERS, in KB -------------------------------------------
     * Numbers rather than adjectives is the point. "Keep reads small" is
     * unactionable; "read_file 64 KB" lets a model predict a truncation before it
     * pays for one. */
    jc_sb_init(&sb);
    jc_sysmsg_append_cost_model(&sb, 1, 65536, 16384, 32768, 16384, 8192);
    JC_CHECK(sb.data != NULL);
    JC_CHECK(strstr(sb.data, "# Cost model") != NULL);
    JC_CHECK(strstr(sb.data, "read_file 64 KB") != NULL);
    JC_CHECK(strstr(sb.data, "run_terminal_command 16 KB") != NULL);
    JC_CHECK(strstr(sb.data, "fetch_url 32 KB") != NULL);
    JC_CHECK(strstr(sb.data, "search_code 16 KB") != NULL);
    JC_CHECK(strstr(sb.data, "git tools 8 KB") != NULL);
    /* the four measured behaviours (§6 items 5-8) */
    JC_CHECK(strstr(sb.data, "Search before you read") != NULL);
    JC_CHECK(strstr(sb.data, "`offset` and `limit`") != NULL);
    JC_CHECK(strstr(sb.data, "re-read") != NULL);
    JC_CHECK(strstr(sb.data, "Batch shell work") != NULL);
    /* the WHY, stated once: a tool result is re-sent with every later request. A
     * rule without its reason is one a model discards under pressure. */
    JC_CHECK(strstr(sb.data, "re-sent") != NULL);
    /* It must NOT tell the model to refuse or narrow an explicit request:
     * TOOL_OUTPUT_COST §7 rejected auto-bounding reads, and prose instructing the
     * model to do by hand what jichi declined to do in code would re-open that
     * decision through the back door. */
    JC_CHECK(strstr(sb.data, "refuse") == NULL);

    /* --- the caps are reported, not invented ------------------------------ */
    jc_sb_init(&sb2);
    jc_sysmsg_append_cost_model(&sb2, 1, JC_CAP_READ_DEFAULT, JC_CAP_RUN_DEFAULT,
                                JC_CAP_FETCH_DEFAULT, JC_CAP_SEARCH_DEFAULT,
                                JC_CAP_GIT_DEFAULT);
    JC_CHECK(sb2.data != NULL && strstr(sb2.data, "read_file 256 KB") != NULL);
    JC_CHECK(sb2.data != NULL && strstr(sb2.data, "git tools 32 KB") != NULL);
    /* A different cap set must produce different text, so the numbers are really
     * threaded through rather than baked into the literal. */
    JC_CHECK(sb.data != NULL && sb2.data != NULL &&
             strcmp(sb.data, sb2.data) != 0);
    jc_sb_free(&sb2);
    jc_sb_free(&sb);

    /* --- prefix stability: same inputs, byte-identical output --------------
     * Weak as a unit check, since the function is pure -- but it states the
     * CONTRACT, and it is the assertion that fails first if someone later threads a
     * live number (a hit rate, a token total, an elapsed time) into this section.
     * M31d's guard covers the built request; this covers the section in isolation,
     * which is where that mistake would actually be made. */
    jc_sb_init(&sb);
    jc_sb_init(&sb2);
    jc_sysmsg_append_cost_model(&sb, 1, 65536, 16384, 32768, 16384, 8192);
    jc_sysmsg_append_cost_model(&sb2, 1, 65536, 16384, 32768, 16384, 8192);
    JC_CHECK(sb.data != NULL && sb2.data != NULL &&
             strcmp(sb.data, sb2.data) == 0);
    jc_sb_free(&sb);
    jc_sb_free(&sb2);

    /* --- NULL-safety: jc_sysmsg_build calls this unconditionally ----------- */
    jc_sysmsg_append_cost_model(NULL, 1, 1, 1, 1, 1, 1);   /* must not crash */
}

/* M596: a command's `agent:` persona is the identity paragraph of its subtask's
 * system prompt, and the sections jichi enforces at depth still follow it.
 *
 * The hazard this pins: until M596, jc_agent_run_command_subtask built the
 * generic delegate prompt and never read app->persona_override -- so the
 * scaffolded mentor never once received its own FORMAT IS STRICT block, while
 * jc_agent.h claimed the persona applied. The smoke driver
 * (tests/smoke/subtask_persona.sh) proves delivery on the wire; this pins the
 * builder's contract: NULL/empty persona is exactly the generic prompt, a persona
 * replaces the identity paragraph AND its delegation clause, and the untrusted
 * rule plus the environment are appended in both shapes. */
void test_sysmsg_sub_persona(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    char *generic;
    char *as_null;
    char *as_empty;
    char *as_zed;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, "/w");
    strcpy(app.root, "/w");
    jc_output_style_set_init(&app.output_styles);
    jc_skill_set_init(&app.skills);

    generic = jc_sysmsg_build_sub(&app);
    as_null = jc_sysmsg_build_sub_as(&app, NULL, NULL);
    as_empty = jc_sysmsg_build_sub_as(&app, "", NULL);
    JC_CHECK(generic != NULL && as_null != NULL && as_empty != NULL);
    /* No persona: byte-identical to the generic prompt, both spellings. */
    JC_CHECK(generic != NULL && as_null != NULL &&
             strcmp(generic, as_null) == 0);
    JC_CHECK(generic != NULL && as_empty != NULL &&
             strcmp(generic, as_empty) == 0);
    JC_CHECK(generic != NULL &&
             strstr(generic, "You are a focused sub-agent") != NULL);
    JC_CHECK(generic != NULL && strstr(generic, "UNTRUSTED") != NULL);

    as_zed = jc_sysmsg_build_sub_as(&app, "You are Zed, a blunt reviewer.",
                                    NULL);
    JC_CHECK(as_zed != NULL);
    /* The persona IS the identity paragraph, first and verbatim... */
    JC_CHECK(as_zed != NULL &&
             strncmp(as_zed, "You are Zed, a blunt reviewer.",
                     strlen("You are Zed, a blunt reviewer.")) == 0);
    /* ...the generic identity and its delegation clause are gone... */
    JC_CHECK(as_zed != NULL &&
             strstr(as_zed, "You are a focused sub-agent") == NULL);
    JC_CHECK(as_zed != NULL &&
             strstr(as_zed, "delegate further") == NULL &&
             strstr(as_zed, "delegate one further") == NULL);
    /* ...and the enforced sections still travel with it (M434). */
    JC_CHECK(as_zed != NULL && strstr(as_zed, "UNTRUSTED") != NULL);
    JC_CHECK(as_zed != NULL && strstr(as_zed, "Working directory") != NULL);
    /* No language given: no directive (a delegate's prose is the parent's). */
    JC_CHECK(as_zed != NULL && strstr(as_zed, "Respond in") == NULL);

    /* M597: a language reaches the sub prompt when the caller has one -- the
     * command-subtask path, whose answer goes to the user with no parent to
     * translate it. Persona and language compose. */
    {
        char *de = jc_sysmsg_build_sub_as(&app, "You are Zed.", "Deutsch");
        char *de_generic = jc_sysmsg_build_sub_as(&app, NULL, "Deutsch");
        JC_CHECK(de != NULL && strstr(de, "Respond in Deutsch") != NULL);
        JC_CHECK(de != NULL && strncmp(de, "You are Zed.", 12) == 0);
        JC_CHECK(de_generic != NULL &&
                 strstr(de_generic, "Respond in Deutsch") != NULL);
        JC_CHECK(de_generic != NULL &&
                 strstr(de_generic, "You are a focused sub-agent") != NULL);
    }

    jc_output_style_set_free(&app.output_styles);
    jc_skill_set_free(&app.skills);
    jc_arena_free(a);
}
