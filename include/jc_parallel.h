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
};

/* Classify one newline-free JSON child message into *out (zeroed first; the
 * caller frees out->answer/out->error). Returns out->kind. Pure (no I/O). */
int jc_parallel_parse_msg(const char *line, struct jc_pmsg *out);

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
