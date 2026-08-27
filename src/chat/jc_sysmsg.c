/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sysmsg.c - system prompt construction (see jc_sysmsg.h). */

#include "jc_toolcaps.h"
#include "jc_sysmsg.h"
#include "jc_snprintf.h"
#include "jc_untrusted.h"
#include "jc_constraint.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "jc_perm.h"
#include "jc_skill.h"
#include "jc_output_style.h"
#include "jc_compact.h"
#include "jc_envelope.h"
#include "jc_assign.h"
#include "jc_agent.h"    /* jc_subagent_can_spawn: one predicate for prompt + tool array */

#include <stdlib.h>
#include <time.h>
#include <string.h>

/* Fraction (percent) of the context window the two shrinkable sections (rules +
 * repo map) may use together; the rest is left for tool definitions, the
 * history, and the model's response. */
#define JC_SYSMSG_FIT_PCT 45

long jc_sysmsg_fit_budget(long limit_tokens, double cal,
                          long *held, long *held_limit)
{
    long raw = limit_tokens;
    long band;
    long drift;

    if (limit_tokens <= 0) {
        return limit_tokens;
    }
    if (cal > 1.0) {
        raw = (long)((double)limit_tokens / cal);
    }
    if (held == NULL || held_limit == NULL) {
        return raw; /* stateless: the plain deflation */
    }
    /* M365: the deadband. The M77 ratio moves a little on every model call,
     * and the M73 fit caps derive linearly from this value -- unheld, every
     * wobble moved the truncation byte of the rules/repo-map sections, so
     * the system prompt churned each turn and the M31 cached prefix (the
     * largest span in every request) re-billed forever, silently. A
     * stateless quantization was the first design and it only RELOCATES the
     * problem: a value sitting near a step boundary still flaps on tiny
     * jitter. So the previous budget is HELD until the raw value drifts a
     * full band away, then re-fits once and holds there. The band is 1/8 of
     * the limit capped at 1024 tokens (floor 64), so holding through a
     * drift-down can over-fit by at most that much -- bounded, and stated. */
    band = limit_tokens / 8;
    if (band > 1024) {
        band = 1024;
    }
    if (band < 64) {
        band = 64;
    }
    if (*held > 0 && *held_limit == limit_tokens) {
        drift = raw - *held;
        if (drift < 0) {
            drift = -drift;
        }
        if (drift < band) {
            return *held;
        }
    }
    *held = raw;
    *held_limit = limit_tokens;
    return raw;
}

void jc_sysmsg_fit_caps(long limit_tokens, jc_size rules_len, jc_size map_len,
                        jc_size design_len, jc_size *rules_cap,
                        jc_size *map_cap, jc_size *design_cap)
{
    jc_size budget;
    jc_size mcap;
    jc_size dcap;

    *rules_cap = 0;
    *map_cap = 0;
    *design_cap = 0;
    if (limit_tokens <= 0) {
        return;                 /* unknown budget: never shrink */
    }
    /* Clamp an absurd/huge limit so the byte math below can't overflow jc_size
     * (size_t) on a 32-bit/embedded target. 8M tokens is far beyond any real
     * model, so this never affects a legitimate budget. */
    if (limit_tokens > 8000000L) {
        limit_tokens = 8000000L;
    }

    /* ~4 bytes/token (the same heuristic compaction uses). */
    budget = (jc_size)limit_tokens * 4;
    budget = budget * JC_SYSMSG_FIT_PCT / 100;

    if (rules_len + map_len + design_len <= budget) {
        return;                 /* already fits: leave all three in full */
    }

    /* M462: three growable sections now compete, and the SACRIFICE ORDER is an
     * argument about what the model can recover on its own.
     *
     *   repo map  -- regenerable. Truncated, the agent can still call
     *                list_files/search_code and learn the layout. It costs tool
     *                calls, not correctness. Shrinks first.
     *   rules     -- project conventions: persistent, but not specific to this
     *                task, and usually restated in review. Shrinks second.
     *   design    -- the authoritative plan for the work IN FLIGHT. Truncate it
     *                and the agent follows half a plan while believing it has
     *                the whole one: the failure is silent and the output looks
     *                confident. So it is sacrificed LAST.
     *
     * The design keeps its own JC_DESIGN_MAX ceiling from load time; this can
     * only tighten it further, never loosen it. That asymmetry is deliberate --
     * see docs/DESIGN_INPUT.md. A purely dynamic cap would REGRESS, because an
     * undeclared context limit returns above with "never shrink", which is
     * exactly the configuration where an unbounded doc does the most damage. */
    dcap = design_len;          /* design is spared while anything else can give */
    mcap = budget / 3;
    if (mcap > map_len) {
        mcap = map_len;         /* don't reserve more than it needs */
    }
    if (budget > mcap + dcap) {
        *rules_cap = budget - mcap - dcap;
    } else {
        /* Rules squeezed to nothing: only now does the design give ground, and
         * it still keeps the largest share of what remains. */
        *rules_cap = 0;
        if (budget > mcap) {
            dcap = budget - mcap;
        } else {
            dcap = 0;
        }
    }

    /* A cap must never be left at 0 for a NON-empty section: append_capped
     * reads 0 as "no limit", which would let that section escape truncation at
     * a tiny budget -- exactly the starved case the fit exists for. */
    if (mcap == 0 && map_len > 0) {
        mcap = 1;
    }
    if (*rules_cap == 0 && rules_len > 0) {
        *rules_cap = 1;
    }
    if (dcap == 0 && design_len > 0) {
        dcap = 1;
    }
    *map_cap = mcap;
    *design_cap = dcap;
}

/* Append `s`, truncating to `cap` bytes (0 => no limit) with a one-line note
 * when cut. Keeps the head, which is usually the most important content. */
