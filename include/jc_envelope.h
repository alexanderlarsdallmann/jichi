/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_envelope.h - the autonomy envelope: budgets, edit-scope, a verification
 * gate with automatic rollback, and a structured audit journal that together
 * bound an unsupervised (`--auto`) run. See docs/AUTONOMY.md.
 *
 * The agent works inside the declared limits; when it produces a final answer
 * the configured verifier (build + test) must pass. If it cannot be made to
 * pass within the retry budget -- or a budget is exhausted -- the workspace is
 * rolled back to the last known-good (green) checkpoint and the run ends with a
 * clear outcome. Every decision is appended to a JSONL journal.
 *
 * The parse / glob / scope / budget helpers are pure and unit-tested; the
 * verifier runner, journal, and rollback are verified end-to-end.
 */
#ifndef JC_ENVELOPE_H
#define JC_ENVELOPE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_json.h" /* cJSON for the journal */

#include <stdio.h>

/* M89: max length of a verify-failure signature (the first `error:` line), used
 * to detect a run stuck re-hitting the same compile error. */
#define JC_ENV_SIG_MAX 160

/* How the run finished, read by the CLI to set the exit code. */
enum jc_env_outcome {
    JC_ENV_RUNNING = 0,
    JC_ENV_OK,                /* completed; verify passed or none configured  */
    JC_ENV_VERIFY_FAILED,     /* verifier still failing after the retry budget */
    JC_ENV_BUDGET_EXHAUSTED,  /* a token/time/tool-call budget was hit         */
    /* M332 (REFUSE-THE-GREEN): the verifier passed, but the run changed a file
     * outside `--edit-scope` first, so the green cannot be trusted -- it may have
     * been obtained by editing the gate. Opt-in via `strict_green`; APPENDED
     * rather than inserted so every existing value keeps its number for any
     * consumer that reads them. Work is NOT rolled back: this refuses a verdict,
     * it does not discard the run (the M80 principle). */
    JC_ENV_SCOPE_TAINTED
};

/* Which budget tripped (also used as a journal label). */
enum jc_env_budget {
    JC_BUDGET_NONE = 0,
    JC_BUDGET_TOKENS,
    JC_BUDGET_DEADLINE,
    JC_BUDGET_TOOLCALLS,
    JC_BUDGET_READS       /* M98: per-run read-tool cap (--max-reads) */
};

/* M343: what kind of verifier the operator says they wrote. An INVARIANT
 * answers "is the tree healthy?" -- green before the work, green after. A GOAL
 * answers "has the work happened?" -- red before the work BY CONSTRUCTION,
 * which is what makes it unfakeable. jichi treated the two as one, so a goal
 * gate made --verify-baseline warn on every correct run (a warning an operator
 * learns to skip is the corrosive kind of noise). The declaration is optional;
 * UNSET keeps every pre-M343 behaviour. See docs/AUTONOMY.md and finding 10 of
 * docs/analysis/2026-08-07-driving-zigodot-harness-findings.md, which rejected
 * both a silent flag split and inference -- this is neither: one optional
 * declaration, CHECKED against the tree at run start. */
enum jc_env_verify_kind {
    JC_VERIFY_KIND_UNSET = 0,
    JC_VERIFY_KIND_INVARIANT,
    JC_VERIFY_KIND_GOAL
};

/* What the baseline probe's result MEANS, given the declared kind. */
enum jc_env_baseline_verdict {
    JC_BASELINE_OK = 0,          /* green start, kind unset or invariant      */
    JC_BASELINE_NOT_KNOWN_GOOD,  /* red start, kind unset or invariant: warn  */
    JC_BASELINE_EXPECTED_RED,    /* red start, declared goal: the normal state */
    JC_BASELINE_FORCES_NOTHING   /* GREEN start, declared goal: the gate can
                                  * pass without the work -- the M330 trap,
                                  * caught before a token is spent */
};

/* How many edit-scope globs the run journal records. A cap, not a limit on the
 * scope itself: the journal is machine-read line-by-line and a pathological
 * scope should not produce a pathological line. Ten is far above any real
 * fence seen in this project. */
#define JC_ENV_SCOPE_JOURNAL_MAX 10

