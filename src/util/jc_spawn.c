/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_spawn.c - start a program, with or without fork() (see jc_spawn.h).
 *
 * The fork backend is the eight call sites' historical child ritual, moved, not
 * changed: every line below has an M-number on it in jc_proc.h. The posix_spawn
 * backend expresses the same contract parent-side. Keep the two in step BY
 * CONTRACT, not by eye: tests/smoke/spawn_backend.sh runs the load-bearing
 * fences (the marker, the scrub, the pipeline group-kill, stdin feeding) against
 * BOTH backends of one binary, so a ritual present in one and missing in the
 * other goes red in the gate rather than on a fork-less port. */

#include "jc_spawn.h"
#include "jc_proc.h"
#include "jc_vec.h"
#include "jc_log.h"
#include "jc_path.h"   /* JC_PATH_MAX for the cwd round-trip */
#include "jc_fcntl.h"  /* <fcntl.h> + JC_O_BINARY (portability_lint 38: file opens) */
#include "jc_fault.h"  /* M1116 (B-09): the cwd-restore vanish fault */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>   /* pid_t: MiNTLib's <unistd.h> hides it below X/Open (M723) */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#if defined(JC_HAVE_POSIX_SPAWN)
#include <spawn.h>
#include <signal.h>
extern char **environ;
#endif

/* M1116 (B-09): fchdir() is not declared under jichi's flags (-std=c89
 * -D_POSIX_C_SOURCE=200112L); glibc hides it behind _DEFAULT_SOURCE /
 * _XOPEN_SOURCE>=700. The Makefile JC_HAVE_FCHDIR probe (a LINK probe, mirroring
 * the pid_t note at line 23) sets the macro only when the symbol actually
 * resolves in libc, so a local extern declaration -- not a hidden prototype --
 * is what makes the call below compile. It is load-bearing: under the build's
 * -Werror an implicit declaration of fchdir would fail the build. */
#ifdef JC_HAVE_FCHDIR
extern int fchdir(int);
#endif

/* --- backend selection ------------------------------------------------------
 *
 * Resolved once per process. The default is fork wherever fork exists, because
 * that is the backend every row's evidence was gathered on; "spawn" (or
 * "posix"/"posix_spawn") in JICHI_SPAWN_BACKEND selects the other. Any other
 * word -- a typo included -- keeps the default, which is the safe direction: a
 * misspelt request falls back to the measured path, never to an unmeasured one.
 * On a build without the probe the answer is always fork, whatever the env says
 * (doctor reports which). */
/* File-scope so a JC_FAULT-only seam below can re-resolve it (the unit test
 * for the cwd-restore wants the posix_spawn backend in a process whose earlier
 * calls already resolved it to fork). In production it is touched once, at the
 * first resolution, exactly as before. */
#if defined(JC_HAVE_POSIX_SPAWN)
static int s_backend_resolved = -1;
#endif

int jc_spawn_backend(void)
{
#if defined(JC_HAVE_POSIX_SPAWN)
    if (s_backend_resolved < 0) {
        const char *w = getenv("JICHI_SPAWN_BACKEND");
        s_backend_resolved = (w != NULL &&
                              (strcmp(w, "spawn") == 0 ||
                               strcmp(w, "posix") == 0 ||
                               strcmp(w, "posix_spawn") == 0)) ? 1 : 0;
    }
    return s_backend_resolved;
#else
    return 0;
#endif
}

#ifdef JC_FAULT
/* M1116 (B-09): a test seam, NOT a production API. The posix_spawn backend is
 * selected once per process (above), but the unit suite's earlier tests --
 * search_code through jc_proc_popen -- have already resolved it to the default
 * (fork) by the time test_spawn runs, and jichi is single-threaded before a
 * fork so nothing else can interleave. Re-arming lets the test force the spawn
 * backend against the same binary, in-process, with no second driver. Compiled
 * to nothing in release builds (JC_FAULT), so it carries no production surface
 * and no public declaration (the test TU names it with its own extern). */
void jc_spawn_backend_reset(void);
void jc_spawn_backend_reset(void)
{
#if defined(JC_HAVE_POSIX_SPAWN)
    s_backend_resolved = -1;
#endif
}
#endif

/* Apply `env` ("KEY=VALUE" entries) in the CHILD process, after the scrub, so a
 * configured key wins over it. This moved here from jc_proc.c / jc_mcp_stdio.c,
 * which carried identical copies. */
static void child_apply_env(const struct jc_vec *env)
{
    jc_size i;
    if (env == NULL) {
        return;
    }
    for (i = 0; i < env->len; i++) {
        const char *kv = *(char **)jc_vec_at((struct jc_vec *)env, i);
        const char *eq = (kv != NULL) ? strchr(kv, '=') : NULL;
        if (eq != NULL) {
            char key[256];
            jc_size kn = (jc_size)(eq - kv);
            if (kn < sizeof(key)) {
                memcpy(key, kv, kn);
                key[kn] = '\0';
                setenv(key, eq + 1, 1);
            }
        }
    }
}

