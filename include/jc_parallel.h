/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_parallel.h - pure, unit-tested helpers for the parallel agent swarm.
 *
 * The fork pool, pipes and git worktrees live in src/tools/jc_tool_parallel.c
 * (the spawn_parallel tool); these are the deterministic cores it builds on:
 * concurrency sizing, parsing a worktree's change list, and the first-wins
 * file-level merge claim. See docs/PARALLEL.md.
 */
#ifndef JC_PARALLEL_H
#define JC_PARALLEL_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_mem.h"

/* One changed path from `git diff --name-status --no-renames`. */
struct jc_change {
    char  status;  /* 'A' added | 'M' modified | 'D' deleted (others kept) */
    char *path;    /* arena-owned, relative to the work tree               */
};

/* Effective concurrency: min(n_tasks, cfg_max > 0 ? cfg_max : min(cpu,
 * ceiling)), clamped to at least 1. */
int jc_parallel_eff_max(int n_tasks, int cfg_max, int cpu, int ceiling);

/* Parse `git diff --name-status --no-renames` output (lines "<C>\t<path>") into
 * `out` (a jc_vec of struct jc_change); paths are copied into `a`. Blank or
 * tab-less lines are ignored; a trailing CR is trimmed. */
void jc_parallel_parse_changes(const char *text, struct jc_arena *a,
                               struct jc_vec *out);

/* M927 (R04-F1): parse the NUL form of the same listing
 * (`git diff --name-status --no-renames -z`): records are
 * "<status>\0<path>\0". Paths are never quoted and may contain newlines;
 * the line form above cannot represent those, and a merge must not lose
 * them. Empty/short input is ignored. */
void jc_parallel_parse_changes_z(const char *text, struct jc_arena *a,
                                 struct jc_vec *out);

/* First-wins claim for the file-level merge: returns 1 and records `path` when
 * it has not been claimed yet; returns 0 (a conflict) when an earlier child
 * already claimed it. `seen` is a jc_vec of char* whose entries must outlive
 * it (e.g. arena-owned paths). */
int jc_parallel_claim(struct jc_vec *seen, const char *path);

/* M144: resolve the verifier a write child's worktree is gated on before its
 * changes may merge: the envelope's command, else config `verify`, else
 * `testCommand` (the same precedence the run-level gate uses in main.c).
 * NULL/empty inputs are skipped; returns NULL when nothing is configured.
 * Pure; unit-tested. */
const char *jc_parallel_verify_cmd(const char *env_cmd,
                                   const char *config_verify,
                                   const char *test_command);

/* A progress message a child streams over its pipe (newline-framed compact
 * JSON: {"t":"tool","name":..} | {"t":"tok","n":..} | {"t":"done",...}). */
enum jc_pmsg_kind { JC_PMSG_NONE = 0, JC_PMSG_TOOL, JC_PMSG_TOK, JC_PMSG_DONE };

/* M863: the room for a child's kept-before-discard refs in its done line. */
#define JC_PAR_PINNED_MAX 600