struct jc_envelope {
    /* Declared limits (0 / NULL means unset). */
    double        budget_tokens;    /* total input+output token cap          */
    long          deadline_secs;    /* wall-clock cap                        */
    int           max_tool_calls;   /* tool-execution cap                    */
    int           max_reads;        /* M98: read-tool cap (0 = unset). Prevents the
                                     * read-heavy budget bust M96 only detects. */
    struct jc_vec edit_scope;       /* of char*: path globs (caller-owned)   */
    char         *verify_cmd;       /* verifier shell command, or NULL       */
    int           verify_retries;   /* fix-forward attempts (default 3)      */
    long          verify_timeout;   /* per-verify wall-clock cap; 0 => none  */
    int           rollback_on_fail; /* roll back to green on failure (def 1) */
    int           verify_baseline;  /* verify once at run start (informative) */
    int           verify_kind;      /* M343: enum jc_env_verify_kind; declaring
                                     * one also arms the baseline probe -- a
                                     * declaration nobody checks is a comment */
    int           strict_scope;     /* edit-scope also forbids the shell tool */
    /* M332 (REFUSE-THE-GREEN): refuse a passing verify when this run changed a
     * file outside the edit scope. Off by default -- it turns a currently-zero
     * exit code non-zero, which is a documented stable interface, so it ships
     * opt-in until the false-positive rate on real runs is known. See
     * docs/GATE_INTEGRITY.md. */
    /* M336: preserve the tree a rollback is about to discard, under
     * refs/jichi/discarded/<run>/<n>. Opt-in; see the config key's comment. */
    int           preserve_discarded;
    int           discarded_n;   /* how many states this run has preserved */
    int           strict_green;
    /* M332: how many out-of-scope paths this run has been observed changing,
     * accumulated across turns. The count already existed inside the M83 report
     * and was thrown away; the verdict needs it. */
    int           out_of_scope_seen;
    /* M410: how many times M88's moved-goalpost heuristic fired (a test
     * assertion MODIFIED during this run). The warning existed since M88 and
     * was counted by nobody: `attempt` reported a bare PASS for a run whose
     * goalpost warning had fired ten times, then deleted the worktree -- so
     * the verdict contradicted the run's own log and the evidence was gone.
     * Detection stays heuristic and advisory; this only lets a VERDICT say
     * what the warnings said. */
    int           test_edits;
    /* M347: the budget notice has been injected (once per run -- M323's
     * lesson: one condition produced 1,038 warnings when the pass throttled
     * itself; only the caller knows what "once" means). */
    int           budget_noticed;
    /* M431f: the ambient budget panel (config `budgetPanel`, --budget-panel).
     * DEFAULT OFF -- M347's DECISIONS row rejected "a reminder per round or a
     * countdown" on the M323 evidence (1,038 warnings from one unthrottled
     * condition) and the principle that a nag a model learns to skip is worse than
     * one clear ask. That objection is a judgement about model behaviour, not a
     * measurement, so this ships as a FLAG to be measured rather than as a default
     * that overrules it. What is genuinely new since M347 is the RATE: caps at
     * takeoff plus one reading at 80% leave AUTONOMY.md's own "76% to 100% band"
     * empty, and tokens-per-call (measured 25-42k and RISING within a run) is what
     * turns "300k left" into "about eight calls left". */
    int           budget_panel;      /* the feature is on for this run          */
    long          panel_last_call;   /* tool_calls when the panel last rendered */
    double        panel_tokens;      /* tokens_used at that same moment        */
    long          model_calls;       /* in-run model calls, for the rate        */
    /* M351: the hollow-green notice has been injected (same once-per-run
     * throttle as the budget notice). */
    int           sanity_noticed;
    int           revert_out_of_scope; /* M142: at turn end, restore files the
                                     * M83 guard flags as changed outside the
                                     * edit scope (shell-introduced) back to
                                     * the run-start baseline (opt-in, def 0) */
    int           verify_every;     /* M81: run verify every N tool calls mid-
                                     * turn (0 = off; only at completion)     */