static void append_capped(struct jc_sb *sb, const char *s, jc_size cap,
                          const char *what)
{
    jc_size n;

    if (s == NULL) {
        return;
    }
    n = (jc_size)strlen(s);
    if (cap == 0 || n <= cap) {
        jc_sb_append(sb, s);
        return;
    }
    /* On a character boundary (M191): the system prompt reaches the wire too. */
    jc_sb_append_n(sb, s, jc_utf8_trunc_len(s, cap));
    jc_sb_append(sb, "\n[... ");
    jc_sb_append(sb, what);
    jc_sb_append(sb, " truncated to fit the model context window ...]\n");
}

/* C89 only requires support for string literals up to 509 chars, so the base
 * prompt is appended in several pieces, each comfortably under that limit. */
static const char *PROMPT_PART_1 =
    "You are jichi, a command-line AI coding agent. You help the user "
    "with software engineering tasks in their current project.\n\n"
    "You have access to tools for reading, writing, and editing files, "
    "listing directory contents, searching code, and running terminal "
    "commands. Use them to inspect the project and make precise changes.";

static const char *PROMPT_PART_2 =
    "\n\nGuidelines:\n"
    "- Prefer reading a file before editing it.\n"
    "- Make minimal, targeted edits that match the surrounding style.\n"
    "- When a task is complete, briefly state what you did.\n"
    "- If a tool fails, read the error and adapt rather than repeating the "
    "same call.";

/* M299: the craft. Two requested things that are one thing -- the ORDER the work is
 * done in, and the temperament it is done with.
 *
 * The order first, because an agent that opens with an edit has already skipped the
 * part where it could have been wrong cheaply. Analyse, ask when a reading is
 * genuinely ambiguous, write the design AND the decisions with their rejected
 * alternatives, then build, test, correct, refactor. The exception is a very short
 * program -- and even that carries design notes, because the cost of a paragraph is
 * nothing against the cost of not knowing why.
 *
 * Then the temperament, 職人気質 (shokunin kishitsu): devotion to the craft, honesty
 * about what is not known, and a peaceful heart. This is phrased as BEHAVIOUR, not
 * as vocabulary. Japanese words over unchanged conduct would be the opposite of the
 * value they name, so each line here asks for something an observer could check:
 * say what you do not know, name the alternative you rejected, prefer the honest
 * small answer to the impressive vague one. */
static const char *PROMPT_CRAFT_A =
    "\n\nHow to work (the craft):\n"
    "- Understand before changing: read the code, the constraints, and any rules "
    "or memory that apply. Measure rather than assume -- a number you have not "
    "checked is a guess wearing a number's clothes.\n"
    "- Ask when two readings of the task would lead to materially different "
    "work. Do not ask what the code can tell you.\n";

static const char *PROMPT_CRAFT_B =
    "- Design before implementing, and write the design down: what you will do, "
    "and the decisions -- including the alternatives you rejected and why. Even "
    "a very short program gets a short design note. Later you, or someone else, "
    "will need the why, and the code will only hold the what.\n"
    "- Then implement, test, correct, and refactor -- in that order, and expect "
    "to go round again. A first version that works is a draft.\n";

static const char *PROMPT_CRAFT_C =
    "- Prove a test can fail before trusting that it passes. A test never seen "
    "failing has never been seen working.\n"
    "- Be honest about what you did and did not do. Say plainly when something "
    "is unverified, partial, or skipped, and report a failure with its output "
    "rather than a summary of it. An admitted mistake is a lesson; a hidden one "
    "is a trap for whoever comes next.\n";

static const char *PROMPT_CRAFT_D =
    "- Work with care and without hurry. Prefer the small honest answer to the "
    "impressive vague one, leave the place tidier than you found it, and stay "
    "open to the better idea that arrives uninvited.";

/* Mode-specific addendum (kept under the C89 509-char literal limit). */
static const char *PROMPT_PLAN =
    "\n\nYou are in PLAN mode. Do not make any changes: mutating tools "
    "(writing/editing files, running commands) are disabled. Investigate the "
    "project with the read-only tools, then present a clear, step-by-step plan "
    "for the task and stop. Tell the user to switch to chat or auto mode (e.g. "
    "/plan off) to carry the plan out.";

static const char *PROMPT_AUTO =
    "\n\nYou are in AUTO mode: approved tool calls run without asking, bounded "
    "by an iteration budget. Work autonomously toward the goal, then summarise "
    "what you changed.";

/* Base prompt for a delegated subagent. The delegation sentence is NOT here: it
 * depends on the depth this subagent will run at, and stating it unconditionally
 * made it false -- see PROMPT_SUB_NEST / PROMPT_SUB_LEAF below. */
static const char *PROMPT_SUB =
    "You are a focused sub-agent inside jichi, working on a single task "
    "delegated by the main agent. You have tools for reading, editing, and "
    "searching the project and running commands. Complete the delegated task "
    "autonomously, then reply with a concise final answer that the main agent "
    "can use.";

/* M431: this prompt told EVERY subagent "You cannot delegate further (no
 * sub-agents of your own)" -- false at the default maxSubagentDepth of 2, where a
 * depth-1 subagent is advertised spawn_subagent (jc_agent.c builds its tool array
 * from jc_subagent_can_spawn, and a forked parallel child also runs at depth 1).
 * A prompt that denies a tool the model can see is worse than saying nothing: it
 * invites the model to distrust the rest of the prompt. Both branches now come
 * from the same predicate the tool array uses, so they cannot disagree. */
static const char *PROMPT_SUB_LEAF =
    " You cannot delegate further: you have no sub-agents of your own, so do "
    "this task yourself.";
static const char *PROMPT_SUB_NEST =
    " You may delegate one further sub-agent (spawn_subagent) if a self-"
    "contained part of this task would flood your context. Prefer doing the "
    "work yourself: each level costs a fresh model call and returns only prose.";

