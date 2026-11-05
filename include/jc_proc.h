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
#include <sys/types.h> /* pid_t: MiNTLib's <unistd.h> declares it only under an X/Open level (M723) */
#include <signal.h>    /* sigaction, sigset_t, SIGCHLD, SIG_DFL, SIG_IGN, SIGINT, SIGTERM, SIGHUP */

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

/* popen(3)'s read half, built over jc_spawn (M832) rather than libc, so the
 * child gets the same rituals as every other child jichi starts -- the SIGPIPE
 * reset (libc popen inherited the agent's SIG_IGN and needed the parent's
 * disposition flipped around the call; no longer), the secret-env scrub, and
 * CLOEXEC pipe ends from birth (libc's popen deliberately cleared the flag on
 * the parent's end, M472) -- and so it exists at all on a fork-less backend.
 * Only mode "r" is supported (no caller has ever written). Close the stream
 * with jc_proc_pclose(), NEVER pclose(3): the FILE* is fdopen'd, and libc's
 * pclose on it is undefined behaviour. posix_utils_lint enforces that too. */
FILE *jc_proc_popen(const char *cmd, const char *mode);

/* Close a jc_proc_popen() stream and reap its child: returns the wait status
 * exactly as pclose(3) would (WIFEXITED/WEXITSTATUS-readable), or -1. */
int jc_proc_pclose(FILE *f);

/* Reap a child with a checked waitpid that retries on EINTR and treats any
 * failure (including ECHILD when SIGCHLD was inherited ignored) as "unknown".
 * Returns 0 on success (status is stored in *status_out if non-NULL), -1 on
 * failure. A reaper failure is UNKNOWN, never a status to decode -- the
 * project rule: a classifier's else-branch must say unknown. */
int jc_proc_waitpid(pid_t pid, int *status_out);

/* Bounded, interrupt-aware reap helper (M989, B-01, B-06, R06-#3).
 *
 * Polls waitpid(pid, &status, WNOHANG) in ~20ms slices. Returns:
 *   0  -- child reaped; *status_out holds the exit status.
 *  -1  -- ECHILD (SIGCHLD was ignored, kernel auto-reaped, no status).
 *  -2  -- deadline exceeded; child was stopped via SIGTERM/SIGKILL.
 *  -3  -- abort flag set; child was stopped via SIGTERM/SIGKILL.
 *
 * "Stop the child" = SIGTERM to the group (kill(-pid)), ~1s grace, SIGKILL,
 * then a final blocking reap via jc_proc_waitpid (the child is being killed,
 * so blocking is acceptable here). The return is -2 or -3 regardless of the
 * final status.
 *
 * deadline_ms == 0 means no time bound, only interrupt-aware.
 * abort_flag may be NULL (no interrupt support).
 *
 * On EINTR during polling: retry. On ECHILD: return -1 (unknown). This is
 * why the M987 teeth hold (SIGCHLD=SIG_IGN -> ECHILD -> -1, not 0). */
int jc_proc_reap_bounded(pid_t pid, double deadline_ms, volatile int *abort_flag,
                         int *status_out);

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

/* The PARENT-side twin of the backstop, for jc_spawn's posix_spawn backend
 * (M832), which has no child-side hook: mark every descriptor above stderr
 * close-on-exec just before the spawn, so nothing jichi OR A LIBRARY holds
 * crosses the exec.
 *
 * MEASURED why the primary layer is not enough: libcurl creates its internal
 * pipe with pipe2(fds, 0) -- no O_CLOEXEC -- inside curl_easy_perform, so
 * jc_fd_cloexec-at-creation structurally cannot reach it (the descriptor is not
 * jichi's). Under posix_spawn both ends reached every model-issued shell
 * (child_fds.sh check 1, found by the M832 NOFORK measurement). The same pipe
 * reached every shell on the OLD libc-popen path too -- installed M831 binary,
 * fds 4 and 5 -- because libc's popen child runs no jichi code either; routing
 * popen through jc_spawn closed that (child_fds.sh check 4).
 *
 * Safe: nothing jichi execs inherits a descriptor above stderr BY DESIGN (the
 * fork backend's backstop closes them all), dup2'd stdio slots are new
 * descriptors without the flag, and CLOEXEC does not affect fork() -- the pool
 * and daemon children keep everything. jichi forks rather than threads; a
 * library thread creating a descriptor mid-sweep could escape it, and none runs
 * between model calls, which is when tools start children. */
void jc_proc_parent_cloexec_all(void);

/* Default kill deadline (seconds) when `timeout` <= 0. */
#define JC_PROC_DEF_TIMEOUT 30L