    /* Live counters. */
    double        tokens_used;
    /* M329: model calls metered AFTER the outcome was decided. Always a bug: the
     * envelope has concluded, its `end` event is written, and `jichi runs` has
     * already reported the run's totals -- so anything spent here is invisible.
     * Found the hard way: learn-on-stop fired after a budget stop and spent 625k
     * tokens past a 1m budget (M328). That was diagnosed by hand-comparing three
     * sinks and noticing a timestamp 50s past the `end` event; these two counters
     * exist so the next instance announces itself instead. */
    long          post_outcome_calls;
    double        post_outcome_tokens;
    int           post_outcome_warned; /* WARN + journal event emitted once */
    long          start_time;       /* time(NULL) at jc_env_init             */
    long          deadline_credit;  /* M162: seconds credited back by
                                     * `control pause --extend` -- the only
                                     * thing that may stretch the deadline   */
    int           tool_calls;      /* ATTEMPTED: what --max-tool-calls bounds */
    /* EXECUTED: attempts that survived every gate and actually ran a tool.
     *
     * Two honest meanings were sharing one counter until M459. `tool_calls`
     * bounds CONTAINMENT -- an operator writing --max-tool-calls N is bounding
     * what the agent may attempt, and counting only permitted work made the cap
     * weaker the more a run was refused (probe P13: five forbidden attempts,
     * cap of 2, never fired, `tool_calls: 0`, outcome `ok`). The delegate report
     * needs the other meaning: telling a parent "0 tool calls AND a denied
     * write" is an accurate, useful pairing that a count of attempts would
     * destroy.
     *
     * So both are kept. Reporting them TOGETHER is also strictly better than
     * either alone: attempted-many/executed-none is the machine-readable
     * signature of exactly the thrash P13 measured. */
    int           tool_calls_executed;
    int           reads;            /* M98: read-category tool calls so far   */
    int           retries_left;     /* armed to verify_retries each turn     */
    long          verify_every_last;/* tool_calls at the last periodic verify */
    int           test_file_written; /* a test-looking path was written this run
                                     * (jc_env_is_test_path). Feeds the
                                     * tests-not-wired check: "you added a test
                                     * and the gate ran no more of them".        */
    int           verify_max_tests; /* M86: largest test count seen in a green
                                     * verify this run (-1 = none observed yet) */
    int           repeat_fails;     /* M89: consecutive verify failures sharing
                                     * last_fail_sig (>=2 => stuck on one error) */
    char          last_fail_sig[JC_ENV_SIG_MAX]; /* M89: prev failure signature */
    int           repeat_blocks;    /* M429: consecutive POLICY-BLOCKED calls
                                     * sharing last_block_sig. Distinct from
                                     * repeat_fails above because a block is not a
                                     * failure: it cannot be fixed, only abandoned,
                                     * so the notice differs in kind. Measured: a
                                     * model repeated one forbidden write 5x and
                                     * spent a whole token budget on it.        */
    char          last_block_sig[JC_ENV_SIG_MAX]; /* M429: prev block signature */
    int           rolled_back;      /* M92: set when the run's work was actually
                                     * reverted to green at exit (vs kept). Orthogonal
                                     * to `outcome`; distinguishes a budget stop that
                                     * banked green work from one rolled back red. */
    enum jc_env_budget tripped;     /* M97: which budget stopped the run (NONE if it
                                     * completed); surfaced in the headless result */
    int           starved;          /* M97: set when the M96 all-reads-no-synthesis
                                     * guard fired at the budget stop; surfaced in the
                                     * headless result so a driving agent can re-scope */

    /* State. Commit ids are fixed 40-hex git SHAs, so they live inline
     * (48 = SHA + NUL, rounded; "" = unset) -- M218: the strdup-per-passing-
     * verify onto the session arena was unbounded in count over a long run. */
    char          green_commit[48]; /* SHA of the last known-good checkpoint */
    int           green_verified;   /* M207: 1 once a verify actually PASSED in
                                     * this run, so green_commit is known-good
                                     * rather than assumed-good. The first
                                     * pre-edit checkpoint is recorded as green
                                     * on the premise that the tree started
                                     * green; nothing checks that premise, and a
                                     * run whose gate is already red had its work
                                     * discarded at the budget stop in favour of
                                     * an equally red baseline. */
    char          baseline_commit[48]; /* SHA of the pre-run (first) checkpoint --
                                     * fixed, unlike green_commit which advances;
                                     * the M83 out-of-scope diff bases on this */
    enum jc_env_outcome outcome;

    /* M289: paths already reported by the M83 out-of-scope guard this run (of
     * char*, malloc'd). The guard diffs the whole tree against the FIXED
     * run-start baseline at every top-level turn end, so a file changed once
     * stays changed and was re-reported every turn afterwards: one measured run
     * logged 17 `out_of_scope` events that were all the same path. That reads as
     * 17 violations in `runs`, and the noise is what drove a real user to widen
     * `editScope` rather than look at the one file. Reported paths are
     * suppressed on later turns; a REVERTED path is not recorded, because the
     * revert makes the tree clean again and a later change to it is a genuinely
     * new violation. */
    struct jc_vec oos_reported;

