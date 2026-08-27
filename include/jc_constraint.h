/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_constraint.h - user-stated constraints, captured and ENFORCED (M110).
 *
 * The problem: a user (or a driving agent) tells jichi "do not run the build, nor
 * the tests", but after a lost/compacted context window the model forgets and
 * runs them anyway. Advice in a prompt is not enough. So a constraint is:
 *   (1) injected into the SYSTEM PROMPT (never compacted -> re-sent every turn), and
 *   (2) mapped to the tool-call gate so a forbidden call is MECHANICALLY REFUSED
 *       even if the model forgets.
 *
 * This module is the pure core: scan a message for constraints, decide whether a
 * tool call violates one, render the prompt block, and parse/serialize the durable
 * store (.jichi/constraints.md). No I/O; unit-tested.
 */
#ifndef JC_CONSTRAINT_H
#define JC_CONSTRAINT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_str.h"

enum jc_constraint_kind {
    JC_CONSTRAINT_DENY_TOOL, /* forbid a named tool (subject = tool name)        */
    JC_CONSTRAINT_DENY_CMD,  /* forbid shell commands matching a key (subject)   */
    JC_CONSTRAINT_READ_ONLY, /* forbid every mutating tool + shell mutation      */
    JC_CONSTRAINT_NOTE       /* advisory only: injected into the prompt, not      */
                             /*   enforced (a free-form "rule" the user stated)   */
};

/* Where a constraint came from -- which decides whether it OUTLIVES the session.
 *
 * M169. A constraint the operator wrote is a policy; one guessed from a sentence
 * in a prompt is a guess, and a guess must not govern every future run in a
 * directory. Three misparses in a single day made the case: "do not change the
 * test file" became "do not run tests" (M167d) and "Oracle files (read-only, ...)"
 * became a global read-only (M168c). Each was narrowed, but a keyword scanner over
 * natural language will keep producing them -- so the durable mitigation is to cap
 * the blast radius rather than to keep tightening keywords. AUTHORED persists to
 * .jichi/constraints.md; INFERRED is enforced just as hard, for this session only.
 *
 * AUTHORED is 0 so a zeroed/parsed constraint defaults to persisting -- the store
 * on disk is by definition authored. */
enum jc_constraint_origin {
    JC_CONSTRAINT_AUTHORED = 0, /* the store, or an explicit `/constraints add` */
    JC_CONSTRAINT_INFERRED = 1  /* scanned out of a prompt; session-scoped      */
};

struct jc_constraint {
    enum jc_constraint_kind kind;
    char *subject; /* tool name (DENY_TOOL) or canonical cmd key (DENY_CMD); else NULL */
    char *text;    /* human phrasing for the prompt + display (arena-owned)            */
    enum jc_constraint_origin origin; /* M169: persisted vs session-scoped           */
};

#define JC_CONSTRAINT_MAX 64

/* Scan a user message for constraint statements (a negation cue + a known target,
 * e.g. "do not run the build, nor the tests"). Appends the recognized constraints
 * to out[] (up to `max`; strings arena-allocated via `a`) and returns the count
 * appended. Conservative: skips conversational "don't you/we ..." and requires an
 * explicit negation. Pure; unit-tested. */
int jc_constraint_scan(const char *msg, struct jc_constraint *out, int max,
                       struct jc_arena *a);

/* M433: which LINE of `msg` produced the constraint at cs[i]?
 *
 * `jc_constraint_scan` returns what a run would infer, and its `text` is a canonical
 * phrasing rather than the operator's sentence -- so a pre-flight that only listed
 * the constraints would leave the author hunting for the words responsible. That is
 * the whole difficulty: the phrases that trigger inference are DESCRIPTIONS, not
 * instructions ("force never-compiled core code to compile", "47 of 88 files never
 * compiled"), so they do not look like constraints when you read them.
 *
 * Attribution is by RE-SCANNING each line alone and matching kind+subject, which
 * needs no change to the enforcement path. Returns the 1-based line number, or 0
 * when no single line reproduces the finding -- an honest outcome, since a scan over
 * the whole message can match across a line break, and claiming a wrong line would
 * be worse than admitting none. Pure; unit-tested. */
int jc_constraint_source_line(const char *msg, const struct jc_constraint *c,
                              struct jc_arena *a);