/* Wire one stdio slot in the forked child. A DEVNULL slot opens its own
 * /dev/null with the direction the slot implies; a source fd is dup2'd (the
 * CLOEXEC original is dropped at exec, the copy is not -- the wanted split). */
static void child_wire_fd(int want, int slot)
{
    if (want == JC_SPAWN_INHERIT) {
        return;
    }
    if (want == JC_SPAWN_DEVNULL) {
        int dn = open("/dev/null", (slot == 0) ? O_RDONLY : O_WRONLY);
        if (dn >= 0) {
            dup2(dn, slot);
            if (dn != slot) {
                close(dn);
            }
        }
        return;
    }
    if (want >= 0 && want != slot) {
        dup2(want, slot);
    }
}

/* The historical ritual, in the one order that satisfies every constraint the
 * eight sites accumulated: pgroup first (so a parent kill(-pid) can never miss
 * the pipeline, M461), then the fd wiring, then cwd (the envelope's _exit(126)
 * contract), then scrub-before-configured-env (M130: an explicit key is
 * re-added), then the close-fds backstop (AFTER the dup2s, M472), then the
 * SIGPIPE reset (M461), then exec. */
static void child_run(const struct jc_spawn_req *req)
{
    if (req->new_pgroup) {
        setpgid(0, 0);
    }
    child_wire_fd(req->fd_stdin,  0);
    child_wire_fd(req->fd_stdout, 1);
    child_wire_fd(req->fd_stderr, 2);
    if (req->cwd != NULL && req->cwd[0] != '\0') {
        if (chdir(req->cwd) != 0) {
            _exit(126);
        }
    }
    if (req->scrub_env) {
        jc_proc_scrub_secret_env();
    }
    child_apply_env(req->env);
    /* The backstop and the reset sit IMMEDIATELY before each exec, per branch:
     * posix_utils_lint checks 6/6c hold them adjacent by position, so a new
     * exec cannot drift away from either. */
    if (req->argv != NULL) {
        jc_proc_child_close_fds(); /* M472: not our fds */
        jc_proc_child_sigreset();  /* M461: not our SIG_IGN */
        execvp(req->argv[0], req->argv);
    } else if (req->shell_cmd != NULL) {
        jc_proc_child_close_fds(); /* M472 */
        jc_proc_child_sigreset();  /* M461 */
        execl(jc_shell_path(), "sh", "-c", req->shell_cmd, (char *)NULL);
    }
    _exit(127);
}

static jc_status spawn_fork(const struct jc_spawn_req *req, pid_t *pid_out)
{
    pid_t pid = fork();
    if (pid < 0) {
        return JC_ERR_IO;
    }
    if (pid == 0) {
        child_run(req);
        /* not reached: child_run exits or execs */
    }
    if (req->new_pgroup) {
        /* Parent side too, racing the child's own setpgid harmlessly -- both
         * target the same pgid -- so a kill(-pid) issued before the child has
         * run cannot miss it. */
        setpgid(pid, pid);
    }
    *pid_out = pid;
    return JC_OK;
}

/* --- the posix_spawn backend ------------------------------------------------ */

char **jc_spawn_build_envp(int scrub, const struct jc_vec *env)
{
#if defined(JC_HAVE_POSIX_SPAWN)
    jc_size n_env = 0;
    jc_size n_extra = (env != NULL) ? env->len : 0;
    jc_size i;
    jc_size out_n = 0;
    char **out;

    while (environ != NULL && environ[n_env] != NULL) {
        n_env++;
    }
    out = (char **)malloc((n_env + n_extra + 1) * sizeof(char *));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n_env; i++) {
        char *entry = environ[i];
        const char *eq = strchr(entry, '=');
        jc_size name_len = (eq != NULL) ? (jc_size)(eq - entry)
                                        : (jc_size)strlen(entry);
        int drop = 0;
        jc_size j;
        if (scrub && jc_proc_env_name_is_secret(entry, name_len)) {
            drop = 1;
        }
        /* A configured entry REPLACES its key (setenv overwrite=1 in the
         * child ritual), so the inherited copy is dropped here and the
         * configured one appended below. */
        for (j = 0; !drop && j < n_extra; j++) {
            const char *kv = *(char **)jc_vec_at((struct jc_vec *)env, j);
            if (kv != NULL && strncmp(kv, entry, name_len) == 0 &&
                kv[name_len] == '=') {
                drop = 1;
            }
        }
        if (!drop) {
            out[out_n++] = entry;
        }
    }
    for (i = 0; i < n_extra; i++) {
        char *kv = *(char **)jc_vec_at((struct jc_vec *)env, i);
        if (kv != NULL && strchr(kv, '=') != NULL) {
            out[out_n++] = kv;
        }
    }
    out[out_n] = NULL;
    return out;