    /* M501: paths THIS RUN wrote, root-relative, recorded at the
     * jc_app_write_file chokepoint (so write_file, edit_file, apply_patch and
     * the ACP fs delegate all land here). It exists because the envelope had no
     * provenance for a working-tree change: "changed since my baseline" and
     * "changed by me" were the same predicate, so `revertOutOfScope` would
     * revert a concurrent editor's merge -- a near-miss recorded in DEFERRED.
     * A shell-introduced change is deliberately NOT in this set; those are
     * reported and left alone rather than reverted, because jichi cannot tell a
     * model-issued `sed -i` from a colleague's. */
    struct jc_vec wrote;

    /* M501: did a shell-executing tool run this run? The edit-scope fence covers
     * the file tools but not `sed -i`, so the shell is the only way the run
     * itself can change a path outside its scope. With this flag the revert rule
     * becomes provable in one direction: no shell command and no chokepoint
     * write means the change cannot be the run's. */
    int           shell_ran;

    /* M503: where verify_cmd came from -- "flag" (the operator typed it),
     * "config" (inherited from `verify`/`testCommand`), or "" (none).
     *
     * Measured on a test-first authoring run: a project whose config sets
     * `testCommand` had that command become its envelope verifier without
     * anyone asking, and fix-forward then fed "the verify failed, fix it" back
     * at a model whose brief said the suite MUST end red -- 39 tool calls of
     * thrash, 1.56M tokens, nothing delivered. The inheritance is a good
     * default; being unable to SEE it from the journal is not. */
    const char   *verify_source;

    /* Audit journal (NULL when journaling is off). */
    FILE         *journal;
    char         *run_id;
};

/* ----- pure helpers (unit-tested) -------------------------------------- */

/* Parse a token-budget literal: digits with an optional k/m suffix (decimal:
 * "200k" => 200000, "1m" => 1e6, "512" => 512). Returns 0 on success and writes
 * *out; -1 on a malformed or negative value. */
int jc_env_parse_size(const char *s, double *out);

/* Parse a duration: a number with an optional s/m/h suffix (bare = seconds;
 * "20m" => 1200, "2h" => 7200). Returns 0 / writes *out, -1 on error. */
int jc_env_parse_duration(const char *s, long *out);

/* M289: has this out-of-scope path already been reported this run? Pure (reads
 * the envelope's reported set, mutates nothing). `jc_env_oos_mark` records one.
 * Split so the suppression logic is unit-testable without a git tree. */
int  jc_env_oos_reported(const struct jc_envelope *e, const char *path);
void jc_env_oos_mark(struct jc_envelope *e, const char *path);

/* M501: normalise `path` for comparison against the edit scope or the write set
 * -- strips a `root` prefix (on a path boundary) and a leading "./". Returns a
 * pointer INTO `path`; NULL in, NULL out. The scope check and the write set
 * share it so a formatting difference can never disarm the revert. */
const char *jc_env_relpath(const char *root, const char *path);

/* M501: did this run write `path` (through jc_app_write_file)? / record that it
 * did. Both normalise with jc_env_relpath, so an absolute path from a tool and a
 * git-relative path from the diff compare equal. */
int  jc_env_wrote(const struct jc_envelope *e, const char *root,
                  const char *path);
void jc_env_wrote_mark(struct jc_envelope *e, const char *root,
                       const char *path);

/* Minimal path glob: '?' (one non-slash char), '*' (a run not crossing '/'),
 * '**' (a run that may cross '/'). Returns 1 on a full match. */
int jc_glob_match(const char *pat, const char *path);

/* 1 if `path` is permitted by the edit-scope (no patterns => always permitted).
 * `root` is the canonical workspace root (may be NULL/""): an absolute `path`
 * under `root` is relativized to a root-relative path before matching, so it can
 * match the workspace-relative editScope globs (the file tools pass absolute
 * paths). A leading "./" on the (relativized) path is also ignored. Pure. */
int jc_env_path_in_scope(const struct jc_envelope *e, const char *root,
                         const char *path);

/* Which budget (if any) is exhausted given the counters and `now` (passed in so
 * this stays pure / testable without time()). */
enum jc_env_budget jc_env_over_budget(const struct jc_envelope *e, long now);

/* M347: which armed budget has crossed four fifths of its cap, or NONE --
 * the same checks and order as jc_env_over_budget, at 80%. The human watching
 * the TUI reads ctx% and $ live in the prompt line; the agent flying the
 * envelope was told nothing until the engine stopped (0/7 implementation runs
 * completed in the 2026-08-07 driving session, every one budget_exhausted;
 * M96's starved analysis dies with its report unwritten). Integer four-fifths
 * arithmetic, so the crossing call is exact. Pure; unit-tested. The caller
 * owns "once" via e->budget_noticed. */
enum jc_env_budget jc_env_budget_notice_due(const struct jc_envelope *e,
                                            long now);