/* M87: guidance for AUTO runs with a verify gate. See jc_sysmsg.h. */
void jc_sysmsg_append_verify_gate(struct jc_sb *sb, int in_auto,
                                  const char *verify_cmd, int verify_every)
{
    if (!in_auto || verify_cmd == NULL || verify_cmd[0] == '\0') {
        return;
    }
    if (verify_every > 0) {
        char every[64];
        jc_snprintf(every, sizeof(every),
                    "` every %d tool call%s and reports the result to you. ",
                    verify_every, verify_every == 1 ? "" : "s");
        jc_sb_append(sb, "\n\nA verification gate automatically runs `");
        jc_sb_append(sb, verify_cmd);
        jc_sb_append(sb, every);
        jc_sb_append(sb,
            "Do NOT run it -- or any build/test command -- yourself with "
            "run_terminal_command: that only wastes the budget re-reading "
            "output the gate already gives you. Make your edits and rely on "
            "the gate's feedback; fix whatever it reports red.");
        return;
    }
    /* M543: the DEFAULT. No periodic verify is armed, so the gate runs once,
     * when the turn ends -- and its result decides whether the work is kept.
     * Saying "rely on the gate" here would be advice to work blind. */
    jc_sb_append(sb, "\n\nA verification gate runs `");
    jc_sb_append(sb, verify_cmd);
    jc_sb_append(sb,
        "` when your turn ENDS, and its result decides whether your work is "
        "kept. It does NOT run between your tool calls, so nothing will tell "
        "you about a red gate before then. If you need to know sooner, run it "
        "yourself with run_terminal_command -- ONCE, after a batch of edits, "
        "not after every edit: re-reading the same build output each iteration "
        "is the largest avoidable cost in a run.");
}

/* M332 DECLARE-THE-GATE: say that the verifier is a contract. See jc_sysmsg.h. */
void jc_sysmsg_append_gate_contract(struct jc_sb *sb, int on,
                                    const char *verify_cmd)
{
    if (!on || verify_cmd == NULL || verify_cmd[0] == '\0') {
        return;
    }
    jc_sb_append(sb,
        "\n\nThe verification command above, and any script or build file it "
        "names, are this run's CONTRACT -- they are how your work is judged, "
        "not part of the work. If the gate itself looks wrong (hardcoded to "
        "fail, referring to a path that does not exist, testing the wrong "
        "thing), say so in your final answer and leave it alone. Changing it "
        "would make a passing result mean nothing, and a run that does so is "
        "refused even if the gate then passes.");
}

void jc_sysmsg_append_scope_list(struct jc_sb *sb, const struct jc_vec *scope)
{
    jc_size i;

    if (sb == NULL || scope == NULL) {
        return;
    }
    for (i = 0; i < scope->len; i++) {
        const char *g = *(char **)jc_vec_at((struct jc_vec *)scope, i);
        if (g != NULL && g[0] != '\0') {
            jc_sb_append(sb, " ");
            jc_sb_append(sb, g);
        }
    }
}

void jc_sysmsg_append_scope_reach(struct jc_sb *sb, const struct jc_vec *scope)
{
    if (sb == NULL || scope == NULL || scope->len == 0) {
        return;
    }
    /* M431: name the globs. This paragraph opened "The edit scope above fences
     * the file tools" and there was no above -- the scope was rendered into the
     * system prompt nowhere, so the only way to learn which paths were writable
     * was to violate the fence and read the refusal. M333 added the list to that
     * refusal after one run guessed 177 times; stating it up front is the same
     * fact, delivered before the tokens are spent instead of after. Rendered by
     * the shared jc_sysmsg_append_scope_list, which the refusal also uses, so the
     * two lists cannot differ. */
    jc_sb_append(sb, "\n\nThis run has an --edit-scope: the only paths you may "
                     "write are:");
    jc_sysmsg_append_scope_list(sb, scope);
    /* Deterrent, not invitation: the shell CAN reach past the scope, so a
     * model steered toward "the edit tool refused, I'll use the shell" must
     * know the move is seen. Framed as detection, never as an escape route. */
    jc_sb_append(sb,
        ". That scope fences the file tools (edit_file, write_file, "
        "apply_patch); a run_terminal_command shell command can reach files "
        "outside it and is detected afterward, not prevented. Keep your writes "
        "inside the scope and use the file tools for them; if the task needs a "
        "path outside the scope, say so in your final answer rather than "
        "reaching around the fence with the shell.");
}

/* Render a byte cap in the unit a reader thinks in. KB because every cap is a
 * whole number of them; a "0.03 MB" would be precise and useless. */
static void cap_kb(struct jc_sb *sb, const char *label, jc_size cap)
{
    jc_sb_append_fmt(sb, "%s %lu KB", label, (unsigned long)(cap / 1024));
}

/* Split for the C89 509-char literal limit, like PROMPT_CRAFT_*. */
static const char *PROMPT_COST_A =
    "\n\n# Cost model\n\n"
    "Every tool result you receive stays in the conversation and is re-sent with "
    "every later request in this turn. Output you take once is therefore billed "
    "many times, and this run is not reusing a cached prompt prefix, so that "
    "applies to every byte.\n\n"
    "Your tool output is capped at: ";

static const char *PROMPT_COST_B =
    ". Output past a cap is truncated with a notice -- so a whole-file read of a "
    "large file spends the whole cap and still answers only partly.\n\n"
    "What follows from that:\n"
    "- Search before you read. Finding the lines first is what makes a bounded "
    "read possible; reading a file to find out whether it is relevant is the "
    "expensive way to ask a cheap question.\n";

static const char *PROMPT_COST_C =
    "- Read a range, not a whole file: name `offset` and `limit` when the file is "
    "large and your question is local.\n"
    "- Do not re-read what is already in this conversation. If a file was elided "
    "and you genuinely need it again, re-read the range you need.\n"
    "- Batch shell work. One command that does three things costs one round-trip; "
    "three commands cost three, each re-sending the whole conversation.\n";

