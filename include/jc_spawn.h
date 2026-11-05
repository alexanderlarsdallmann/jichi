/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_spawn.h - start a program, with or without fork() (M832, closes DEFERRED
 * def-33).
 *
 * M769 measured that eight of jichi's ten fork() sites fork only to RUN ANOTHER
 * PROGRAM -- the shell and run_tests, verifiers, git, background commands, MCP
 * stdio servers, language servers, and jc_proc's captured commands -- and that a
 * spawn call could replace every one of them on the fork-less rows (AROS/AmigaOS,
 * FreeDOS+DJGPP, NuttX). This is that call: the eight sites and jc_proc_popen()
 * now describe the child they want in a jc_spawn_req and jc_spawn() starts it,
 * so "how a child is made" lives in exactly one translation unit. The two sites
 * that fork jichi's OWN code -- the parallel pool and the daemon worker -- are
 * deliberately not callers (def-34): no spawn call replaces a fork that does not
 * exec.
 *
 * Two backends, chosen at RUN time where both exist:
 *
 *   fork        the default everywhere fork() exists: fork, then the child-side
 *               ritual exactly as the eight sites always did it (dup2 wiring,
 *               setpgid, secret-env scrub, configured env, the close-fds
 *               backstop, the SIGPIPE reset -- see jc_proc.h for why each
 *               exists), then execvp / the shell.
 *   posix_spawn compiled when the Makefile probe finds <spawn.h>
 *               (JC_HAVE_POSIX_SPAWN); selected by JICHI_SPAWN_BACKEND=spawn.
 *               The same contract, expressed parent-side: dup2/open file
 *               actions, POSIX_SPAWN_SETPGROUP, POSIX_SPAWN_SETSIGDEF for
 *               SIGPIPE, and an envp built BEFORE the spawn (environ minus the
 *               registered secrets, plus the configured entries). It exists so
 *               the fork-less rows are a backend choice rather than a port, and
 *               it is exercised on every gate run by
 *               tests/smoke/spawn_backend.sh, which flips the env var against
 *               the normal binary.
 *
 * The one child-side layer a spawn call cannot express is the close-fds
 * BACKSTOP (jc_proc_child_close_fds): the primary layer -- every descriptor
 * jichi creates is CLOEXEC at creation (jc_fd_cloexec / jc_pipe_cloexec, M472)
 * -- carries the fence alone under the spawn backend, and
 * tests/smoke/child_fds.sh asserts the RESULT, so a leak would be seen, not
 * assumed absent. Exec failure also differs in shape, honestly: the fork
 * backend's child _exit(127)s (126 for a cwd that will not enter), so the
 * caller sees a child that ran and failed; posix_spawn refuses BEFORE a child
 * exists, so the caller sees a start failure. Both are real descriptions of
 * what happened on that backend. */
#ifndef JC_SPAWN_H
#define JC_SPAWN_H

#include "jc_platform.h"

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct jc_vec; /* jc_vec.h */

/* Sentinels for the three stdio slots. A value >= 0 is a descriptor to dup2
 * onto that slot (the same fd may serve stdout and stderr -- the everyday
 * "2>&1" wiring). The source fd should be CLOEXEC (jc_pipe_cloexec), so the
 * child keeps only the dup2'd copy -- dup2 does not carry the flag, which is
 * exactly the wanted split (jc_proc.h, M472). */
#define JC_SPAWN_INHERIT (-1)   /* leave the slot as jichi's own            */
#define JC_SPAWN_DEVNULL (-2)   /* open /dev/null onto the slot             */

/* What to run and the fence posture around it. memset to zero, then set what
 * differs; a zeroed struct is "inherit everything, no pgroup, no scrub" --
 * every FENCE here is opt-in per site, because the eight call sites carried
 * different postures when this was factored out (git and the LSP never
 * scrubbed; only the shell/capture/bg children lead a process group) and a
 * refactor must move code, not policy. */
struct jc_spawn_req {
    char *const *argv;      /* program + args, NULL-terminated, PATH-searched.
                             * NULL => run `shell_cmd` as jc_shell_path() -c   */
    const char  *shell_cmd; /* used only when argv is NULL                    */
    int fd_stdin;           /* JC_SPAWN_INHERIT / JC_SPAWN_DEVNULL / an fd    */
    int fd_stdout;
    int fd_stderr;
    int new_pgroup;         /* child leads its own process group, so a kill of
                             * -pid takes the whole pipeline (M461); set on the
                             * parent side too, racing the child harmlessly    */
    int scrub_env;          /* drop the registered secret env names (M130)
                             * before any `env` entry is applied              */
    const struct jc_vec *env; /* of char* "KEY=VALUE": applied after the scrub,
                             * so an explicitly configured key wins (NULL ok) */
    const char *cwd;        /* start the child here; NULL inherits. On the fork
                             * backend a cwd that will not chdir() makes the
                             * child _exit(126) (the envelope's contract); on
                             * the spawn backend it fails the start instead    */
};

/* Start the child. On success *pid_out is the child's pid and the caller owns
 * the usual parent duties unchanged: close its ends of any pipes it wired in,
 * waitpid, and -- when new_pgroup -- kill(-pid) to stop the pipeline. Returns
 * JC_ERR_IO when no child could be started (fork or posix_spawn refused); the
 * request's descriptors are untouched either way, so the caller's cleanup is
 * the same as it was around fork(). */
jc_status jc_spawn(const struct jc_spawn_req *req, pid_t *pid_out);

/* Which backend jc_spawn() will use, resolved once per process from
 * JICHI_SPAWN_BACKEND: 1 when "spawn" (or "posix"/"posix_spawn") selected the
 * posix_spawn backend AND the build carries it, else 0 (fork). Exposed so
 * doctor can say which fence layers are active and so a driver can assert the
 * flip took. */
int jc_spawn_backend(void);

/* The envp the posix_spawn backend passes: a NULL-terminated array over the
 * current environment, minus every entry whose NAME the secret registry knows
 * (when `scrub`), with each `env` "KEY=VALUE" replacing its key or appended.
 * The array is malloc'd and owned by the caller (free the ARRAY only: entries
 * point into environ / `env`); NULL only when memory ran out. Exposed for the
 * unit suite -- the scrub is a fence, so its builder is tested directly, not
 * only through a live child. */
char **jc_spawn_build_envp(int scrub, const struct jc_vec *env);

#ifdef __cplusplus
}
#endif
#endif /* JC_SPAWN_H */