/* Run argv (NULL-terminated), writing `stdin_data` (may be NULL/empty) to its
 * stdin and appending combined stdout+stderr to `out`, bounded by `cap` (a
 * "\n... [output truncated]" note is appended when the cap is hit). A NULL
 * `out` is a legal discard path: the pipe is still drained so the child
 * cannot block, and the exit code is returned as usual (M929, R06-#1).
 * `env` is an
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
 * registry that grows as needed (M724: it was 32 names of under 128 bytes, and a
 * name past either bound was silently not scrubbed). NULL, empty and invalid
 * names are ignored and re-registering a name is a no-op; both return 0.
 * Returns -1 only when memory ran out -- then the name is NOT registered, and
 * the caller must refuse to run anything rather than fork with it in place.
 * Called at startup for each configured apiKeyEnv. */
int jc_proc_secret_env_add(const char *name);

/* In a just-forked child, before exec: unsetenv() every registered secret env
 * name plus the built-in provider defaults. MUST be called only in the child
 * (it mutates the process environment). Where a caller also applies a
 * configured child env, scrub FIRST so an explicitly-set variable is re-added. */
void jc_proc_scrub_secret_env(void);

/* Write a shell prefix (`unset A B C; `) that drops every secret env var, for a
 * command run through popen()/`sh -c` where the child can't call the scrub
 * directly. Returns 1 when the prefix was written, 0 when there is nothing to
 * drop, and -1 when it does not fit `cap` or memory ran out -- then `buf` is ""
 * and the caller must NOT run the command. Until M724 "does not fit" answered 0,
 * like "nothing to drop", and the popen path ran the command with every key in
 * place. `buf` is NUL-terminated. Size it with jc_proc_secret_env_prefix_size. */
int jc_proc_secret_env_prefix(char *buf, jc_size cap);

/* The bytes jc_proc_secret_env_prefix needs for the current registry, counting
 * the NUL (M724). */
jc_size jc_proc_secret_env_prefix_size(void);

/* How many configured names the registry holds, and a way back to an earlier
 * count, freeing what was added after it. Exposed for tests, which share one
 * process-wide registry and must leave it as they found it (M724). */
int jc_proc_secret_env_count(void);
void jc_proc_secret_env_truncate(int n);

/* Whether the first `name_len` bytes of `name` are a secret env-var NAME --
 * a registered apiKeyEnv, a configured secretEnv name, or a built-in default
 * (provider keys plus the M1043-a CI/forge names); exactly the set the
 * child-side scrub drops. For jc_spawn's posix_spawn backend (M832), which
 * cannot run the scrub in a child and builds the child's envp in the parent
 * instead: one predicate, owned here beside the lists, so the two scrub paths
 * cannot drift. */
int jc_proc_env_name_is_secret(const char *name, jc_size name_len);

/* M1043-a: doctor's suffix scan. Walk the process environment and fill `found`
 * (an array of max_found 128-byte name slots, written NUL-terminated) with the
 * NAMES that are set and non-empty, end in one of the case-sensitive suffixes
 * _TOKEN, _KEY, _SECRET or _PASSWORD, and are in NEITHER the built-in list nor
 * the registered set (not listed by jc_proc_env_name_is_secret). The suffix
 * list lives here, beside the lists it is judged against. Returns how many
 * names were written (max_found when there are more), or -1 when `found` is
 * NULL or max_found is not positive. Names that a list already holds never
 * appear: the warning is for a credential jichi does not know. */
int jc_proc_secret_env_scan(char found[128][128], int max_found);