/* Does a shell `command` violate a DENY_CMD whose subject is the canonical `key`
 * (build/test/commit/push/deploy/install)? Word/synonym match, case-insensitive.
 * 0 for NULL/unknown key. Pure; unit-tested. */
int jc_constraint_cmd_hits(const char *key, const char *command);

/* The enforcement predicate. Given the active constraints, a tool name, the shell
 * command (or NULL if the tool isn't a shell tool), and whether the tool is
 * read-only, write a human-readable block reason into `reason` (cap) and return 1
 * if the call MUST be refused, else 0. Pure; unit-tested. */
/* As jc_constraint_blocks, but the caller may state that an OPERATOR DECLARATION
 * -- an explicit `--edit-scope` / `editScope` -- already permits writing the exact
 * path this call targets (`explicit_write_allowed`).
 *
 * When it does, an **INFERRED** JC_CONSTRAINT_READ_ONLY does not block. Nothing
 * else changes: an AUTHORED read-only still binds, and DENY_TOOL / DENY_CMD bind
 * regardless of origin.
 *
 * Why the origin decides it. This header already says an AUTHORED constraint is a
 * policy the operator wrote and an INFERRED one is "a guess" scanned out of prose,
 * and caps the guess's blast radius for exactly that reason. Before M459 the guess
 * outranked a typed flag: a run given `--edit-scope docs/PROBE.md` inferred
 * read-only from the same request's wording and refused `write_file` on that very
 * file, spending 250,288 tokens and 11 model calls to make 1 tool call before
 * ending starved with no changes (probe P3, M424). Both declarations sat in one
 * journal, disagreeing. The shape is the commonest useful one -- "work read-only
 * and write your findings to <file>", a read-only analysis whose only deliverable
 * is a written report -- so this is not an exotic conflict.
 *
 * The exemption is deliberately per-PATH, not per-run: a write outside the declared
 * scope is still refused by the same constraint, so this narrows a guess rather than
 * disarming it. Callers must announce when it fires -- silently ignoring a
 * constraint is the same class of defect as silently enforcing one. Pure;
 * unit-tested. */
int jc_constraint_blocks_ex(const struct jc_constraint *cs, int n,
                            const char *tool_name, const char *command,
                            int tool_readonly, int explicit_write_allowed,
                            char *reason, jc_size cap);

int jc_constraint_blocks(const struct jc_constraint *cs, int n,
                         const char *tool_name, const char *command,
                         int tool_readonly, char *reason, jc_size cap);

/* Render the active constraints as a stable system-prompt block into `sb`
 * (a "# Active constraints" section the model sees every turn). Cache-friendly
 * (stable across turns). No-op when n == 0. Pure. */
void jc_constraint_render(const struct jc_constraint *cs, int n,
                          struct jc_sb *sb);

/* Parse a constraints store (the lines of .jichi/constraints.md) into out[] (up to
 * `max`; arena strings); returns the count. Accepts directive lines:
 *   deny-tool <name>[; text]   deny-cmd <key>[; text]   read-only[; text]
 *   note <text>            (and tolerates '- ' bullets + '#'/blank lines)
 * Pure; unit-tested. */
int jc_constraint_parse(const char *text, struct jc_constraint *out, int max,
                        struct jc_arena *a);

/* Serialize constraints back to the store format (inverse of parse) into `sb`. */
void jc_constraint_serialize(const struct jc_constraint *cs, int n,
                             struct jc_sb *sb);

/* True if cs[0..n) already holds a constraint equal to *c (same kind + subject).
 * Used to dedupe on add. Pure. */
int jc_constraint_has(const struct jc_constraint *cs, int n,
                      const struct jc_constraint *c);

/* Comma-join the human text of cs[from..n) into `buf` (always NUL-terminated,
 * truncated with a trailing "..." if it does not fit). Pure.
 *
 * M167: an enforced constraint the operator did not type -- inferred from a
 * prompt, or inherited from a persisted .jichi/constraints.md -- must be *named*
 * where it is announced. Reporting only a count ("adopted 2") is what let a
 * misparsed "do not change the test file" silently ban the test suite for every
 * later run in a directory. */
void jc_constraint_join_text(const struct jc_constraint *cs, int from, int n,
                             char *buf, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONSTRAINT_H */