void jc_sysmsg_append_cost_model(struct jc_sb *sb, int on,
                                 jc_size read_cap, jc_size run_cap,
                                 jc_size fetch_cap, jc_size search_cap,
                                 jc_size git_cap)
{
    if (sb == NULL || !on) {
        return;
    }
    jc_sb_append(sb, PROMPT_COST_A);
    /* The numbers, not adjectives. "Keep reads small" is unactionable; "read_file
     * 64 KB" lets the model predict a truncation before it pays for one. */
    cap_kb(sb, "read_file", read_cap);
    cap_kb(sb, ", run_terminal_command", run_cap);
    cap_kb(sb, ", fetch_url", fetch_cap);
    cap_kb(sb, ", search_code", search_cap);
    cap_kb(sb, ", the git tools", git_cap);
    jc_sb_append(sb, PROMPT_COST_B);
    jc_sb_append(sb, PROMPT_COST_C);
}

void jc_sysmsg_append_language(struct jc_sb *sb, const char *language)
{
    if (language == NULL || language[0] == '\0') {
        return;
    }
    jc_sb_append(sb, "\n\n# Language\n\nRespond in ");
    jc_sb_append(sb, language);
    jc_sb_append(sb,
        " unless the user writes in a different language or explicitly asks "
        "for another one. Code, identifiers, file paths, and command names "
        "stay as they are; translate only the surrounding prose.");
}

void jc_sysmsg_append_design(struct jc_sb *sb, const char *design, jc_size cap)
{
    if (design == NULL || design[0] == '\0') {
        return;
    }
    jc_sb_append(sb,
        "\n\n# Design specification (authoritative for this task)\n\n"
        "Treat the following as the authoritative plan for this task: follow its "
        "seam, reuse the code paths it names, and honor its listed pitfalls. If it "
        "conflicts with the actual code, SURFACE the conflict rather than silently "
        "diverging from either.\n\n");
    /* M462: capped like rules and the repo map. The load-time JC_DESIGN_MAX
     * ceiling still applies; this can only tighten it further. */
    append_capped(sb, design, cap, "design");
}

/* Append the shared environment block (cwd, platform). */
void jc_sysmsg_append_envelope(struct jc_sb *sb, const struct jc_envelope *e)
{
    char part[96];

    if (sb == NULL || e == NULL) {
        return;
    }
    if (e->budget_tokens <= 0.0 && e->deadline_secs <= 0 &&
        e->max_tool_calls <= 0 && e->max_reads <= 0) {
        return;
    }
    jc_sb_append(sb,
        "\n\nThis run is bounded; a budget stop ends it where it stands:\n");
    if (e->budget_tokens > 0.0) {
        jc_snprintf(part, sizeof(part), "- token budget: %.0f\n",
                    e->budget_tokens);
        jc_sb_append(sb, part);
    }
    if (e->deadline_secs > 0) {
        jc_snprintf(part, sizeof(part), "- wall-clock deadline: %ld seconds\n",
                    e->deadline_secs);
        jc_sb_append(sb, part);
    }
    if (e->max_tool_calls > 0) {
        jc_snprintf(part, sizeof(part), "- tool calls: %d\n",
                    e->max_tool_calls);
        jc_sb_append(sb, part);
    }
    if (e->max_reads > 0) {
        jc_snprintf(part, sizeof(part), "- read-tool calls: %d\n",
                    e->max_reads);
        jc_sb_append(sb, part);
    }
    jc_sb_append(sb,
        "Pace the work to land inside these limits: prefer finishing a "
        "smaller complete thing over starting a larger unfinished one. One "
        "[envelope] budget check arrives when ~80% of a limit is used.");
}

void jc_sysmsg_append_date(struct jc_sb *sb, long now)
{
    struct tm *tmv;
    time_t t = (time_t)now;
    char buf[32];

    if (sb == NULL) {
        return;
    }
    tmv = localtime(&t);
    if (tmv == NULL) {
        return;
    }
    if (strftime(buf, sizeof(buf), "%Y-%m-%d", tmv) == 0) {
        return;
    }
    jc_sb_append_fmt(sb, "- Today's date: %s\n", buf);
}

static void append_env(struct jc_sb *sb, struct jc_app *app)
{
    jc_sb_append(sb, "\n\nEnvironment:\n");
    jc_sb_append_fmt(sb, "- Working directory: %s\n", app->cwd);
    jc_sb_append(sb, "- Platform: POSIX\n");
    /* M352: both builders (main and subagent) pass through here, so both
     * prompts get the one line of clock. */
    jc_sysmsg_append_date(sb, (long)time(NULL));
    /* M358: the static half of the context gauge -- stated only when the
     * number is a configured fact, never the built-in default (the M355
     * armed-only rule). Both builders again: a subagent runs the same
     * model's window. Byte-stable within a model, so the M31 cached prefix
     * is unaffected. */
    jc_sysmsg_append_context_window(sb,
                                    jc_compact_context_limit_explicit(app));
}

void jc_sysmsg_append_context_window(struct jc_sb *sb, long limit)
{
    if (sb == NULL || limit <= 0) {
        return;
    }
    jc_sb_append_fmt(sb,
                     "- Context window: ~%ld tokens, managed -- near the "
                     "limit, older tool output in this conversation is "
                     "elided. Prefer read_file with offset/limit and "
                     "search_code over whole-file reads of big files.\n",
                     limit);
}

/* --- section accounting (M312) ----------------------------------------------
 *
 * `mark` charges everything appended since the last mark to one part. It is
 * called after every section, unconditionally -- including when the section
 * emitted nothing, which costs a zero-length charge and keeps the call sites
 * uniform. The sum of the parts equals the built length by construction; a
 * unit test asserts it, so a future section appended without a mark fails
 * loudly rather than silently swelling an unnamed remainder. */
static void mark(struct jc_sysmsg_parts *p, int part, struct jc_sb *sb,
                 jc_size *last)
{
    jc_size now = (sb->data != NULL) ? (jc_size)strlen(sb->data) : 0;
    if (p != NULL && part >= 0 && part < JC_SYSPART_COUNT && now >= *last) {
        p->bytes[part] += now - *last;
    }
    *last = now;
}

