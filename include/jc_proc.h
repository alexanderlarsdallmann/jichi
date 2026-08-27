/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_proc.h - fork/exec a child, feed it stdin, capture combined stdout+stderr.
 *
 * Shared by the user-tool runner (jc_tool_user.c) and the lifecycle-hook runner
 * (jc_hooks.c), which previously carried near-identical copies. Unlike those
 * copies, jc_proc_capture services stdin and stdout *concurrently* via select on
 * non-blocking pipes, so a child that emits output before draining its stdin
 * cannot deadlock the parent.
 */
#ifndef JC_PROC_H
#define JC_PROC_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

#include <stdio.h>

struct jc_sb;  /* jc_str.h */
struct jc_vec; /* jc_vec.h */

/* --- SIGPIPE and the children we spawn (M461) --------------------------------
 *
 * jichi ignores SIGPIPE at its entry points, so a client that disconnects
 * mid-stream cannot kill the agent. POSIX resets *caught* signals to their
 * default across exec but leaves *ignored* ones ignored, so every command the
 * agent runs -- run_terminal_command, user tools, hooks, the verify gate, git,
 * MCP stdio servers, LSP servers -- has been starting life with SIGPIPE
 * ignored, inherited rather than chosen.
 *
 * The consequence is a hang, not a crash. In any pipeline the agent runs
 * (`producer | head -20` is the everyday shape), the producer no longer dies
 * when the consumer exits: write() returns EPIPE instead. A program that
 * checks its write result exits anyway -- which is why GNU coreutils hide
 * this on Linux -- but one that does not spins until jichi's kill deadline.
 * OpenBSD's yes(1) is the clean case: `yes | head -n 25000` finished in 0.00s
 * with SIGPIPE at its default and had to be killed at 20s with it ignored,
 * burning 4.42s of CPU. Found by the OpenBSD row; the bug is on every
 * platform, and only the userland differed.
 *
 * Call jc_proc_child_sigreset() in the forked child, immediately before exec.
 * Use jc_proc_popen() instead of popen(): a popen'd child cannot fix this
 * itself, because POSIX forbids a non-interactive shell from trapping or
 * resetting a signal that was ignored on entry -- `trap - PIPE` does nothing.
 * tests/smoke/posix_utils_lint.sh fails the build on a bare exec or popen. */
void jc_proc_child_sigreset(void);

/* popen(3) with SIGPIPE at SIG_DFL for the duration of the fork, so the child
 * does not inherit the agent's SIG_IGN. The parent's disposition is restored
 * before this returns; the window contains nothing but popen() itself, and
 * jichi forks rather than threads, so nothing else can observe it. */
FILE *jc_proc_popen(const char *cmd, const char *mode);

/* --- inherited file descriptors (M472) --------------------------------------
 *
 * exec() replaces the code, not the descriptor table. So without either of the
 * two helpers below, every child inherits every descriptor jichi holds -- and
 * one of jichi's children is `sh -c <whatever the model chose>`.
 *
 * MEASURED before these existed, by having a model-issued run_terminal_command
 * list /proc/self/fd:
 *
 *   3 -> run.jsonl        the run journal        WRITABLE
 *   4 -> telemetry.jsonl  the telemetry sink     WRITABLE
 *   5 -> pipe (read end)  \ both ends of one internal pipe
 *   6 -> pipe (write end) /
 *   7 -> socket           the live provider connection, READ/WRITE
 *
 * and then, with `echo {*FORGED*} >&3`, a forged record landed in the middle of
 * the run journal -- the sink `jichi runs` and `doctor --unattended` read to
 * gate an unattended loop.
 *
 * This is a THIRD channel, next to the two already fenced: M130 scrubs the
 * child's environment, M132 sets file modes. A descriptor obeys neither -- the
 * permission check happened at open() time, in the parent, and the child holds
 * the result. It is also the invisible one: it shows up in neither `env` nor
 * `ls -l`, only in /proc/self/fd, and you only look there if you already
 * suspect. When fencing a child there are FOUR channels: environment, file
 * modes, inherited descriptors, and the arguments.
 *
 * Two layers, because they fail differently:
 *
 *   jc_fd_cloexec()  -- the precise fix, at the point of creation. Scales: a
 *                       descriptor marked here is dropped by every exec,
 *                       including ones added later.
 *   jc_proc_child_close_fds() -- the backstop, in the child. Catches whatever
 *                       the first layer forgot, which -- given that this defect
 *                       existed at eight exec sites -- is the failure mode with
 *                       the track record.
 *
 * See docs/analysis/2026-08-17-source-hardening-audit.md §H2. */

/* Mark `fd` close-on-exec. Cheap, idempotent, and safe on a bad fd (it just
 * fails). O_CLOEXEC is POSIX-2008 and NOT available under this tree's
 * -D_POSIX_C_SOURCE=200112L -- docs/INSTALL.md lists it among the features
 * jichi does not require -- so this uses fcntl(F_SETFD), which is POSIX-2001 and
 * present on every row in docs/PLATFORMS.md. */
