/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sysmsg.h - build the system prompt for a turn.
 *
 * Assembles a base agent prompt plus environment context (cwd, platform) and
 * any operator-supplied extra text from the config. Returned string is
 * allocated from the arena.
 */
#ifndef JC_SYSMSG_H
#define JC_SYSMSG_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_app.h"
#include "jc_str.h"

char *jc_sysmsg_build(struct jc_app *app);

/* --- where the system prompt's bytes went (M312) ----------------------------
 *
 * The system prompt is the largest single component of a call (~15,200 tokens of
 * an 18,300-token prefix on jichi's own tree; ~3,952 of a graded `attempt`'s
 * ~4,900 even with rules skipped) and used to be the one component /context
 * could not explain -- six hand-measured sub-parts against a stated total, with
 * "the base persona + section headers" as an unnamed remainder. On a graded
 * attempt every named part is zero and the remainder is the whole thing.
 *
 * So the BUILDER reports what it built. These counts are the same bytes the
 * model receives -- including the M73 fit caps, which the old line missed by
 * measuring the raw `app->rules` rather than the capped text actually emitted.
 *
 * What keeps it true, stated at its real strength -- two checks in
 * tests/test_sysmsg.c, neither of which is total:
 *
 *   1. the parts SUM to `total`. This catches a section appended after the LAST
 *      mark. It does NOT catch one inserted between two marks: those bytes are
 *      charged to the following slot and the sum still balances (verified by
 *      trying it, which is why this paragraph is not the one originally written
 *      here).
 *   2. in a minimal configuration, the thirteen optional slots must be EXACTLY
 *      zero. This is what closes most of gap 1: an unmarked append anywhere
 *      upstream of an inactive slot lands in it and turns a 0 positive.
 *
 * Uncaught: an insertion between two *active* slots, credited to a neighbour. A
 * misattribution rather than an unexplained remainder -- a smaller error than the
 * one this replaced, but not nothing. Prefer a lint to an audit, and prefer
 * knowing what the lint does not cover. */
enum jc_sysmsg_part {
    JC_SYSPART_PERSONA = 0,  /* base persona, or a command's `agent:` override  */
    JC_SYSPART_CRAFT,        /* M299 "How to work" (config `craft`)             */
    JC_SYSPART_SAFETY,       /* M300 untrusted-content rule (unconditional)     */
    JC_SYSPART_COST,         /* M440 "Cost model" (config `costModel`)          */
    JC_SYSPART_ENV,          /* env + mode + read-only + plan/auto + verify gate */
    JC_SYSPART_EXTRA,        /* config `systemPrompt` free text                 */
    JC_SYSPART_LANGUAGE,     /* M135 answer language                            */
    JC_SYSPART_STYLE,        /* active output style body                        */
    JC_SYSPART_RULES,        /* AGENTS.md/CLAUDE.md, as capped                  */
    JC_SYSPART_DESIGN,       /* --design/--spec doc                             */
    JC_SYSPART_CONSTRAINTS,  /* M110 enforced constraints                       */
    JC_SYSPART_MEMORY,       /* remembered notes                                */
    JC_SYSPART_GLOSSARY,     /* domain terms                                    */
    JC_SYSPART_BOARD,        /* kanban focus                                    */
    JC_SYSPART_REPOMAP,      /* repository map, as capped                       */
    JC_SYSPART_ASSIGNMENT,   /* authoring nudge | solving block | tutor stance   */
    JC_SYSPART_SKILLS,       /* skills catalog (names + descriptions)           */
    JC_SYSPART_COUNT
};

struct jc_sysmsg_parts {
    jc_size bytes[JC_SYSPART_COUNT];
    jc_size total;           /* == the built prompt's strlen; == sum(bytes)     */
};

/* Human label for a part ("rules", "repo map", ...). Static string, never NULL
 * (an out-of-range index yields "other"). Pure. */
const char *jc_sysmsg_part_name(int part);

/* jc_sysmsg_build, additionally reporting the per-section byte counts when
 * `parts` is non-NULL (`jc_sysmsg_build(app)` is exactly this with NULL). The
 * ENV slot deliberately absorbs the environment block, mode line, read-only
 * note, plan/auto addendum and verify-gate note: one contiguous "your situation"
 * region, none of it separately shrinkable by the user. ASSIGNMENT likewise
 * covers the three assignment stances, of which at most one is ever present. */
char *jc_sysmsg_build_parts(struct jc_app *app, struct jc_sysmsg_parts *parts);

/* System prompt for a delegated subagent: a focused-task framing plus the same
 * environment context and every rule jichi enforces at depth (M434).
 * Arena-allocated. */
char *jc_sysmsg_build_sub(struct jc_app *app);