/* M347: render the one [envelope] budget line the model gets to see: every
 * ARMED budget's used-of-cap state (an unarmed budget is never named), then
 * the ask -- land the run rather than start new work. Appends to `sb`.
 * Pure; unit-tested. */
/* M431f: the ambient budget panel -- the reading M347's one-shot notice is not.
 * `jc_env_panel_due` is the cadence (every `every`-th tool call, plus each quintile
 * crossing of the token budget, throttled so one boundary cannot render twice);
 * `jc_env_panel_render` is the line. Armed budgets only, and the rate/projection are
 * omitted rather than guessed when there is no data. Both pure; unit-tested.
 *
 * OFF by default: see the budget_panel field above for why this is a flag to be
 * measured rather than a default that overrules a shipped decision. */
int jc_env_panel_due(const struct jc_envelope *e, int every);
void jc_env_panel_render(const struct jc_envelope *e, long now, struct jc_sb *sb);

void jc_env_budget_notice_render(const struct jc_envelope *e, long now,
                                 struct jc_sb *sb);

/* Human-readable names (for journal/summary). Never NULL. */
const char *jc_env_budget_name(enum jc_env_budget b);
const char *jc_env_outcome_name(enum jc_env_outcome o);

/* M92: a human-facing label composing the outcome with the terminal DISPOSITION
 * (was the work kept or reverted?), for operator-facing summaries where the bare
 * `outcome` misleads -- a budget-sized `--auto` increment normally ends
 * `budget_exhausted` with its green work KEPT (M80), not rolled back. Returns e.g.
 * "budget_exhausted (work kept)" vs "budget_exhausted (rolled back)", "completed",
 * "verify_failed (rolled back)". Distinct from jc_env_outcome_name (the bare enum
 * name used in the journal `outcome` field + exit-code logic, left unchanged).
 * Never NULL. Pure; unit-tested. */
const char *jc_env_disposition_name(enum jc_env_outcome o, int rolled_back);

/* Summarize a newline-separated file list (as jc_snapshot_changed_since produces:
 * one path per line, optional trailing newline) into a bounded, ", "-joined string
 * for a rollback disposition report. Appends up to `max` names to `out`, then
 * " (+K more)" when the total exceeds `max`. Returns the TOTAL count. Pure:
 * NULL/empty names -> 0 (out untouched); a NULL `out` still returns the count.
 * Used by the budget-revert transparency report (which files were kept vs
 * discarded) so a supervisor sees the post-revert reality, not just kept=false. */
int jc_env_summarize_paths(const char *names, int max, struct jc_sb *out);

/* Policy (M80): should a budget/deadline/tool-call exhaustion roll the workspace
 * back to green? Budget exhaustion is NOT a broken state (unlike a verify
 * failure), so rollback fires ONLY when a verifier is configured AND its tree is
 * red -- never merely because the run ran out of budget. Returns 1 to roll back.
 * `verify_code` is the verifier's exit (0 = pass); it is consulted only when
 * `has_verifier`.
 *
 * M207: `has_green` must mean OBSERVED green, not merely "a checkpoint exists".
 * The first pre-edit checkpoint is recorded as green on the unchecked premise
 * that the tree started green. On a run whose gate is red from the outset that
 * premise is false, and this function then discarded a drive's real work in
 * favour of a baseline that was equally red -- the opposite of the M80 intent
 * this comment states. Rolling back to a state not known to be better than the
 * current one is never an improvement, so the caller passes
 * `env->green_verified`. Pure; unit-tested. */
int jc_env_budget_rollback_decision(int rollback_on_fail, int has_green,
                                    int has_verifier, int verify_code);

/* M96: warn on the "all reads, no synthesis" failure -- a read-only / no-edit
 * --auto run whose only deliverable is its final answer, that exhausts a budget
 * before finishing. On such a run nothing is written to disk (M80 keeps nothing
 * when no edit was made), so budget exhaustion means the *answer itself* was
 * truncated -- e.g. a read-only analysis over a broad --reference-root that
 * over-reads until the token budget dies, yielding an empty report. Returns 1 to
 * emit the advisory hint (narrow the scope / raise the budget / cap reads).
 * Fires only when the budget was exhausted, snapshots are on (so `made_edits`
 * is trustworthy -- absent snapshots green_commit is NULL even for an edit run),
 * and no edit was made this run (`made_edits` == 0). Advisory: detection only,
 * never changes the outcome. Pure; unit-tested. */
int jc_env_analysis_starved(enum jc_env_outcome outcome, int snapshots_on,
                            int made_edits);