#else
    (void)scrub;
    (void)env;
    return NULL;
#endif
}

#if defined(JC_HAVE_POSIX_SPAWN)
static jc_status spawn_posix(const struct jc_spawn_req *req, pid_t *pid_out)
{
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    sigset_t sigdfl;
    short flags = POSIX_SPAWN_SETSIGDEF;
    char **envp;
    char cwd_save[JC_PATH_MAX];
    int have_cwd_save = 0;
    int dirfd = -1;   /* M1116 (B-09): held open on the ORIGINAL directory */
    int rc;
    pid_t pid;

    if (posix_spawn_file_actions_init(&fa) != 0) {
        return JC_ERR_IO;
    }
    if (posix_spawnattr_init(&at) != 0) {
        posix_spawn_file_actions_destroy(&fa);
        return JC_ERR_IO;
    }

    /* The fd wiring, as file actions. Order per slot mirrors child_wire_fd. */
    {
        int slot;
        int wants[3];
        wants[0] = req->fd_stdin;
        wants[1] = req->fd_stdout;
        wants[2] = req->fd_stderr;
        for (slot = 0; slot < 3; slot++) {
            int want = wants[slot];
            if (want == JC_SPAWN_INHERIT) {
                continue;
            }
            if (want == JC_SPAWN_DEVNULL) {
                rc = posix_spawn_file_actions_addopen(&fa, slot, "/dev/null",
                        (slot == 0) ? O_RDONLY : O_WRONLY, 0);
            } else {
                rc = posix_spawn_file_actions_adddup2(&fa, want, slot);
            }
            if (rc != 0) {
                goto fail_actions;
            }
        }
    }

    /* SIGPIPE back to default (M461): exec preserves an IGNORED disposition,
     * and jichi ignores SIGPIPE, so without this every pipeline the child runs
     * would have producers that no longer die when their consumer exits. */
    sigemptyset(&sigdfl);
    sigaddset(&sigdfl, SIGPIPE);
    if (posix_spawnattr_setsigdefault(&at, &sigdfl) != 0) {
        goto fail_actions;
    }
    if (req->new_pgroup) {
        flags |= POSIX_SPAWN_SETPGROUP;
        if (posix_spawnattr_setpgroup(&at, 0) != 0) {
            goto fail_actions;
        }
    }
    if (posix_spawnattr_setflags(&at, flags) != 0) {
        goto fail_actions;
    }

    envp = jc_spawn_build_envp(req->scrub_env, req->env);
    if (envp == NULL) {
        goto fail_actions;
    }

    /* cwd: spawn has no portable chdir action, and jichi forks rather than
     * threads, so a parent-side round-trip is unobservable by anything else in
     * the process. A cwd that will not enter fails the START here (the fork
     * backend's child would _exit(126) instead -- both honest, per backend).
     *
     * M1116 (B-09): before we chdir away, hold an open descriptor on the
     * ORIGINAL directory. A chdir by saved PATH string cannot return to a
     * directory that vanished (or was renamed away) mid-call -- jichi would
     * then run on in a silently-moved cwd. An open dirfd keeps the inode alive
     * and fchdir() back to it succeeds regardless of what the PATH now says.
     * The getcwd(cwd_save) is kept as the fallback the restore names in its
     * warn, and as the path the FAULT test vanishes. */
    if (req->cwd != NULL && req->cwd[0] != '\0') {
        if (getcwd(cwd_save, sizeof cwd_save) != NULL) {
            have_cwd_save = 1;
        }
        {
            int dflags = O_RDONLY;
            /* M1116-b (B-09): O_DIRECTORY is not in the base C89/POSIX-200112
             * set (same reason the tree's existing guard at
             * src/platform/jc_platform_posix.c:674 exists); plain O_RDONLY on
             * a directory is the portable minimum, O_DIRECTORY only reinforces
             * open() refusing a regular file. */
#ifdef O_DIRECTORY
            dflags |= O_DIRECTORY;
#endif
            dirfd = open(".", dflags | JC_O_BINARY);
        }
        if (chdir(req->cwd) != 0) {
            if (dirfd >= 0) {
                close(dirfd);
                dirfd = -1;
            }
            free(envp);
            goto fail_actions;
        }
    }

    /* The backstop, parent-side (jc_proc.h): libcurl's internal pipe is not
     * CLOEXEC, and a spawn call has no child-side hook to close it. */
    jc_proc_parent_cloexec_all();
    if (req->argv != NULL) {
        rc = posix_spawnp(&pid, req->argv[0], &fa, &at, req->argv, envp);
    } else if (req->shell_cmd != NULL) {
        char *shargv[4];
        shargv[0] = (char *)"sh";
        shargv[1] = (char *)"-c";
        shargv[2] = (char *)req->shell_cmd;
        shargv[3] = NULL;
        rc = posix_spawn(&pid, jc_shell_path(), &fa, &at, shargv, envp);
    } else {
        rc = EINVAL;
    }

#ifdef JC_FAULT
    /* M1116 (B-09) fault site, at the RESTORE: model 'the original directory
     * vanished mid-call' by renaming its PATH out of the way (the same-dir
     * rename is atomic, and it is exactly what leaves the old PATH gone). It
     * can only fire when the test armed JICHI_FAULT_SPAWN_RESTORE_VANISH_AFTER,
     * so an un-armed FAULT=1 build -- and every release build (JC_FAULT_HIT
     * expands to 0) -- is untouched. The dirfd still names the inode; only the
     * path string is gone, which is the half the path-based restore loses. */
    if (dirfd >= 0 && jc_fault_hit(JC_FAULT_SPAWN_RESTORE_VANISH)) {
        char vanished[JC_PATH_MAX];
        size_t slen = strlen(cwd_save);
        /* cwd_save is absolute (getcwd's result) and leaves room for the
         * suffix plus the terminator. */
        if (slen > 0 && cwd_save[0] == '/' && slen + 11 < sizeof vanished) {
            memcpy(vanished, cwd_save, slen + 1);
            memcpy(vanished + slen, ".vanished", 9);
            vanished[slen + 9] = '\0';
            (void)rename(cwd_save, vanished);
            /* If the rename itself failed the fault did not take: the
             * directory did not vanish and the restore succeeds. Since
             * M1116-c that is no longer a green suite -- the teeth assert
             * (under JC_HAVE_FCHDIR) that after the spawn stat(orig) FAILS
             * and stat(orig_vanished) SUCCEEDS, and a failed rename leaves
             * the original path present and the renamed one never created,
             * so those checks go RED instead of the
             * fault silently not modelling a vanish. */
        }
    }
#endif

    /* M1116 (B-09): restore the original working directory by HANDLE. The
     * dirfd held open above (opened before the chdir away) names the original
     * directory's inode; fchdir(dirfd) returns to it even if the directory's
     * PATH was renamed away mid-call -- an open fd keeps the inode alive, so
     * 'the original directory vanished mid-call' can no longer leave jichi
     * running in a silently-moved cwd. Where fchdir is unavailable (no
     * JC_HAVE_FCHDIR), or the open above failed (dirfd < 0), fall back to the
     * old path-based chdir(cwd_save): its change-back CAN fail on a vanished
     * path, so it keeps its warn-on-failure -- the honest half of the fix. */
    if (req->cwd != NULL && req->cwd[0] != '\0') {
        int restored = 0;
#ifdef JC_HAVE_FCHDIR
        if (dirfd >= 0) {
            /* M1116-b (B-09): load-bearing -- the intervening
             * jc_proc_parent_cloexec_all() backstop above CLOEXECs rather
             * than closes fd 3..maxfd (jc_proc.h:180-185, jc_proc.c:651), so
             * dirfd is still open here; that is why the hold-and-fchdir
             * restore works. */
            restored = (fchdir(dirfd) == 0);
            if (!restored) {
                /* Effectively unreachable -- the fd is ours and open -- but
                 * never silent: a moved cwd would misdirect every later
                 * relative path. */
                jc_logf(JC_LOG_WARN,
                        "spawn: fchdir back to the held cwd fd failed");
            }
        }
#endif
        if (!restored && have_cwd_save) {
            if (chdir(cwd_save) != 0) {
                /* Only possible when the old cwd vanished mid-call. Say so: a
                 * silently moved cwd would misdirect every later relative
                 * path. */
                jc_logf(JC_LOG_WARN, "spawn: could not return to %s", cwd_save);
            }
        }
    }
    if (dirfd >= 0) {
        close(dirfd);
        dirfd = -1;
    }
    free(envp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (rc != 0) {
        return JC_ERR_IO;
    }
    if (req->new_pgroup) {
        setpgid(pid, pid); /* parent-side reinforcement, as on the fork path */
    }
    *pid_out = pid;
    return JC_OK;

fail_actions:
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    return JC_ERR_IO;
}
#endif /* JC_HAVE_POSIX_SPAWN */

jc_status jc_spawn(const struct jc_spawn_req *req, pid_t *pid_out)
{
    if (req == NULL || pid_out == NULL ||
        (req->argv == NULL && req->shell_cmd == NULL)) {
        return JC_ERR_INVALID;
    }
#if defined(JC_HAVE_POSIX_SPAWN)
    if (jc_spawn_backend() == 1) {
        return spawn_posix(req, pid_out);
    }
#endif
    return spawn_fork(req, pid_out);
}