/* The same prompt with `persona` -- a command's `agent:` profile body or a
 * delegate's -- as the identity paragraph in place of the generic one (M596).
 * A NULL/empty persona is exactly jc_sysmsg_build_sub. With a persona the
 * delegation clause is omitted (a profile may fence spawn_subagent away, and a
 * clause naming a tool the array does not offer is the M431 defect mirrored);
 * the environment, the untrusted-content rule, the constraints and the edit
 * scope are appended regardless, so "enforced implies stated" holds for a
 * persona'd delegate too. This is what jc_agent_run_command_subtask and the
 * profiled paths of spawn_subagent / spawn_parallel / ask_for_help build with:
 * before M596 the command path ignored the persona and the delegate paths used
 * the bare profile text, dropping the enforced sections.
 *
 * `language` (M597): the answer-language directive, appended when non-NULL --
 * a command subtask passes its resolved language (frontmatter `language:`,
 * else config `language`) because its answer reaches the user directly; a
 * spawned delegate passes NULL, since the parent that consumes its prose
 * follows the session directive itself. */
char *jc_sysmsg_build_sub_as(struct jc_app *app, const char *persona,
                             const char *language);

/* Compute byte caps for the two largest shrinkable system-prompt sections
 * (the instruction files `rules_len` and the repo map `map_len`) so their
 * combined size fits a fraction of the context window `limit_tokens`. A cap of
 * 0 means "emit in full" — both caps are 0 when the sections already fit (so
 * the common case is unchanged) or when `limit_tokens <= 0` (unknown budget).
 * The repo map (regenerable) is capped first; rules (project conventions) keep
 * the remainder. This is what keeps the system prompt from overflowing a model
 * whose context is smaller than rules + repo map + tools. Pure; unit-tested. */
/* M365: the fit budget -- the effective limit deflated by the calibration
 * ratio, HELD through jitter by a deadband so the M73 fit-cap truncation
 * byte cannot move every turn (which churned the M31 cached prefix,
 * silently re-billing the largest span of every request). `held`/
 * `held_limit` carry the state (jc_app fields); the previous budget is
 * returned until the raw deflation drifts a full band (limit/8, capped at
 * 1024 tokens, floor 64) from it, then re-fits once. NULL state = the plain
 * stateless deflation. Pure given the two state longs. */
long jc_sysmsg_fit_budget(long limit_tokens, double cal,
                          long *held, long *held_limit);

void jc_sysmsg_fit_caps(long limit_tokens, jc_size rules_len, jc_size map_len,
                        jc_size design_len, jc_size *rules_cap,
                        jc_size *map_cap, jc_size *design_cap);

/* M87: append AUTO-mode guidance about the verification gate. No-op unless
 * `in_auto` is set and `verify_cmd` is non-empty. Pure (writes to `sb`);
 * unit-tested.
 *
 * M543: `verify_every` is now an ARGUMENT, and the text changes with it, because
 * the old wording was true in one configuration and false in the default one. It
 * said the gate "runs after your tool calls" and told the model not to run the
 * build or tests itself -- but the mid-turn verify at jc_agent.c's periodic gate
 * is guarded by jc_env_should_verify_now, which returns 0 when `verify_every <= 0`,
 * and that is the DEFAULT (`--verify-every` unset). So an ordinary `--auto --verify`
 * run told the model to expect feedback that would not arrive until the turn was
 * already over, and forbade it from checking its own work in the meantime. The
 * model edited blind, on instruction.
 *
 * M87's cost argument still holds where the gate really is periodic: re-reading
 * build output every iteration is the dominant spend on a no-prompt-cache backend.
 * It just does not license the advice when nothing is running the gate. */
void jc_sysmsg_append_verify_gate(struct jc_sb *sb, int in_auto,
                                  const char *verify_cmd, int verify_every);

/* M135: append the "# Language" section directing the model to answer in the
 * user's natural language (config `language` / --language / TUI /language).
 * The value is free-form ("Japanese", "Deutsch", "zh") and passed verbatim;
 * code, identifiers, and command names are told to stay as-is. No-op when
 * `language` is NULL/empty. Pure (writes to `sb`); unit-tested. */
/* M332 (DECLARE-THE-GATE): append one paragraph declaring the verify command --
 * and any script it names -- to be the run's contract rather than part of the
 * work, and stating that a run which changes it is refused. Appends nothing when
 * `on` is 0 or `verify_cmd` is empty.
 *
 * Opt-in, because it costs tokens on every model call and because its companion
 * (REFUSE-THE-GREEN) changes an exit code. It is the weaker half of the pair on
 * purpose: prose in a prompt is advice a model may or may not act on
 * (docs/ANECDOTES.md #41), so it ships BESIDE the check, never instead of it.
 * Pure; unit-tested. */
void jc_sysmsg_append_gate_contract(struct jc_sb *sb, int on,
                                    const char *verify_cmd);

void jc_sysmsg_append_language(struct jc_sb *sb, const char *language);