/* Out-of-scope guard (M83): given the paths a run changed, push the ones that
 * are NOT in the edit scope onto `out` (a jc_vec of `const char *` borrowing the
 * caller's path strings). Catches shell-introduced changes (rm/mv/redirect via
 * run_terminal_command) that the file-write-tool fence can't see. No-op when the
 * envelope has no edit scope (nothing is fenced). `paths` are workspace-relative
 * or absolute (matched via jc_env_path_in_scope). Pure; unit-tested. */
void jc_env_out_of_scope_paths(const struct jc_envelope *e, const char *root,
                               const char *const *paths, int n,
                               struct jc_vec *out);

/* M86: verdict for a *green* (exit 0) verify. The hollow-gate lesson: a gate
 * that passes while running zero tests -- or fewer than it ran earlier this run
 * -- is a false "green" (whole subsystems can rot uncompiled/unreached).
 * Advisory only: detection, never a change to the run's outcome. */
enum jc_verify_sanity {
    JC_VERIFY_SANE = 0,     /* nothing suspicious (or no test signal at all)   */
    JC_VERIFY_NO_TESTS,     /* green but an observed test count of 0           */
    JC_VERIFY_FEWER_TESTS,  /* green but ran fewer tests than an earlier green */
    JC_VERIFY_TESTS_NOT_WIRED /* green, a TEST FILE was edited, and the count did
                               * not grow -- the new test probably never runs   */
};

/* Does `path` look like a test file? The M88 heuristic, exposed so the
 * hollow-gate checks can share one definition rather than drift apart. Pure. */
int jc_env_is_test_path(const char *path);

/* Decide whether a just-passed verify is suspicious. `count_now` is the test
 * count observed this run (jc_test_report_count; <0 = unknown -> SANE),
 * `prev_max` the largest count seen in a prior green verify this run (<0 =
 * none), and `test_edit` whether a test-looking file was written this run.
 *
 * The `test_edit` case closes M86's documented blind spot. M86 asks whether the
 * gate ran FEWER tests; it cannot see a gate that runs the right NUMBER of tests
 * without exercising the change. Measured 2026-08-07 in a downstream project: a
 * run added a 13-test file, the suite stayed at exactly 253 passing because
 * nothing referenced it, and both the gate and a count-based check called that
 * green. Narrowing the question to "a test file was edited AND the count did not
 * grow" keeps the false-positive rate low -- it only fires when the run itself
 * claimed to add tests. Pure; unit-tested. */
/* M351: render the note the MODEL gets when a mid-run green is hollow. The
 * loop feeds every RED verify back (fix-forward), so the model always hears
 * its failures -- but M86's finding that a GREEN ran zero tests, fewer than
 * an earlier green, or none of a freshly-edited test file went to the
 * operator alone (log, journal, on_status), while the model banked the false
 * confidence and built on "tests pass". One [envelope] line closes that: the
 * finding, and the behaviour asked for (check the gate; say plainly that the
 * result is unverified if it cannot be fixed -- conduct, not vocabulary, per
 * M299). SANE appends nothing. Pure; unit-tested. */
void jc_env_sanity_note(enum jc_verify_sanity v, int count_now, int prev_max,
                        struct jc_sb *out);

enum jc_verify_sanity jc_env_verify_sanity(int count_now, int prev_max,
                                           int test_edit);

/* M331: does a verify's VERDICT agree with its own EVIDENCE?
 *
 * Every guarantee the envelope makes reduces to one bit -- the verifier's exit
 * code -- while jc_testparse independently reports how many tests passed and
 * failed. When those two disagree, the run proceeds on a false premise, and the
 * expensive direction is a RED verdict with no failing test: the model then
 * spends its whole budget repairing something that was never broken. Measured in
 * one 28-run engagement, 16 runs ended in budget exhaustion.
 *
 * `exit_code` is the verifier's status, `passed`/`failed` the parsed counts (<0 =
 * unknown), `prev_max` the largest test count seen in a prior GREEN verify this
 * run (<0 = none).
 *
 * Three design decisions, each with the reason it went the way it did:
 *
 * 1. A SIBLING of jc_env_verify_sanity rather than an extension of it. The
 *    proposal (docs/proposals/2026-08-verify-consistency.md) said this should
 *    subsume M86; reading M86 said otherwise. The two answer different questions
 *    from different inputs -- "is this green gate hollow?" from a count plus a
 *    test-file-edited flag, versus "does this verdict match its evidence?" from
 *    an exit code plus pass/fail counts -- and merging them yields one function
 *    with five parameters and two unrelated purposes. The proposal is wrong on
 *    this point and is annotated to say so.
 *
 * 2. GREEN always returns AGREES, because green is M86's territory. Without this
 *    a shrinking test count would warn twice, from two checks, in one verify.
 *
 * 3. HOLLOW_RED requires `passed > 0`. A red verify with NO counts at all is a
 *    compile error -- entirely normal, and the single most common failing verify
 *    there is: 26 of 67 in that engagement, every one legitimate. Warning on it
 *    would be the crying-wolf failure that stops a check being read. The cost of
 *    this decision is real and worth stating: the incident that motivated the
 *    feature (ANECDOTES #42, where Zig 0.16 failed the build step over stderr
 *    output) ALSO suppressed the counts, so this check would not have caught that
 *    instance. It catches the class, not its most famous member.
 *
 * Advisory: the caller warns, journals, and pings on_status. It never changes an
 * outcome, following the M83 out-of-scope guard and the M96 starved detector.
 * Pure; unit-tested. */