struct jc_pmsg {
    int    kind;        /* enum jc_pmsg_kind                              */
    char   tool[64];    /* TOOL: the tool name                           */
    double tokens;      /* TOK/DONE: token count                         */
    int    tool_calls;  /* DONE: tool calls the child made (budget merge) */
    char  *answer;      /* DONE: malloc'd answer, or NULL (caller frees)  */
    char  *error;       /* DONE: malloc'd error, or NULL (caller frees)   */
    /* M437: the child's delegation report, so the parent can render every child
     * through the same renderer the synchronous spawn_subagent uses. Fixed
     * buffers, not pointers: these are short and bounded, and the caller already
     * has two frees to remember. `stop` is empty for a pre-M437 child message,
     * which the renderer treats as "not reported" rather than as "done". */
    char   stop[16];
    char   ftool[64];
    char   fmsg[200];
    int    fcls;       /* enum jc_fail_class */
    int    repeated;   /* M808: tool-loop repeat count of the last detected loop */
    int    fence_hints; /* M852: shell commands that hit a permission error
                         * under the shell fence ("fhints") */
    /* M854 (N33, N24c): how a run that did not end on its own ended. */
    char  *last;        /* DONE: malloc'd last assistant text of a run that
                         * did not end OK ("last"), or NULL (caller frees) */
    int    term;        /* DONE: the child saw SIGTERM ("term")           */
    int    overflow;    /* DONE: a context-window 400 ended it ("overflow") */
    int    cut;         /* M858: DONE: its last reply was cut at the output
                         * ceiling; the value is that ceiling in tokens ("cut") */
    int    looped;      /* M869: DONE: which loop stop ended it ("looped"), 0 none */
    int    fcount;      /* M876: DONE: failures of fcls in the run ("fcount") */
    int    fstop;       /* M885: DONE: the last failing call was killed by the
                         * child's own stop, not by a limit ("fstop") */
    char   rerr[200];   /* M876: DONE: the run's model failure ("rerr"), or "" */
    int    rerrn;       /* M876: its attempts ("rerrn")                     */
    long   rerr0;       /* M876: wall clock of the first failed attempt     */
    long   rerr1;       /* M876: ...and of the last                         */
    int    tused;       /* M861: DONE: the calls its task budget counted, its
                         * delegates' included ("tused"); 0 when it had none */
    int    npinned;     /* M863: DONE: states kept before discarding commands */
    char   pinned[JC_PAR_PINNED_MAX]; /* M863: DONE: their refs ("pinned") */
};

/* Classify one newline-free JSON child message into *out (zeroed first; the
 * caller frees out->answer/out->error/out->last). Returns out->kind. Pure (no
 * I/O). */
int jc_parallel_parse_msg(const char *line, struct jc_pmsg *out);

/* M854 (N33, N24c): how a WRITE child that did not end on its own ended, as a
 * phrase for its note ("was stopped from outside (SIGTERM)", "ran out of
 * context ...", "stopped at its iteration cap"), or NULL for every other
 * child. `finished` is whether its done line arrived: a child the BATCH stopped
 * (an abort, a timeout) is killed with its pipe closed and never reports, and
 * `batch_aborted` covers the race in which a group-wide signal reached the child
 * before the parent noticed its own abort -- a batch abort never merges. `stop`
 * is the child's own stop name (jc_delegreport_stop_name). M858 (N42): `cut` adds a
 * fourth ending -- a run that ended on a reply cut at the output ceiling (its stop
 * "done", or "no_answer" when the cut reply had no text). Pure; unit-tested. */
const char *jc_parallel_ending(int write, int has_worktree, int finished,
                               int batch_aborted, const char *stop, int term,
                               int overflow, int cut);

/* M869 (N27, def-305): the ending phrase for a child a LOOP stop ended (its stop
 * is max_iters, which M854 names "stopped at its iteration cap"): 1 the same
 * successful call (M829), 2 the same refused call, 3 copied placeholders, 4 a
 * rotation with nothing new (M870, N21); NULL for any other value. Pure. */
const char *jc_parallel_loop_ending(int looped);

/* M861 (N28): a task's `max_tool_calls`, read leniently -- a JSON number that is
 * a whole number, or a string of digits, as a model may send either. Returns 0
 * when it is absent (NULL or JSON null), 1 with *out set (values above
 * JC_PAR_MAX_CALLS_CAP are capped to it), -1 for anything else: 0, a negative,
 * a fraction, words. A refused value refuses the task, so that a lead never
 * believes a budget binds that does not. Pure; unit-tested. */
#define JC_PAR_MAX_CALLS_CAP 1000000
struct cJSON;
struct jc_sb;
int jc_parallel_parse_max_calls(const struct cJSON *v, int *out);

/* M861: the call count at which a task with a budget of `eff` tool calls gets its
 * one note -- four-fifths of it, rounded up -- or 0 when the budget is too small
 * for a note to come before the end (under 5). */
int jc_parallel_budget_note_at(int eff);