/* M387 (STATE-THE-REACH): when an edit scope is armed, append one line telling
 * the model that the scope fences the file tools and a shell command reaches
 * past it and is DETECTED afterward. Deterrent framing, never an invitation:
 * for a steered model it prevents the accidental "the edit tool refused, I'll
 * use the shell" move (ANECDOTES #45); for an adversarial one it changes
 * nothing. It makes the boundary legible; it does NOT close the escape (that is
 * OS isolation, GATE_INTEGRITY.md §9.3-B), so it never claims to.
 *
 * M431: takes the scope itself rather than a have-it flag, and NAMES the globs.
 * The paragraph opened "The edit scope above fences the file tools" while nothing
 * above named it, so a model could only learn its writable paths by violating the
 * fence and reading the refusal. Appends nothing when `scope` is NULL or empty.
 * Pure; unit-tested. */
void jc_sysmsg_append_scope_reach(struct jc_sb *sb, const struct jc_vec *scope);

/* M440: the `# Cost model` section -- what the model's output costs, and the four
 * behaviours that follow. Pure, so the section is unit-testable and, more to the
 * point, PROVABLY prefix-stable: every input is config-derived, so the text cannot
 * change from turn to turn. That is not a nicety. The natural thing to say here is
 * the observed cache hit-rate, and saying it would change the cached prefix on
 * every turn and destroy the caching it describes (M31).
 *
 * The caps are the EFFECTIVE ones (jc_config_cap over jc_toolcaps.h's defaults),
 * so a `--lite` or hand-tightened run reports its real numbers rather than the
 * built-in ones. Emits nothing when `on` is 0.
 *
 * The four rules are docs/TOOL_OUTPUT_COST.md §6 items 5-8, which were measured
 * advice addressed to a HUMAN -- to be copied by hand into an AGENTS.md -- while
 * jichi knew every input at runtime. This does NOT re-open §7's rejection of
 * auto-bounding reads: it informs the model's decision and never narrows the
 * answer to an explicit request. */
void jc_sysmsg_append_cost_model(struct jc_sb *sb, int on,
                                 jc_size read_cap, jc_size run_cap,
                                 jc_size fetch_cap, jc_size search_cap,
                                 jc_size git_cap);

/* M431: append the edit-scope globs as " a b c". Shared by the system prompt
 * (above) and the out-of-scope refusal in jc_agent.c, so the list a model is
 * shown up front is the same list it is shown when a write is refused. Appends
 * nothing for a NULL/empty scope. Pure; unit-tested. */
void jc_sysmsg_append_scope_list(struct jc_sb *sb, const struct jc_vec *scope);

/* M355: the flight plan -- the armed envelope limits, stated at takeoff. M347
 * rings the fuel gauge at ~80%, but a model that only learns the tank size at
 * 80% can only scramble; told "30 tool calls, 500k tokens" at call 1 it can
 * pace the whole run (finding 14's budget deaths were sized in calls the
 * model could have planned against from the start). Armed budgets only, the
 * M347 rule; appends nothing when `e` is NULL or nothing is armed. Top-level
 * prompt only, like the notice itself -- a subagent's iteration budget is the
 * M62 taper, not the run envelope. Pure; unit-tested. */
struct jc_envelope;
void jc_sysmsg_append_envelope(struct jc_sb *sb, const struct jc_envelope *e);

/* M352: one line of clock. The model has no other way to know the date, and a
 * model that is not told does not leave dates blank -- it guesses from its
 * training priors, in a project whose registers, analysis pages and memory
 * practice are all dated. ISO `YYYY-MM-DD` only: %Y/%m/%d are numeric and
 * immune to the LC_TIME the display layer deliberately localizes (main.c sets
 * LC_TIME from the user's locale for timeFormat), so the canonical prompt
 * never varies by locale. Local time, because register dates are local dates.
 * Costs one prompt-cache bust per day -- the M31 prefix stays byte-stable
 * within a day, which is the stability that matters. Takes the timestamp as a
 * parameter so tests pin it; callers pass time(NULL). Unit-tested. */
void jc_sysmsg_append_date(struct jc_sb *sb, long now);

/* M358: the static half of the context gauge -- one Environment line stating
 * the configured context window + the reading habit that fits it. No-op when
 * limit <= 0 (i.e. when only the built-in default would apply: a number
 * nobody set is not a fact about this model). */
void jc_sysmsg_append_context_window(struct jc_sb *sb, long limit);

/* M-C: append the "# Design specification (authoritative for this task)" section
 * -- a --design/--spec doc the agent is told to treat as the authoritative plan
 * (follow its seam, reuse its named paths, honor its pitfalls; surface conflicts
 * with the code rather than diverging). No-op when `design` is NULL/empty. Pure
 * (writes to `sb`); unit-tested. `design` is expected pre-capped (JC_DESIGN_MAX). */
void jc_sysmsg_append_design(struct jc_sb *sb, const char *design,
                             jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_SYSMSG_H */