enum jc_verify_consistency {
    JC_VERIFY_AGREES = 0,      /* verdict and evidence agree (or green)        */
    JC_VERIFY_HOLLOW_RED,      /* failed, yet no test failed and some passed   */
    JC_VERIFY_RED_TESTS_GONE   /* failed, and fewer tests ran than before      */
};

enum jc_verify_consistency jc_env_verify_consistency(int exit_code, int passed,
                                                     int failed, int prev_max);

/* M332 (REFUSE-THE-GREEN): should a passing verify be refused? True when the
 * feature is on, this run changed at least one file outside the edit scope, and
 * the outcome is currently JC_ENV_OK.
 *
 * A predicate rather than an inline condition for the same reason
 * jc_env_budget_rollback_decision is one: it encodes a policy someone will want
 * to argue with, and an argument is easier against a named function with tests
 * than against three clauses inside a 3000-line file.
 *
 * Note what it does NOT ask: whether the changed file was the verifier's own.
 * Identifying that cannot be done in general (`./gate.sh` is a path, `make test`
 * is a Makefile, `zig build test` is build.zig and everything it imports), and a
 * guess there fails OPEN. Asking "did this run change something it was told not
 * to, and then pass?" needs no parsing and covers routes nobody has thought of.
 * The exception has vocabulary already: put the gate in --edit-scope and no flag
 * is raised. Pure; unit-tested. */
int jc_env_refuse_green(int strict_green, int out_of_scope_seen,
                        enum jc_env_outcome outcome);

/* Policy (M81): is a periodic mid-turn verify due? True when `verify_every` > 0
 * and at least that many tool calls have run since the last periodic verify
 * (`last`). Keeps a long implementation turn green incrementally instead of
 * thrashing to a budget with a broken build. Pure; unit-tested. */
int jc_env_should_verify_now(int verify_every, long tool_calls, long last);

/* M343: parse a declared verifier kind ("invariant" | "goal", exact) into
 * enum jc_env_verify_kind. Returns 1 and writes *out on success, 0 on any
 * other string (the caller decides whether that is a hard error -- the CLI --
 * or a warn-and-ignore -- the config). Pure; unit-tested. */
int jc_env_verify_kind_parse(const char *s, int *out);

/* M343: canonical name for a kind, for journals and messages. Never NULL. */
const char *jc_env_verify_kind_name(int kind);

/* M343: what the baseline probe's exit code MEANS under the declared kind.
 * The truth table is exhaustive and deliberately boring; the one row that pays
 * for the feature is (GOAL, exit 0) -> FORCES_NOTHING: a goal gate that is
 * green on the untouched tree can be satisfied without the work, which is the
 * hollow-requirement trap that cost two runs ~3M tokens (ANECDOTES #38, the
 * M330 forcing check). With the kind UNSET both rows behave exactly as before
 * M343. Pure; unit-tested. */
enum jc_env_baseline_verdict jc_env_baseline_check(int kind, int exit_code);

/* M89: extract a stable signature from verifier output for detecting a run stuck
 * on one error -- the text of the first line containing "error:" (from "error:"
 * to end-of-line, trimmed), which drops the varying file:line prefix so the same
 * compile error at any location matches. Writes into `buf` (cap>0). Returns 1 if
 * a signature was found, else 0 (and `buf` is set to ""). Pure; unit-tested. */
int jc_env_fail_signature(const char *output, char *buf, jc_size cap);

/* M89: record a verify FAILURE's signature on the envelope and return how many
 * consecutive failures have now shared it -- 1 for a new/changed error, >=2 when
 * the SAME error just recurred (a stuck fix/break loop, e.g. re-guessing a moved
 * stdlib API). Resets to 0 when no signature is extractable. The caller surfaces
 * a "you keep hitting the same error" hint at >=2. Mutates `e`; no I/O. */