/* --- the parent's own /proc entries (M770) ------------------------------------
 *
 * The scrub above clears the CHILD's environment, and that turned out to be half
 * of the fence. The child runs as the same user as jichi, and Linux lets a
 * same-user process read /proc/<pid>/environ of any process that is DUMPABLE --
 * every ordinary process is. jichi's own environment still holds the key, since
 * that is where jichi read it, so one model-issued command got it back:
 *
 *   tr '\0' '\n' < /proc/$PPID/environ | grep KEY
 *
 * MEASURED 2026-09-25, on the host and in a rootless container, under Yama
 * ptrace_scope 1 -- which restricts attaching, not this weaker read. The same
 * route let a child write into jichi's stdout through /proc/$PPID/fd/1. It is a
 * FIFTH channel next to the four above (environment, file modes, descriptors,
 * arguments): the parent's own /proc entries. See
 * docs/analysis/2026-08-17-source-hardening-audit.md §H2.
 *
 * jc_proc_harden_environ() makes jichi non-dumpable (prctl PR_SET_DUMPABLE 0).
 * The kernel then makes /proc/<pid>/{environ,fd,maps,mem,...} root-owned and
 * refuses them to other processes of the same user. It resets the flag at
 * every exec of a non-setuid program, so the children jichi starts are
 * dumpable, and readable, as before. MEASURED what jichi keeps of ITSELF: its
 * own /proc/self/{exe,stat,status}, which are all it reads of itself, still
 * read, and so do fd/ and maps. Its own environ and auxv do NOT (0400,
 * root-owned, no self exemption): a future feature that reads them breaks
 * here, and now knowably.
 *
 * What it costs: jichi itself leaves no core dump, and a same-user debugger
 * cannot attach to it. JICHI_KEEP_DUMPABLE=1 (any yes of jc_bool_from_word)
 * skips the call for exactly those cases, and doctor says so; anything else,
 * a typo included, keeps it on.
 *
 * Linux-only, behind the HAVE_SET_DUMPABLE probe (JC_HAVE_SET_DUMPABLE). Where
 * it is absent the call records UNAVAILABLE and does nothing, and doctor says
 * so: the class may exist there by another route, unmeasured.
 *
 * Called once, first thing in main(), so before any child can exist and for
 * every subcommand. tests/smoke/proc_key_hidden.sh drives it. */
/* OpenBSD, NetBSD and illumos have no dumpable/trace-control flag; the in-place
 * environ scrub (jc_proc_harden_secret_env) is their partial substitute. Pure
 * libc, so no Makefile probe -- gated on the compiler's own platform macro. Extend
 * the condition only for a platform MEASURED to (a) expose env via `ps`, `pargs`
 * or /proc/<pid>/environ and (b) hide it after the scrub, as OpenBSD (M796),
 * NetBSD (M798) and illumos (M799) were. On NetBSD (procfs mounted) and illumos,
 * /proc/<pid>/fd and /proc/<pid>/{mem,as} stay open and no facility there closes
 * them -- HARDENING.md §2. */
#if defined(__OpenBSD__) || defined(__NetBSD__) || (defined(__sun) && defined(__SVR4))
#define JC_HAVE_ENVIRON_SCRUB 1
#endif

enum jc_harden {
    JC_HARDEN_NOT_RUN = 0,  /* jc_proc_harden_environ() never ran: a bug */
    JC_HARDEN_ON,           /* non-dumpable: hidden from same-user processes */
    JC_HARDEN_OFF_OPTOUT,   /* JICHI_KEEP_DUMPABLE kept the process dumpable */
    JC_HARDEN_UNAVAILABLE,  /* built without PR_SET_DUMPABLE (not Linux) */
    JC_HARDEN_FAILED,       /* prctl refused, or the flag did not read back 0 */
    JC_HARDEN_ON_PARTIAL    /* environ scrubbed: `ps -e`/`/proc/environ` closed,
                             * ptrace (and, on a procfs NetBSD, /proc fd+mem) NOT
                             * (OpenBSD/NetBSD -- no PR_SET_DUMPABLE, no procctl) */
};

void jc_proc_harden_environ(void);

/* OpenBSD (M796): a same-user `ps -e` reads a process's LIVE environment off its
 * user stack, so it shows jichi's API key -- and OpenBSD has neither
 * PR_SET_DUMPABLE nor procctl PROC_TRACE_CTL to make the process unreadable.
 * What works, measured on OpenBSD 7.9: overwrite the registered secret env
 * strings in place, so `ps -e` reads the zeroed bytes. This is a PARTIAL guard:
 * it closes the `ps -e` route (the trivial one a model-issued shell command
 * takes) but NOT ptrace -- a debugger can still read jichi's in-memory key copy,
 * which no OpenBSD facility can prevent. Call it AFTER the secret registry is
 * armed and the config's keys are resolved (every key is read from getenv() into
 * the config at load, and nothing re-reads it after, so scrubbing is safe).
 * JICHI_KEEP_DUMPABLE=1 skips it, as with the dumpable flag. It records
 * JC_HARDEN_ON_PARTIAL (or OFF_OPTOUT); where JC_HAVE_ENVIRON_SCRUB is not
 * defined it does nothing. tests/smoke/proc_key_hidden.sh drives it. */
void jc_proc_harden_secret_env(void);

/* The state jc_proc_harden_environ() left, as an enum jc_harden. Five states,
 * not three, so that doctor's sentence is true in each: a build WITH the probe
 * whose call never ran used to be reported as a build without it -- the M693
 * class, a diagnostic contradicting the build it describes (seen at M770 by
 * removing the call). */
int jc_proc_harden_state(void);

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