/* M861: the note itself, appended to `out`: "[jichi] You have used <used> of the
 * <eff> tool calls this task allows. ..." */
void jc_parallel_budget_note(int used, int eff, struct jc_sb *out);

/* M862 (N43): a red worktree verify's excerpt for the batch result, appended to
 * `dst`. The tail is the last `tail` bytes, started at its first line's start when
 * that is within JC_PAR_VFAIL_LINE_MAX bytes (else cut, and marked "..."). First
 * the lines BEFORE the tail that carry a failure marker
 * ("FAIL", "fail", "Fail", "error:", "Error", "ERROR", "xpected", "panic",
 * "ssert", "not ok", "Traceback"; case as written), at most JC_PAR_VFAIL_LINES of
 * them, each cut at JC_PAR_VFAIL_LINE_MAX bytes, with a count of the matching
 * lines left out; then "output tail:" and the last `tail` bytes, as before M862.
 * Zig prints its failing tests before its summary, so a tail alone held the
 * summary and the failed build command (run 0090: the lead saw "3 failed" and
 * no names, and guessed). Binary-safe; pure; unit-tested. */
/* M865 (the demos drive's §9, "the verify trusts a test the child may edit"; the
 * operator's decision of 2026-10-02: hold the merge): does `diff` (a
 * `git diff --unified=0` of the one file `path`) change an existing test
 * assertion? Per hunk, the removed lines and the added lines go to M88's
 * jc_env_test_assertion_edit -- a test-looking path, an assertion marker on both
 * sides, and the old text not kept -- so adding a test is not one, and moving an
 * expected value is. Pure; unit-tested. */
int jc_parallel_diff_edits_assertion(const char *path, const char *diff);

/* M875: the metric gate's pure parts (docs/proposals/2026-10-metric-gate.md).
 *
 * jc_parallel_metric_parse: the metric in a command's output -- the first number
 * after the first occurrence of `label`, or, with no label (NULL or ""), the last
 * number printed. A number is an optional '-', digits and an optional fraction,
 * not inside a word ("L1", "x86_64" and "v1.2" hold none). 1 with *v set; 0 when
 * there is none, or the label does not occur.
 * jc_parallel_metric_worse: is `value` worse than `base`, given the direction?
 * jc_parallel_path_is_pin: does `path` match a declared pin -- exactly, or as a
 * jc_glob_match glob; a leading "./" on either side is ignored.
 * jc_parallel_metric_fmt: the number as a person reads it: an integer without a
 * fraction, else trailing zeros trimmed. Pure; unit-tested. */
int jc_parallel_metric_parse(const char *out, const char *label, double *v);
int jc_parallel_metric_worse(double base, double value, int higher_better);
int jc_parallel_path_is_pin(const char *path, const struct jc_vec *pins);
void jc_parallel_metric_fmt(double v, char *buf, jc_size cap);

#define JC_PAR_VFAIL_LINES 12
#define JC_PAR_VFAIL_LINE_MAX 200
void jc_parallel_verify_excerpt(const char *out, jc_size len, jc_size tail,
                                struct jc_sb *dst);

/* Per-agent state for the live status board (TUI). */
enum jc_board_state {
    JC_BOARD_RUN = 0,   /* running (detail = current tool, or "")   */
    JC_BOARD_DONE,      /* finished with an answer                  */
    JC_BOARD_FAIL,      /* errored (detail = the error message)     */
    JC_BOARD_TIMEOUT    /* killed by the per-child watchdog         */
};

/* Format one aligned board line into `buf`:
 *   "[<n>] <tag> <model>      <detail> (<tok>k)"
 * `tag` is a fixed-width state word (run/done/FAIL/time); `detail` is the
 * current tool (RUN) or the error (FAIL), "" to omit; tokens<=0 omits the count.
 * 1-based index. Pure (no I/O); always NUL-terminates. */
void jc_parallel_board_line(char *buf, jc_size cap, int idx, const char *model,
                            int state, const char *detail, double tokens);

#ifdef __cplusplus
}
#endif
#endif /* JC_PARALLEL_H */