int jc_env_note_failure(struct jc_envelope *e, const char *output);

/* M429: the same idea for a POLICY BLOCK, which is deliberately NOT the same
 * thing. `jc_env_note_failure` above tracks something that failed and might
 * succeed differently; this tracks something the run is not permitted to do at
 * all. Returns how many consecutive blocks have shared (tool, target) -- 1 for a
 * new one, >=2 when the model just tried the same forbidden thing again.
 *
 * The distinction matters because the ADVICE inverts. "Try a different fix" is
 * right for a stuck verify and wrong here: a forbidden action does not succeed
 * however it is rephrased, and telling a model to vary its approach is what
 * produced the measured case -- five attempts at one blocked write, a whole token
 * budget spent, no intervention (probe P13). Mutates `e`; no I/O. */
/* M435: the model-facing half of M88's moved-goalpost detection.
 *
 * M88 detects an edit that changes a test's ASSERTION and routes the finding to the
 * journal, telemetry, a WARN, on_status and (M410) attempt's verdict -- to everyone
 * except the model that made the edit. ANECDOTES #51 is the cost: the warning fired
 * TEN TIMES while a model kept editing gate assertions, and the verdict printed PASS.
 * The same model, fenced to the one file the task named, produced a correct
 * implementation at one tenth the cost.
 *
 * `nth` is the run's running count (env->test_edits AFTER the increment), so the
 * count itself is the escalation. Deliberately NOT once-per-turn: each assertion edit
 * is a distinct decision rather than a repetition of one, and a model told once and
 * then left in silence for nine more has been shown the practice is tolerated.
 *
 * Says the edit is RECORDED and does not count if it was to make a gate pass -- never
 * that it is forbidden, because fixing a genuinely wrong test is legitimate and
 * refusing it would be the cure worse than the disease. Pure; unit-tested. */
void jc_env_test_edit_note(int nth, const char *path, char *out, jc_size cap);

int jc_env_note_blocked(struct jc_envelope *e, const char *tool,
                        const char *target);

/* M88: heuristic -- does this edit look like it MODIFIED an existing test's
 * assertion (a possible "moved goalpost" that turns a red gate green by changing
 * the EXPECTED value instead of fixing the code -- extends M86's hollow-gate
 * detection to a *moved* gate)? True when `path` looks like a test file, both
 * `old_string` and `new_string` contain an assertion marker (expect/assert/
 * testing./GDTEST/...), and the change is a modification rather than a pure
 * addition (`new_string` does not contain `old_string`). Detection-only, so a
 * false positive is just a reviewable warning. Pure; unit-tested. */
int jc_env_test_assertion_edit(const char *path, const char *old_string,
                               const char *new_string);

/* ----- effectful helpers (verified end-to-end) ------------------------- */

/* Initialise `e`: defaults (3 retries, rollback on), start_time = now, copy
 * run_id into `a`, and open `journal_path` for append (its parent directory
 * must already exist; NULL/"" disables journaling). Always returns JC_OK. */
jc_status jc_env_init(struct jc_envelope *e, struct jc_arena *a,
                      const char *run_id, const char *journal_path);

void jc_env_free(struct jc_envelope *e);

/* Add input+output tokens to the running total. */
void jc_env_record_tokens(struct jc_envelope *e, double in_tok, double out_tok);

/* Run `cmd` via /bin/sh -c in `cwd` (chdir in the child; no shell quoting of
 * cwd), capturing combined stdout+stderr into `out` when non-NULL. Returns the
 * child exit code, -1 if it could not be run, or -2 if it was killed because it
 * exceeded `timeout_secs` (0 => no limit) or `*abort` became set. */
int jc_env_run_verify(const char *cmd, const char *cwd, struct jc_sb *out,
                      volatile int *abort, long timeout_secs);

/* Sentinel return from jc_env_run_verify when the command was killed. */
#define JC_VERIFY_TIMEOUT (-2)

/* Journal one event. Begin returns a cJSON object pre-stamped with ts/run/event
 * (NULL when journaling is off); add fields to it, then End writes one compact
 * JSON line and deletes the object (End tolerates a NULL object). */
cJSON *jc_env_journal_begin(struct jc_envelope *e, const char *event);
void   jc_env_journal_end(struct jc_envelope *e, cJSON *o);

#ifdef __cplusplus
}
#endif
#endif /* JC_ENVELOPE_H */