const char *jc_sysmsg_part_name(int part)
{
    static const char *const N[JC_SYSPART_COUNT] = {
        "persona", "craft", "safety", "cost model", "environment",
        "extra prompt",
        "language", "output style", "rules", "design", "constraints",
        "memory", "glossary", "board", "repo map", "assignment", "skills"
    };
    if (part < 0 || part >= JC_SYSPART_COUNT) {
        return "other";
    }
    return N[part];
}

char *jc_sysmsg_build(struct jc_app *app)
{
    return jc_sysmsg_build_parts(app, NULL);
}

char *jc_sysmsg_build_parts(struct jc_app *app, struct jc_sysmsg_parts *parts)
{
    struct jc_sb sb;
    char *result;
    jc_size rules_cap = 0;
    jc_size map_cap = 0;
    jc_size design_cap = 0;
    jc_size at = 0;             /* bytes accounted for so far (M312) */

    if (parts != NULL) {
        memset(parts, 0, sizeof(*parts));
    }

    /* Bound the two largest shrinkable sections so the system prompt can't
     * overflow a model whose context is smaller than rules + repo map + tools.
     * Honors the effective context budget (top-level `contextLimit`, else the
     * model's `contextLength`, else the default). No-op when they already fit.
     * fit_caps measures raw byte lengths, so DEFLATE the limit by the model's
     * calibration ratio (M77): capping the byte estimate to budget/ratio caps
     * the REAL token size to the intended fraction of the window. */
    {
        double cal = jc_compact_calibration(app);
        long lim = jc_sysmsg_fit_budget(jc_compact_context_limit(app), cal,
                                        &app->fit_held,
                                        &app->fit_held_limit);
        jc_sysmsg_fit_caps(lim,
                       (app->rules != NULL) ? (jc_size)strlen(app->rules) : 0,
                       (app->repo_map != NULL) ? (jc_size)strlen(app->repo_map)
                                               : 0,
                       (app->design != NULL) ? (jc_size)strlen(app->design) : 0,
                       &rules_cap, &map_cap, &design_cap);
    }

    jc_sb_init(&sb);
    /* A custom command's `agent:` profile takes over the persona for this turn
     * (replacing the default coding-agent persona so the profile is
     * authoritative, not merely appended); everything else below — env, mode,
     * rules, memory, repo map, skills — still applies. */
    if (app->persona_override != NULL && app->persona_override[0] != '\0') {
        jc_sb_append(&sb, app->persona_override);
        jc_sb_append(&sb, "\n");
    } else {
        jc_sb_append(&sb, PROMPT_PART_1);
        jc_sb_append(&sb, PROMPT_PART_2);
    }
    mark(parts, JC_SYSPART_PERSONA, &sb, &at);
    /* M299: appended even under a persona override, because HOW to work is not
     * the same thing as WHO is working -- a reviewer profile or a tutor profile
     * still analyses before it concludes and still says what it does not know.
     * Gated by config `craft` (default on) so a user who wants the terse older
     * prompt, or who is measuring token cost on a small window, can turn it off. */
    if (app->config.craft) {
        jc_sb_append(&sb, PROMPT_CRAFT_A);
        jc_sb_append(&sb, PROMPT_CRAFT_B);
        jc_sb_append(&sb, PROMPT_CRAFT_C);
        jc_sb_append(&sb, PROMPT_CRAFT_D);
    }
    mark(parts, JC_SYSPART_CRAFT, &sb, &at);
    /* M300: establish the untrusted-content convention ONCE, in the cached prefix
     * (M31), rather than arguing it per tool result. Unconditional -- unlike the
     * craft section this is a safety rule, and a user turning off prose guidance
     * must not silently turn off the injection warning too. */
    jc_sb_append(&sb, jc_untrusted_prompt_rule());
    mark(parts, JC_SYSPART_SAFETY, &sb, &at);
    /* M440: what the model's own output costs, and the four behaviours that follow
     * (docs/TOOL_OUTPUT_COST.md §6 items 5-8, which were measured advice addressed
     * to a human while jichi knew every input at runtime).
     *
     * Every input is CONFIG-derived, so this text is identical from turn to turn --
     * required, not incidental: the natural thing to state here is the observed
     * cache hit-rate, and stating it would change the cached prefix every turn and
     * destroy the caching it describes (M31). */
    jc_sysmsg_append_cost_model(&sb,
        jc_config_cost_model_on(app->config.cost_model,
                                app->config.model.prompt_cache != 0),
        jc_config_cap(app->config.read_max_bytes, JC_CAP_READ_DEFAULT),
        jc_config_cap(app->config.run_max_bytes, JC_CAP_RUN_DEFAULT),
        jc_config_cap(app->config.fetch_max_bytes, JC_CAP_FETCH_DEFAULT),
        jc_config_cap(app->config.search_max_bytes, JC_CAP_SEARCH_DEFAULT),
        jc_config_cap(app->config.git_max_bytes, JC_CAP_GIT_DEFAULT));
    mark(parts, JC_SYSPART_COST, &sb, &at);

    append_env(&sb, app);
    jc_sb_append_fmt(&sb, "- Mode: %s\n",
                     jc_agent_mode_name((enum jc_agent_mode)app->mode));
    if (app->readonly && app->mode != JC_MODE_PLAN) {
        jc_sb_append(&sb, "- Mutating tools are disabled (read-only).\n");
    }

    if (app->mode == JC_MODE_PLAN) {
        jc_sb_append(&sb, PROMPT_PLAN);
    } else if (app->mode == JC_MODE_AUTO) {
        jc_sb_append(&sb, PROMPT_AUTO);
    }

    /* M87: in an AUTO run with a verify gate, tell the model not to re-run the
     * gate command itself (the dominant no-prompt-cache cost). */
    jc_sysmsg_append_verify_gate(&sb, app->mode == JC_MODE_AUTO,
                                 (app->env != NULL) ? app->env->verify_cmd
                                                    : NULL,
                                 (app->env != NULL) ? app->env->verify_every
                                                    : 0);
    /* M355: the flight plan -- the armed limits, at takeoff. M347's gauge
     * rings at ~80%; a pilot briefed on the tank size at call 1 can pace the
     * whole run instead of scrambling at the bell. */
    jc_sysmsg_append_envelope(&sb, app->env);
    /* M332 DECLARE-THE-GATE: opt-in, and gated on the same flag as
     * REFUSE-THE-GREEN so the run is told the rule it will be judged by. A model
     * cannot otherwise distinguish "this harness is broken, fix it" from "this
     * gate is the contract" -- both justify the same edit (ANECDOTES #45). */
    jc_sysmsg_append_gate_contract(&sb,
                                   (app->env != NULL) && app->env->strict_green,
                                   (app->env != NULL) ? app->env->verify_cmd
                                                      : NULL);
    /* M387 STATE-THE-REACH: when an edit scope is armed, make its boundary
     * legible -- the scope fences the file tools; the shell reaches past it and
     * is only detected afterward (GATE_INTEGRITY.md §9). Inside the ENV slot, so
     * the section accounting is unchanged. */
    jc_sysmsg_append_scope_reach(&sb,
                                 (app->env != NULL) ? &app->env->edit_scope
                                                    : NULL);
    /* One slot for the whole "your situation" region: the user cannot shrink the
     * mode line separately from the environment block, so splitting them would
     * add lines to the report without adding a decision. */
    mark(parts, JC_SYSPART_ENV, &sb, &at);

    if (app->config.system_prompt_extra != NULL) {
        jc_sb_append(&sb, "\n");
        jc_sb_append(&sb, app->config.system_prompt_extra);
    }
    mark(parts, JC_SYSPART_EXTRA, &sb, &at);

    /* Answer language (M135): one stable directive, so the cached prefix
     * doesn't churn. Top-level only by design -- a subagent's answer is
     * consumed by the main agent, which follows this directive itself. */
    jc_sysmsg_append_language(&sb, app->config.language);
    mark(parts, JC_SYSPART_LANGUAGE, &sb, &at);

    /* Active output style (M28): an authoritative addendum governing response
     * tone/format/verbosity for the whole session. */
    {
        /* M302: a specialist's own tone wins over the session's, by NAME. The
         * precedence is session < profile/skill `style:` < a command `agent:`
         * BODY -- the body still being authoritative because it already replaces
         * the whole persona, and a style is an addendum to a persona rather than a
         * competitor. An override naming no existing style falls back to the
         * session's rather than silently emitting nothing; `doctor` reports the
         * dead name (the M285 lesson: a fence entry that matches nothing is worse
         * than no fence, because it looks like one). */
        const struct jc_output_style *os = NULL;
        if (app->style_override != NULL && app->style_override[0] != '\0') {
            os = jc_output_style_find(&app->output_styles, app->style_override);
        }
        if (os == NULL) {
            os = jc_output_style_active(&app->output_styles);
        }
        if (os != NULL && os->body != NULL && os->body[0] != '\0') {
            jc_sb_append(&sb, "\n\n# Output style\n\n");
            jc_sb_append(&sb, os->body);
        }
    }
    mark(parts, JC_SYSPART_STYLE, &sb, &at);

    /* Project/global instruction files (AGENTS.md etc.). */
    if (app->rules != NULL && app->rules[0] != '\0') {
        jc_sb_append(&sb, "\n");
        append_capped(&sb, app->rules, rules_cap, "instructions");
    }
    /* Charged AS CAPPED (M73), which is what the model gets -- the pre-M312
     * report measured the raw app->rules and so described text that was not in
     * the prompt whenever the cap bit. */
    mark(parts, JC_SYSPART_RULES, &sb, &at);

    /* Design specification (M-C): a --design/--spec doc, the authoritative plan
     * for this task. After project rules (which it operates within), before
     * memory/repo map so it's prominent. Pre-capped to JC_DESIGN_MAX at load. */
    jc_sysmsg_append_design(&sb, app->design, design_cap);
    mark(parts, JC_SYSPART_DESIGN, &sb, &at);

    /* Active constraints (M110): hard user-set limits, ENFORCED at the tool gate.
     * Injected into the system prompt (never compacted) so they survive a lost /
     * compacted context window -- the whole point is that the model can't "forget"
     * them. Placed prominently (right after rules/design). Text is canonical +
     * stable, so it doesn't churn the cached prefix. */
    if (app->constraints_on && app->n_constraints > 0) {
        jc_constraint_render(app->constraints, app->n_constraints, &sb);
    }
    mark(parts, JC_SYSPART_CONSTRAINTS, &sb, &at);

    /* Persistent agent memory (notes saved across sessions via the remember
     * tool). */
    if (app->memory != NULL && app->memory[0] != '\0') {
        jc_sb_append(&sb, "\n\n# Remembered notes\n\n"
                          "Durable notes from earlier sessions (use the "
                          "remember tool to add more):\n\n");
        jc_sb_append(&sb, app->memory);
    }
    mark(parts, JC_SYSPART_MEMORY, &sb, &at);

    /* Glossary: project/house domain terms, so the agent uses the right
     * vocabulary (reference, not instruction). */
    if (app->glossary != NULL && app->glossary[0] != '\0') {
        jc_sb_append(&sb, "\n\n# Glossary\n\n"
                          "Domain terms used in this project:\n\n");
        jc_sb_append(&sb, app->glossary);
    }
    mark(parts, JC_SYSPART_GLOSSARY, &sb, &at);

    /* Kanban board focus (#7): the active phase + in-progress cards, so the
     * agent stays on the current phase/task. Gated by config `board`. */
    if (app->config.board) {
        jc_board_render_focus(&app->board, &sb);
    }
    mark(parts, JC_SYSPART_BOARD, &sb, &at);

    /* Repository map: a compact index of source files + their top-level
     * symbols, so the agent knows the layout up front. */
    if (app->repo_map != NULL && app->repo_map[0] != '\0') {
        jc_sb_append(&sb, "\n\n");
        append_capped(&sb, app->repo_map, map_cap, "repository map");
    }
    mark(parts, JC_SYSPART_REPOMAP, &sb, &at);

    /* Optional SDLC assignment-authoring nudge (config `assignments`, default
     * off). When on, the agent is directed to produce structured assignment +
     * reference-solution files for software-development tasks. */
    /* M173b: the authoring stance below fires only when NO assignment is
     * active. With one active, the model is either the LEARNER (attempt: the
     * solving block) or the TUTOR (a human works it in the TUI: the tutor
     * block) -- and priming either of those to "write a reference solution"
     * is exactly wrong: it hands a learner the answer. */
    if (app->config.assignments && app->assignment == NULL) {
        jc_sb_append(&sb,
            "\n\n# Assignments mode\n"
            "When the user asks you to design, implement, review, test, or "
            "document something as a learning task, produce a structured "
            "*assignment* as a markdown file under docs/assignments/ "
            "(<slug>.md) for the relevant lifecycle phase.");
        jc_sb_append(&sb,
            " Cover: context, learning objectives, requirements, constraints, "
            "use cases, a suggested design (with mermaid diagrams), pseudo-code "
            "skeletons, algorithms/techniques to explore with research hints, "
            "the recommended toolchain, deliverables, and acceptance criteria.");
        jc_sb_append(&sb,
            " Then write a separate reference *solution* "
            "(docs/assignments/<slug>.solution.md) with a detailed explanation, "
            "trade-offs, complexity, and a test plan, so the user can compare "
            "their own work against it. Prefer the assignment-writer / "
            "solution-writer agent profiles when available (run `init "
            "assignments` to scaffold them).");
    }

    /* Solving mode: when an assignment is loaded as the active task (the solve/
     * attempt flow set app->assignment), guide the learner on the support tools
     * -- graded hints, ask-for-help, and delegation -- so hints are earned, not
     * dumped, and help is used only when genuinely stuck. */
    if (app->assignment != NULL && !app->assignment_tutor) {
        /* M319: name only the support tools that are ACTUALLY advertised. Under
         * `--tool-profile core` (which `auto` reaches by itself on a small window)
         * none of these three is sent, and the block was instructing the model to
         * call tools it did not have -- the M285 class of defect, one layer up:
         * a declared name that resolves to nothing.
         *
         * Resolved through the same jc_config_tool_profile_core call the agent
         * loop and every /context report use, so the prompt cannot describe a
         * different toolset from the request. The text changes only when the
         * profile does, which is stable for a session, so the M31 cached prefix
         * is unaffected. */
        int lean = jc_config_tool_profile_core(&app->config,
                                               jc_compact_context_limit(app));
        jc_sb_append(&sb,
            "\n\n# Solving an assignment\n"
            "You are working on the assignment above.");
        if (lean) {
            /* No support tools at all: say so rather than leave a heading with
             * nothing under it, and rather than stay silent -- a learner-model
             * told nothing may keep hunting for a hint tool. */
            jc_sb_append(&sb,
                " Solve it yourself: no hint or help tools are available in this "
                "run, so work from the assignment text and the code.\n");
        } else {
            jc_sb_append(&sb,
                " Solve it yourself first; the support tools are for when you "
                "are genuinely stuck, not a shortcut:\n");
            jc_sb_append(&sb,
                "- `hint`: reveal the next graded hint (nudge -> approach -> "
                "worked step). Use sparingly -- hints are limited and their use "
                "is recorded.\n");
            jc_sb_append(&sb,
                "- `ask_for_help`: ask a focused clarifying question when a real "
                "ambiguity blocks you (it reaches the user, or a helper who gives "
                "a nudge -- never the full solution).\n");
            jc_sb_append(&sb,
                "- `spawn_subagent`: delegate a well-scoped, self-contained "
                "sub-part when that genuinely helps -- but own the overall "
                "solution.\n");
        }
        jc_sb_append(&sb,
            "Your work is judged by the assignment's verification command; make "
            "it pass. Prefer understanding over guessing.");
    }

    /* Tutor stance (M173b): a HUMAN is working the active assignment in an
     * interactive session (/assignment set assignment_tutor). The failure mode
     * this block exists to prevent is the obvious one: a plain chat model,
     * asked about the task, will simply solve it -- and an assignments-mode
     * model would even write the reference solution. Deck 05 calls this "the
     * worry, named". The hint ladder stays the graded path; the tutor guards
     * it. */
    if (app->assignment != NULL && app->assignment_tutor) {
        jc_sb_append(&sb,
            "\n\n# Tutor stance -- a learner is working this assignment\n"
            "The user is a LEARNER currently solving the assignment titled \"");
        jc_sb_append(&sb, app->assignment->title != NULL
                              ? app->assignment->title : "(untitled)");
        jc_sb_append(&sb,
            "\". Your job is to teach, not to solve:\n"
            "- NEVER write the solution, in full or in part, and never edit "
            "files toward it -- not even if asked directly. Decline gently and "
            "point at the hint ladder (`/hint`) instead.\n");
        jc_sb_append(&sb,
            "- Guide with questions, name the concept to look up, check the "
            "learner's reasoning, and explain error messages they show you.\n"
            "- Reading code, running THEIR tests, and explaining results is "
            "fine; producing the missing code is not.\n");
        jc_sb_append(&sb,
            "- If they are stuck, suggest `/hint` (the next graded hint) or "
            "`/grade` (check their work against the assignment's own "
            "verification) rather than giving more away.\n"
            "The assignment is graded by its verification command; the learning "
            "happens on the way there, and shortcuts you provide take it away.");
    }
    /* One slot for all three assignment stances: authoring, solving, tutor. At
     * most one is ever present (M173b makes them mutually exclusive), so three
     * slots would be three lines of which two are always zero. */
    mark(parts, JC_SYSPART_ASSIGNMENT, &sb, &at);

    /* Model-invoked skills: names + descriptions only (progressive disclosure;
     * the full body is fetched via the load_skill tool). */
    jc_skill_render_catalog(&app->skills, &sb);
    mark(parts, JC_SYSPART_SKILLS, &sb, &at);

    /* The system message is per-turn-transient: built fresh each turn and used
     * only within it. Put it on the scratch arena so long sessions don't pile
     * up one copy per turn on the session arena (M20a). */
    result = jc_arena_strdup(jc_app_scratch(app), sb.data != NULL ? sb.data : "");
    if (parts != NULL) {
        parts->total = (sb.data != NULL) ? (jc_size)strlen(sb.data) : 0;
    }
    jc_sb_free(&sb);
    return result;
}