void jc_fd_cloexec(int fd);

/* pipe(2) with FD_CLOEXEC on BOTH ends. Use this instead of pipe() -- enforced by
 * tests/smoke/posix_utils_lint.sh, like the popen rule above.
 *
 * Safe for a pipe you are about to hand a child, and the reason is worth knowing:
 * dup2() does NOT copy the close-on-exec flag, so the child's `dup2(fds[1], 1)`
 * leaves fd 1 inheritable while the original high-numbered end is dropped by
 * exec. That is exactly the wanted split.
 *
 * This is the layer that catches what jc_proc_child_close_fds() structurally
 * cannot: a child spawned through popen(), whose fork happens inside libc where
 * no jichi code runs between fork and exec. Measured -- after all eight explicit
 * exec sites were fixed, a model-issued shell run through the popen path STILL
 * held both ends of one of jichi's pipes (M472). */
int jc_pipe_cloexec(int fds[2]);

/* In a just-forked child, AFTER any dup2() onto 0/1/2 and immediately before
 * exec: close every descriptor above stderr. MUST be called only in the child.
 *
 * Ordering matters and is the one way to get this wrong: called before the
 * dup2s, it would close the pipe the child is about to install as its stdout.
 * tests/smoke/child_fds.sh asserts the result rather than the ordering. */
void jc_proc_child_close_fds(void);

/* Default kill deadline (seconds) when `timeout` <= 0. */
#define JC_PROC_DEF_TIMEOUT 30L

/* Run argv (NULL-terminated), writing `stdin_data` (may be NULL/empty) to its
 * stdin and appending combined stdout+stderr to `out`, bounded by `cap` (a
 * "\n... [output truncated]" note is appended when the cap is hit). `env` is an
 * optional jc_vec of char* "KEY=VALUE" applied in the child (NULL for none).
 * Killed after `timeout` seconds or when *abort_flag becomes non-zero
 * (abort_flag may be NULL). Returns the child's exit code, -1 on spawn failure,
 * or -2 on timeout/abort. */
int jc_proc_capture(char *const argv[], const struct jc_vec *env,
                    const char *stdin_data, struct jc_sb *out, jc_size cap,
                    long timeout, volatile int *abort_flag);

/* --- secret env scrubbing (M130) --------------------------------------------
 *
 * A forked child inherits the agent's entire environment, which holds the
 * resolved provider API keys (ANTHROPIC_API_KEY / OPENAI_API_KEY / a configured
 * apiKeyEnv). Without scrubbing, a single model-issued `run_terminal_command`
 * such as `env | grep -i key` reads them directly. These helpers let the child
 * drop those variables before it execs untrusted code. */

/* Register an environment-variable NAME (not value) that holds a secret, so it
 * is removed from the child environment before exec. The name is copied into a
 * bounded static registry; NULL/empty/over-long names and overflow past the cap
 * are ignored, and re-registering a name is a no-op. Called once at startup for
 * each configured apiKeyEnv. */
void jc_proc_secret_env_add(const char *name);

/* In a just-forked child, before exec: unsetenv() every registered secret env
 * name plus the built-in provider defaults. MUST be called only in the child
 * (it mutates the process environment). Where a caller also applies a
 * configured child env, scrub FIRST so an explicitly-set variable is re-added. */
void jc_proc_scrub_secret_env(void);

/* Write a shell prefix (`unset A B C; `) that drops every secret env var, for a
 * command run through popen()/`sh -c` where the child can't call the scrub
 * directly. Writes "" and returns 0 when nothing is registered or it would
 * overflow `cap`; returns 1 when a prefix was written. `buf` is NUL-terminated. */
int jc_proc_secret_env_prefix(char *buf, jc_size cap);

/* Memory-watchdog verdict for a subprocess (M117). */
enum jc_memwatch {
    JC_MEMWATCH_OK = 0,
    JC_MEMWATCH_WARN, /* approaching the budget (>= 80%) */
    JC_MEMWATCH_KILL  /* over the budget (>= 100%) -- terminate it */
};

/* Pure verdict: WARN at >= 80% of `budget_kb`, KILL at >= 100%. A `budget_kb`
 * <= 0 (no budget) is always OK. Unit-tested. */
int jc_memwatch_decision(long rss_kb, long budget_kb);

/* Total resident memory (KB) of every process in group `pgid`, summed from
 * /proc (so a build's compiler children count too). 0 when unavailable
 * (non-Linux / no /proc / none found). Linux/POSIX-only. */
long jc_proc_group_rss_kb(long pgid);

#ifdef __cplusplus
}
#endif
#endif /* JC_PROC_H */