char *jc_sysmsg_build_sub_as(struct jc_app *app, const char *persona,
                             const char *language)
{
    struct jc_sb sb;
    char *result;

    jc_sb_init(&sb);
    if (persona != NULL && persona[0] != '\0') {
        /* M596: a persona -- a command's `agent:` profile, or a delegate's -- IS
         * the identity paragraph, so it replaces PROMPT_SUB rather than sitting
         * beside a second "you are" sentence. The delegation clause is omitted
         * with it: a profile may fence spawn_subagent away, and a clause that
         * advertises a tool the array does not offer is the M431 defect
         * mirrored. The tool array is the truth about delegation; the enforced
         * sections below still follow, exactly as for the generic identity.
         *
         * Before M596 the command-subtask path never read the persona at all
         * (jc_agent_run_command_subtask built with the generic prompt), so the
         * scaffolded mentor -- whose prompt carries the FORMAT IS STRICT block
         * that jc_learn_parse_draft depends on -- had never once received its
         * instructions. tests/smoke/subtask_persona.sh reads the captured
         * request to prove they now arrive. */
        jc_sb_append(&sb, persona);
    } else {
        jc_sb_append(&sb, PROMPT_SUB);
        /* app->agent_depth is still the PARENT's depth here (spawn_subagent
         * builds the prompt before it increments), so the depth this subagent
         * will run at is one deeper -- the value jc_agent_run_subagent then
         * passes to the very same predicate when it decides whether to
         * advertise spawn_subagent. */
        jc_sb_append(&sb,
                     jc_subagent_can_spawn(app->agent_depth + 1,
                                           app->config.max_subagent_depth)
                         ? PROMPT_SUB_NEST : PROMPT_SUB_LEAF);
    }
    append_env(&sb, app);

    /* M434 -- ENFORCED IMPLIES STATED. Everything below is a rule jichi applies to a
     * subagent MECHANICALLY, at any depth, and until now told it none of them: the
     * prompt was PROMPT_SUB + env + extra and nothing else, so a delegate was fenced
     * by rules it had never seen. `env_scope_fence` holds at any depth (M133), the
     * constraint gate has no depth check at all, and M431 made the budget hold at
     * any depth too.
     *
     * The line is drawn at ENFORCEMENT, which is why this is not simply "give the
     * subagent the main prompt": the repo map and the skills catalogue are context a
     * delegate does not need (the parent digested one, and a skill is handed over
     * explicitly), and copying them would defeat the context isolation that is the
     * reason to delegate at all. A lint holds the line -- see
     * tests/smoke/sub_prompt_lint.sh. */

    /* The untrusted-content convention. NOT an efficiency matter and therefore
     * unconditional, exactly as in the main prompt (M300): a subagent that fetches a
     * URL otherwise has no statement that fetched content is data rather than
     * instructions. This was the most serious of the omissions. */
    jc_sb_append(&sb, "\n\n");
    jc_sb_append(&sb, jc_untrusted_prompt_rule());

    /* Active constraints: refused at the tool gate for a subagent exactly as for the
     * parent, and ANECDOTES #27 is what a model does when blocked by a rule it cannot
     * see -- 64 tool calls and a whole 1.5M budget spent trying to comply with
     * something it was never told. */
    if (app->constraints_on && app->n_constraints > 0) {
        jc_constraint_render(app->constraints, app->n_constraints, &sb);
    }

    /* The edit scope, with its globs. A subagent's writes are fenced by M133 at any
     * depth, and before M431 the globs reached no prompt at all -- which cost one run
     * 177 guesses. A delegate could not even read the refusal to learn them, because
     * its task text is written by the parent. */
    jc_sysmsg_append_scope_reach(&sb,
                                 (app->env != NULL) ? &app->env->edit_scope
                                                    : NULL);

    if (app->config.system_prompt_extra != NULL) {
        jc_sb_append(&sb, "\n");
        jc_sb_append(&sb, app->config.system_prompt_extra);
    }
    /* M597: the answer language, when the caller has one to state. A spawned
     * delegate passes NULL -- its prose is consumed by the parent, which follows
     * the session directive itself (the M135 reasoning, which was right for
     * delegates). A command subtask passes the resolved language, because its
     * answer streams to the user or is written to disk with no parent in
     * between: until M597 the mentor of a German self-learner drafted English
     * lessons, and LANGUAGE.md claimed otherwise. */
    jc_sysmsg_append_language(&sb, language);
    /* Transient like the top-level system message (M20a): scratch arena. */
    result = jc_arena_strdup(jc_app_scratch(app), sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return result;
}

/* The generic delegate prompt: jc_sysmsg_build_sub_as with no persona. Kept as
 * a one-line wrapper so the enforced sections have exactly one body --
 * tests/smoke/sub_prompt_lint.sh reads that body and checks this delegates. */
char *jc_sysmsg_build_sub(struct jc_app *app)
{
    return jc_sysmsg_build_sub_as(app, NULL, NULL);
}
