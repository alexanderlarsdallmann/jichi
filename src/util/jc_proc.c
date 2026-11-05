/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_proc.c - child process capture (see jc_proc.h). */

/* FreeBSD's <sys/procctl.h> (PROC_TRACE_CTL, M771) uses u_int and other BSD
 * types that our global -D_POSIX_C_SOURCE=200112L hides (__BSD_VISIBLE 0). Make
 * them visible for THIS translation unit only -- measured on FreeBSD 15.1: it
 * compiles clean under -std=c89 -pedantic -Wall -Wextra -Werror and the call
 * works. Must precede every include, so <sys/cdefs.h> honours it. Linux and the
 * other platforms never define __FreeBSD__, so nothing changes there. */
#if defined(__FreeBSD__)
#define __BSD_VISIBLE 1
#endif

#include "jc_proc.h"
#include "jc_spawn.h"   /* M832: children are described, not forked, here */
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "jc_select.h"  /* M834: <sys/select.h> where it exists */
#include <sys/time.h> /* struct timeval: MiNTLib's <sys/select.h> only forward-declares it (M723) */
#ifdef JC_HAVE_SET_DUMPABLE
#include <sys/prctl.h> /* PR_SET_DUMPABLE (M770); the Makefile probes it */
#endif
#ifdef JC_HAVE_PROC_TRACE_CTL
#include <sys/procctl.h> /* PROC_TRACE_CTL (M771, FreeBSD); the Makefile probes it */
#endif

/* --- secret env scrubbing (M130) ------------------------------------------ */

/* M724: the registry GROWS. It was 32 names of under 128 bytes, and a name past
 * either bound was ignored -- so a 33rd configured key variable reached every
 * child jichi forks. Every valid name is kept now, and the only failure left is
 * memory, which the caller turns into a refusal to start
 * (tests/smoke/secret_env_many_names.sh checks 4-5). */
static char **g_secret_env;
static int    g_secret_env_n;
static int    g_secret_env_cap;

/* Well-known provider key vars are always dropped, even when unconfigured here,
 * so a stray export in the parent's shell can't leak either.
 *
 * M608: jichi's OWN two names lead the list. Thirteen third-party names were
 * here and neither of the names the setup wizard, the scaffolder and the HRZ
 * onboarding actually write -- so the "stray export" promise above held for
 * every provider's key except the one this project's users have (measured:
 * both reached a shell tool's child, tests/smoke/secret_env_subcommands.sh).
 * tests/smoke/secret_env_lint.sh pins every apiKeyEnv default the tree ships
 * to a row here.
 *
 * M1043-a (plan 2026-10-03, 4.2; def-329's 4.2 half): the four built-in CI and
 * forge names join the list -- dropped even when unconfigured, the M608 shape,
 * because a CI job token in the environment is the measured probe B exposure
 * (docs/internal/2026-10-03-headless-credentials.md): it reached the model's
 * shell while the configured key was scrubbed. Seventeen third-party names now,
 * and every one is a NAME the child cannot need legitimately under its own
 * identity (the model key arrives on the request's Authorization header, not
 * through the child's environment). */
static const char *const g_secret_env_builtin[] = {
    "JICHI_API_KEY", "JLU_API_KEY",
    "ANTHROPIC_API_KEY", "OPENAI_API_KEY", "OPENROUTER_API_KEY",
    "GEMINI_API_KEY", "GOOGLE_API_KEY", "GROQ_API_KEY", "MISTRAL_API_KEY",
    "DEEPSEEK_API_KEY", "TOGETHER_API_KEY", "FIREWORKS_API_KEY",
    "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY",
    "CI_JOB_TOKEN", "GITLAB_TOKEN", "GITHUB_TOKEN", "GH_TOKEN", NULL
};

int jc_proc_secret_env_add(const char *name)
{
    int i;
    char *copy;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    /* A POSIX env-var name is [A-Za-z_][A-Za-z0-9_]*. Reject anything else so a
     * config-supplied apiKeyEnv can never inject shell metacharacters into the
     * unset-prefix built for the popen path. M326e moved the test to the shared
     * jc_envvar_name_valid so this refusal, doctor's lint and the setup
     * wizard's validation cannot drift apart -- they are three consequences of
     * one rule. */
    if (!jc_envvar_name_valid(name)) {
        return 0;
    }
    for (i = 0; i < g_secret_env_n; i++) {
        if (strcmp(g_secret_env[i], name) == 0) {
            return 0; /* already registered */
        }
    }
    if (g_secret_env_n == g_secret_env_cap) {
        int ncap = (g_secret_env_cap == 0) ? 16 : g_secret_env_cap * 2;
        char **grown = (char **)realloc(g_secret_env, (size_t)ncap * sizeof(char *));
        if (grown == NULL) {
            return -1;
        }
        g_secret_env = grown;
        g_secret_env_cap = ncap;
    }
    copy = jc_strdup(name);
    if (copy == NULL) {
        return -1;
    }
    g_secret_env[g_secret_env_n++] = copy;
    return 0;
}

void jc_proc_scrub_secret_env(void)
{
    int i;
    for (i = 0; g_secret_env_builtin[i] != NULL; i++) {
        unsetenv(g_secret_env_builtin[i]);
    }
    for (i = 0; i < g_secret_env_n; i++) {
        unsetenv(g_secret_env[i]);
    }
}

int jc_proc_env_name_is_secret(const char *name, jc_size name_len)
{
    int i;
    if (name == NULL || name_len == 0) {
        return 0;
    }
    for (i = 0; g_secret_env_builtin[i] != NULL; i++) {
        if (strlen(g_secret_env_builtin[i]) == (size_t)name_len &&
            memcmp(g_secret_env_builtin[i], name, (size_t)name_len) == 0) {
            return 1;
        }
    }
    for (i = 0; i < g_secret_env_n; i++) {
        if (strlen(g_secret_env[i]) == (size_t)name_len &&
            memcmp(g_secret_env[i], name, (size_t)name_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* M1043-a: doctor's suffix scan walks the whole process environment, so it
 * needs `environ` on every platform, not just where JC_HAVE_ENVIRON_SCRUB
 * re-declares it (line below). The Makefile builds with -D_POSIX_C_SOURCE,
 * under which environ is declared by <unistd.h>; this extern is the same
 * declaration the scrub section makes, hoisted to cover the scan. */
extern char **environ;

/* M1043-a: doctor's suffix scan. The case-sensitive credential-ish suffixes,
 * kept here beside the two lists they are judged against so the warning cannot
 * drift from the scrub. A name matching a suffix that the lists already hold
 * (built-in or configured) is NOT secret-looking to us -- jichi knows it, so it
 * is silently dropped, and the warning is only for a name jichi does NOT know. */
static const char *const g_secret_env_suffix[] = {
    "_TOKEN", "_KEY", "_SECRET", "_PASSWORD", NULL
};

int jc_proc_secret_env_scan(char found[128][128], int max_found)
{
    int n = 0;
    int si, ei;

    if (found == NULL || max_found <= 0) {
        return -1;
    }
    if (environ == NULL) {
        return 0;
    }
    for (ei = 0; environ[ei] != NULL; ei++) {
        const char *entry = environ[ei];
        const char *eq;
        jc_size name_len, val_off;
        int matches = 0;
        if (entry == NULL) {
            continue;
        }
        eq = strchr(entry, '=');
        if (eq == NULL) {
            continue;
        }
        name_len = (jc_size)(eq - entry);
        if (name_len == 0) {
            continue;
        }
        /* non-empty value */
        val_off = (jc_size)(eq - entry) + 1;
        if (entry[val_off] == '\0') {
            continue;
        }
        /* suffix: case-sensitive, at least the suffix's own length */
        for (si = 0; g_secret_env_suffix[si] != NULL; si++) {
            jc_size sl = (jc_size)strlen(g_secret_env_suffix[si]);
            if (name_len >= sl &&
                memcmp(entry + name_len - sl, g_secret_env_suffix[si], sl) == 0) {
                matches = 1;
                break;
            }
        }
        if (!matches) {
            continue;
        }
        /* a name the scrub already knows is not warned: built-in or configured */
        if (jc_proc_env_name_is_secret(entry, name_len)) {
            continue;
        }
        if (n >= max_found) {
            break; /* more than fit; the caller shows the first max_found */
        }
        if (name_len > 127) {
            name_len = 127; /* the slot holds at most 127 + NUL */
        }
        memcpy(found[n], entry, (size_t)name_len);
        found[n][name_len] = '\0';
        n++;
    }
    return n;
}

int jc_proc_secret_env_count(void)
{
    return g_secret_env_n;
}

void jc_proc_secret_env_truncate(int n)
{
    if (n < 0) {
        n = 0;
    }
    while (g_secret_env_n > n) {
        free(g_secret_env[--g_secret_env_n]);
    }
}

/* --- the parent's own /proc entries (M770) -------------------------------- */

static int g_harden_state = JC_HARDEN_NOT_RUN;

void jc_proc_harden_environ(void)
{
#if defined(JC_HAVE_SET_DUMPABLE) || defined(JC_HAVE_PROC_TRACE_CTL)
    /* The opt-out needs a YES in the one boolean dialect (jc_bool_from_word):
     * "0", "false", "", a typo and prose all leave the protection on, because
     * a misspelled opt-out that silently removed a fence is the M519 class. One
     * knob covers both mechanisms: on Linux JICHI_KEEP_DUMPABLE keeps the
     * process dumpable, on FreeBSD it keeps it traceable -- the same escape,
     * for a core dump or a debugger. */
    const char *keep = getenv("JICHI_KEEP_DUMPABLE");
    int keep_yes = 0;
    if (keep != NULL && jc_bool_from_word(keep, &keep_yes) && keep_yes) {
        g_harden_state = JC_HARDEN_OFF_OPTOUT;
        return;
    }
#endif
#if defined(JC_HAVE_SET_DUMPABLE)
    /* Linux. The kernel reads unsigned longs, hence 0UL. ON is the flag read
     * back, not the call's return: doctor reports the effect, never the attempt. */
    if (prctl(PR_SET_DUMPABLE, 0UL, 0UL, 0UL, 0UL) == 0
        && prctl(PR_GET_DUMPABLE, 0UL, 0UL, 0UL, 0UL) == 0) {
        g_harden_state = JC_HARDEN_ON;
    } else {
        g_harden_state = JC_HARDEN_FAILED;
    }
#elif defined(JC_HAVE_PROC_TRACE_CTL)
    /* FreeBSD (M771). No procfs by default, but a same-user process reads
     * another's environment through `sysctl kern.proc.env` / `ps -e`, gated by
     * the p_candebug() check ptrace also uses. PROC_TRACE_CTL_DISABLE makes this
     * process untraceable, so that read returns EPERM; PROC_TRACE_STATUS writes
     * -1 when tracing is off, which is the effect read back (never the call's
     * return). Like PR_SET_DUMPABLE it is inherited on fork and reset on a
     * non-setuid exec, so the children jichi starts stay readable, and jichi
     * can still read them. id 0 with P_PID is the calling process. */
    {
        int mode = PROC_TRACE_CTL_DISABLE;
        int status = 0;
        if (procctl(P_PID, (id_t)0, PROC_TRACE_CTL, &mode) == 0
            && procctl(P_PID, (id_t)0, PROC_TRACE_STATUS, &status) == 0
            && status == -1) {
            g_harden_state = JC_HARDEN_ON;
        } else {
            g_harden_state = JC_HARDEN_FAILED;
        }
    }
#else
    g_harden_state = JC_HARDEN_UNAVAILABLE;
#endif
}

int jc_proc_harden_state(void)
{
    return g_harden_state;
}

/* --- OpenBSD's partial guard: scrub the secret env strings in place (M796) --- */

#if defined(JC_HAVE_ENVIRON_SCRUB)
extern char **environ;

/* Zero the whole "NAME=value" string for `name`, so `ps -e` -- which reads the
 * live user stack on OpenBSD -- shows nothing for the slot. getenv then skips
 * the NUL-led entry; nothing re-reads the key after config load, so this is
 * safe. Every matching entry is zeroed (a name should appear once, but a
 * duplicate export must not survive). */
static void scrub_env_name(const char *name)
{
    jc_size nl;
    char **e;
    if (name == NULL || name[0] == '\0') {
        return;
    }
    nl = strlen(name);
    for (e = environ; e != NULL && *e != NULL; e++) {
        if (strncmp(*e, name, nl) == 0 && (*e)[nl] == '=') {
            memset(*e, 0, strlen(*e));
        }
    }
}
#endif

void jc_proc_harden_secret_env(void)
{
#if defined(JC_HAVE_ENVIRON_SCRUB)
    int i;
    /* The same opt-out as the dumpable flag, in the one boolean dialect: a YES
     * keeps the key readable for a debugger; anything else keeps the guard on. */
    const char *keep = getenv("JICHI_KEEP_DUMPABLE");
    int keep_yes = 0;
    if (keep != NULL && jc_bool_from_word(keep, &keep_yes) && keep_yes) {
        g_harden_state = JC_HARDEN_OFF_OPTOUT;
        return;
    }
    for (i = 0; g_secret_env_builtin[i] != NULL; i++) {
        scrub_env_name(g_secret_env_builtin[i]);
    }
    for (i = 0; i < g_secret_env_n; i++) {
        scrub_env_name(g_secret_env[i]);
    }
    g_harden_state = JC_HARDEN_ON_PARTIAL;
#endif
}

jc_size jc_proc_secret_env_prefix_size(void)
{
    jc_size n = strlen("unset");
    int i;
    for (i = 0; g_secret_env_builtin[i] != NULL; i++) {
        n += 1 + strlen(g_secret_env_builtin[i]);
    }
    for (i = 0; i < g_secret_env_n; i++) {
        n += 1 + strlen(g_secret_env[i]);
    }
    return n + strlen("; ") + 1; /* + NUL */
}

int jc_proc_secret_env_prefix(char *buf, jc_size cap)
{
    struct jc_sb sb;
    int i, any = 0;
    char *s;
    if (buf == NULL || cap == 0) {
        return -1; /* nowhere to write it: the caller cannot scrub */
    }
    buf[0] = '\0';
    jc_sb_init(&sb);
    jc_sb_append(&sb, "unset");
    for (i = 0; g_secret_env_builtin[i] != NULL; i++) {
        jc_sb_append(&sb, " ");
        jc_sb_append(&sb, g_secret_env_builtin[i]);
        any = 1;
    }
    for (i = 0; i < g_secret_env_n; i++) {
        jc_sb_append(&sb, " ");
        jc_sb_append(&sb, g_secret_env[i]);
        any = 1;
    }
    jc_sb_append(&sb, "; ");
    s = sb.data; /* borrowed -- freed by jc_sb_free below (finish would
                  * detach and leak; the ci ASan pass caught this) */
    if (!any) {
        jc_sb_free(&sb);
        return 0; /* nothing to drop */
    }
    /* M724: "does not fit" (or no memory) is -1, never 0. It used to share 0
     * with "nothing registered", and the popen path, unable to tell them apart,
     * ran the command with no prefix at all: twelve configured names of ~100
     * bytes put every key -- the built-ins too -- in a model-issued command's
     * environment. */
    if (s == NULL || strlen(s) >= (size_t)cap) {
        jc_sb_free(&sb);
        return -1;
    }
    jc_snprintf(buf, cap, "%s", s);
    jc_sb_free(&sb);
    return 1;
}

/* See the long note in jc_proc.h: exec preserves an IGNORED disposition, so a
 * child inherits jichi's SIGPIPE = SIG_IGN and a pipeline producer stops dying
 * when its consumer exits. SIGPIPE is the only signal jichi ignores (checked
 * tree-wide), so resetting it is the whole fix rather than a first instalment. */
void jc_proc_child_sigreset(void)
{
    signal(SIGPIPE, SIG_DFL);
}

/* The popen streams' pid table. jichi forks rather than threads, and a popen
 * stream is short-lived (a command's output read to EOF and closed), so a small
 * fixed table with linear scan is the whole data structure; a caller somehow
 * holding eight live streams gets a refusal, never an untracked child. */
#define JC_POPEN_MAX 8
static struct { FILE *f; pid_t pid; } g_popen[JC_POPEN_MAX];

FILE *jc_proc_popen(const char *cmd, const char *mode)
{
    int fds[2];
    pid_t pid;
    FILE *f;
    int slot;
    struct jc_spawn_req sr;

    /* Every caller reads; a "w" stream has never existed here. Refusing it
     * keeps this function honest about what it reproduces of popen(3). */
    if (cmd == NULL || mode == NULL || mode[0] != 'r') {
        return NULL;
    }
    for (slot = 0; slot < JC_POPEN_MAX && g_popen[slot].f != NULL; slot++) {
        ;
    }
    if (slot == JC_POPEN_MAX) {
        return NULL;
    }
    if (jc_pipe_cloexec(fds) != 0) {
        return NULL;
    }
    /* M832: through jc_spawn, like every other child -- so the popen path stops
     * being the odd one out (libc's popen forks where jichi cannot see: it was
     * the one exec site the close-fds backstop structurally missed, M472, and
     * the one that needed the parent's SIGPIPE disposition flipped around the
     * call, M461 -- both rituals now run inside jc_spawn on either backend).
     * Both pipe ends are CLOEXEC from birth, which also retires the re-arm this
     * function used to do against glibc clearing the flag. Callers keep the
     * `unset` prefix from jc_proc_secret_env_prefix; the scrub here makes the
     * child's environment clean even before the shell reads that prefix. */
    memset(&sr, 0, sizeof sr);
    sr.shell_cmd = cmd;
    /* M844 (ACP-1): never jichi's own stdin -- over ACP it is the client's
     * JSON-RPC stream, and a command that reads stdin ate its next message
     * (a session/cancel). tests/smoke/acp_child_stdin.sh holds this. */
    sr.fd_stdin = JC_SPAWN_DEVNULL;
    sr.fd_stdout = fds[1];
    sr.fd_stderr = JC_SPAWN_INHERIT;   /* popen(3) leaves stderr alone */
    sr.scrub_env = 1;
    if (jc_spawn(&sr, &pid) != JC_OK) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    close(fds[1]);
    f = fdopen(fds[0], "rb");
    if (f == NULL) {
        close(fds[0]);
        /* The child runs on; reap it rather than leak a zombie. */
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
            ;
        }
        return NULL;
    }
    g_popen[slot].f = f;
    g_popen[slot].pid = pid;
    return f;
}

int jc_proc_pclose(FILE *f)
{
    int slot;
    int status = 0;
    pid_t pid = -1;

    if (f == NULL) {
        return -1;
    }
    for (slot = 0; slot < JC_POPEN_MAX; slot++) {
        if (g_popen[slot].f == f) {
            pid = g_popen[slot].pid;
            g_popen[slot].f = NULL;
            break;
        }
    }
    fclose(f);
    if (pid < 0) {
        return -1;   /* not one of ours: nothing to wait for */
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return status;
}

int jc_proc_waitpid(pid_t pid, int *status_out)
{
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;  /* any failure is UNKNOWN, never a status to decode */
        }
    }
    if (status_out != NULL) {
        *status_out = status;
    }
    return 0;
}

int jc_proc_reap_bounded(pid_t pid, double deadline_ms, volatile int *abort_flag,
                         int *status_out)
{
    int status;
    int i;
    long poll_ms = 20;

    /* If no deadline, set a far-future bound (but still check abort_flag). */
    if (deadline_ms <= 0.0) {
        deadline_ms = jc_now_millis() + 1e9;  /* ~11.6 days */
    }

    while (1) {
        pid_t rc;
        int saved_errno;

        rc = waitpid(pid, &status, WNOHANG);
        saved_errno = errno;

        if (rc == pid) {
            /* Child reaped successfully. */
            if (status_out != NULL) {
                *status_out = status;
            }
            return 0;
        } else if (rc == 0) {
            /* Child still running; check deadline and abort. */
            double now_ms = jc_now_millis();
            if (now_ms >= deadline_ms) {
                /* Deadline exceeded. Stop the child. The GROUP is the target;
                 * the direct kill is a fallback when setpgid did not take (a
                 * caller that reaps a non-leader child, e.g. a unit test). */
                if (kill(-pid, SIGTERM) != 0) {
                    kill(pid, SIGTERM);
                }
                /* Grace period ~1s. */
                i = 0;
                for (;;) {
                    pid_t gc = waitpid(pid, &status, WNOHANG);
                    if (gc == pid || (gc == -1 && errno == ECHILD)) {
                        /* Child died during grace; best-effort status. */
                        if (status_out != NULL && gc == pid) {
                            *status_out = status;
                        }
                        return -2;  /* stopped by deadline */
                    }
                    if (i++ >= 50) break;
                    jc_sleep_ms(20, abort_flag);
                }
                /* Escalate to SIGKILL. */
                if (kill(-pid, SIGKILL) != 0) {
                    kill(pid, SIGKILL);
                }
                /* Final blocking reap. */
                (void)jc_proc_waitpid(pid, (status_out != NULL) ? &status : NULL);
                if (status_out != NULL) {
                    *status_out = status;
                }
                return -2;  /* stopped by deadline */
            }
            /* Not deadlined yet; sleep and poll again. */
            if (jc_sleep_ms(poll_ms, abort_flag)) {
                /* Abort flag set. Stop the child (group, with the same
                 * direct-kill fallback as the deadline branch). */
                if (kill(-pid, SIGTERM) != 0) {
                    kill(pid, SIGTERM);
                }
                /* Grace period ~1s. */
                i = 0;
                for (;;) {
                    pid_t gc = waitpid(pid, &status, WNOHANG);
                    if (gc == pid || (gc == -1 && errno == ECHILD)) {
                        if (status_out != NULL && gc == pid) {
                            *status_out = status;
                        }
                        return -3;  /* stopped by abort */
                    }
                    if (i++ >= 50) break;
                    jc_sleep_ms(20, abort_flag);
                }
                if (kill(-pid, SIGKILL) != 0) {
                    kill(pid, SIGKILL);
                }
                (void)jc_proc_waitpid(pid, (status_out != NULL) ? &status : NULL);
                if (status_out != NULL) {
                    *status_out = status;
                }
                return -3;  /* stopped by abort */
            }
        } else if (rc == -1) {
            if (saved_errno == ECHILD) {
                /* SIGCHLD=SIG_IGN: kernel auto-reaped, no status. */
                return -1;  /* UNKNOWN */
            }
            if (saved_errno == EINTR) {
                /* Retry. */
                continue;
            }
            /* Other errors: UNKNOWN. */
            return -1;
        }
    }
}

/* See the long note in jc_proc.h. */
void jc_fd_cloexec(int fd)
{
    int fl;
    if (fd < 0) {
        return;
    }
    fl = fcntl(fd, F_GETFD);
    if (fl == -1) {
        return; /* not a live descriptor; nothing to protect */
    }
    (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
}

int jc_pipe_cloexec(int fds[2])
{
    if (pipe(fds) != 0) {
        return -1;
    }
    jc_fd_cloexec(fds[0]);
    jc_fd_cloexec(fds[1]);
    return 0;
}

void jc_proc_parent_cloexec_all(void)
{
    long maxfd;
    int fd;

    /* Same 4096 bound as the child-side backstop below, for the same reason. */
    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096L) {
        maxfd = 4096L;
    }
    for (fd = 3; fd < (int)maxfd; fd++) {
        int fl = fcntl(fd, F_GETFD);
        if (fl >= 0 && (fl & FD_CLOEXEC) == 0) {
            (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
        }
    }
}

void jc_proc_child_close_fds(void)
{
    long maxfd;
    int fd;

    /* _SC_OPEN_MAX can be huge (or -1) -- RLIMIT_NOFILE is commonly 1048576 on
     * a modern systemd box, and a million close() calls on every spawn is a
     * measurable cost for no benefit. 4096 covers any descriptor table jichi
     * can actually produce: it opens a handful of sinks, one provider socket,
     * and two pipes per live child. The cap is why this is a backstop and
     * jc_fd_cloexec is the fix -- a descriptor above the cap is still marked at
     * creation. */
    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096L) {
        maxfd = 4096L;
    }
    for (fd = 3; fd < (int)maxfd; fd++) {
        (void)close(fd);
    }
}

int jc_proc_capture(char *const argv[], const struct jc_vec *env,
                    const char *stdin_data, struct jc_sb *out, jc_size cap,
                    long timeout, volatile int *abort_flag)
{
    int inp[2];
    int outp[2];
    pid_t pid;
    int status = 0;
    double deadline;
    int truncated = 0;
    int read_done = 0;
    int stdin_done;
    int aborted = 0;
    int timed_out = 0;
    jc_size in_total;
    jc_size in_off = 0;
    int maxfd;

    if (jc_pipe_cloexec(inp) != 0) {
        return -1;
    }
    if (jc_pipe_cloexec(outp) != 0) {
        close(inp[0]);
        close(inp[1]);
        return -1;
    }
    /* M832: the child is described, not forked -- jc_spawn owns the ritual (the
     * pgroup lead so the timeout/abort path can kill the WHOLE pipeline (M461:
     * eight orphaned `yes` processes, 3h50m, load 9), the scrub-then-configured-
     * env order (M130), the close-fds backstop (M472), the SIGPIPE reset), and
     * carries it on both backends, fork and posix_spawn. */
    {
        struct jc_spawn_req sr;
        memset(&sr, 0, sizeof sr);
        sr.argv = argv;
        sr.fd_stdin = inp[0];
        sr.fd_stdout = outp[1];
        sr.fd_stderr = outp[1];
        sr.new_pgroup = 1;
        sr.scrub_env = 1;
        sr.env = env;
        if (jc_spawn(&sr, &pid) != JC_OK) {
            close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
            return -1;
        }
    }

    /* Non-blocking on both ends so stdin and stdout are serviced concurrently
     * (no deadlock when the child writes before reading). */
    close(inp[0]);
    close(outp[1]);
    fcntl(inp[1], F_SETFL, O_NONBLOCK);
    fcntl(outp[0], F_SETFL, O_NONBLOCK);
    in_total = (stdin_data != NULL) ? (jc_size)strlen(stdin_data) : 0;
    stdin_done = (in_total == 0);
    if (stdin_done) {
        close(inp[1]);
        inp[1] = -1;
    }

    /* M368: MONOTONIC, not time(NULL) -- the same fix as the envelope
     * verify runner. A kill deadline on a steppable clock is unreachable
     * after a backward step (VM guest time-sync, NTP), and this deadline
     * bounds every run_terminal_command / user tool / hook the agent runs. */
    deadline = jc_now_millis() +
               (double)(timeout > 0 ? timeout : JC_PROC_DEF_TIMEOUT) * 1000.0;
    while (!read_done) {
        fd_set rfds;
        fd_set wfds;
        struct timeval tv;
        int rc;

        if ((abort_flag != NULL && *abort_flag) ||
            jc_now_millis() > deadline) {
            /* The GROUP: `kill(pid, ...)` reaped the shell and orphaned every
             * other member of its pipeline. The direct kill stays as a fallback
             * for the case where setpgid did not take. */
            if (abort_flag != NULL && *abort_flag) {
                aborted = 1;
            } else {
                timed_out = 1;   /* deadline fired */
            }
            if (kill(-pid, SIGKILL) != 0) {
                kill(pid, SIGKILL);
            }
            break;
        }
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(outp[0], &rfds);
        maxfd = outp[0];
        if (!stdin_done) {
            FD_SET(inp[1], &wfds);
            if (inp[1] > maxfd) {
                maxfd = inp[1];
            }
        }
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        rc = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) {
            continue;
        }
        /* Feed stdin. */
        if (!stdin_done && FD_ISSET(inp[1], &wfds)) {
            ssize_t w = write(inp[1], stdin_data + in_off, in_total - in_off);
            if (w > 0) {
                in_off += (jc_size)w;
            } else if (w < 0 && errno != EINTR && errno != EAGAIN) {
                stdin_done = 1; /* EPIPE etc.: child closed stdin */
            }
            if (in_off >= in_total) {
                stdin_done = 1;
            }
            if (stdin_done && inp[1] >= 0) {
                close(inp[1]);
                inp[1] = -1;
            }
        }
        /* Drain stdout. M929 (R06-#1): `out` may be NULL -- the caller wants
         * the exit code and discards the child's output. The read still
         * happens (draining the pipe is what keeps the child from blocking),
         * but nothing is appended. Before this, every append dereferenced a
         * NULL `out` (SIGSEGV, exit 139, verified against a chatty child). */
        if (FD_ISSET(outp[0], &rfds)) {
            char chunk[4096];
            ssize_t n = read(outp[0], chunk, sizeof(chunk));
            if (n > 0 && out != NULL) {
                if (out->len < cap) {
                    jc_size room = cap - out->len;
                    jc_sb_append_n(out, chunk,
                                   ((jc_size)n < room) ? (jc_size)n : room);
                    if ((jc_size)n >= room) {
                        truncated = 1;
                    }
                } else {
                    truncated = 1;
                }
            } else if (n == 0) {
                read_done = 1;
            } else if (errno != EINTR && errno != EAGAIN) {
                read_done = 1;
            }
        }
    }

    if (inp[1] >= 0) {
        close(inp[1]);
    }
    close(outp[0]);
    if (truncated && out != NULL) {   /* M929: the marker is for the reader */
        jc_sb_append(out, "\n... [output truncated]");
    }
    /* M989: a read-loop kill (deadline or abort) is deterministic -2. The
     * child is already being SIGKILL'd above; give it a moment to be
     * reaped by the kernel, then reap and return -2 unconditionally. This
     * preserves the pre-M989 contract that jc_proc_capture returns -2 on
     * timeout/abort (the M461 pipeline test and every other caller rely
     * on it). Only the EOF-without-kill path (B-01's actual finding) goes
     * through the bounded helper, where the deadline/abort was NOT already
     * enforced in the read loop. */
    if (timed_out || aborted) {
        struct timeval dly; dly.tv_sec = 0; dly.tv_usec = 100000;
        select(0, NULL, NULL, NULL, &dly);
        (void)jc_proc_waitpid(pid, &status);
        return -2;
    }
    /* B-01 path: child closed its output but is still running; bounded,
     * interrupt-aware reap honours the deadline. */
    {
        int rc = jc_proc_reap_bounded(pid, deadline, abort_flag, &status);
        if (rc == -2 || rc == -3) { return -2; }
        if (rc == -1) { return -1; }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

int jc_memwatch_decision(long rss_kb, long budget_kb)
{
    if (budget_kb <= 0) return JC_MEMWATCH_OK;
    if (rss_kb >= budget_kb) return JC_MEMWATCH_KILL;
    if (rss_kb >= (budget_kb / 5) * 4) return JC_MEMWATCH_WARN; /* >= 80% */
    return JC_MEMWATCH_OK;
}

long jc_proc_group_rss_kb(long pgid)
{
    DIR *d;
    struct dirent *e;
    long total_pages = 0;
    long page_kb;

    if (pgid <= 0) return 0;
    d = opendir("/proc");
    if (d == NULL) return 0;
    while ((e = readdir(d)) != NULL) {
        char path[64];
        char buf[1024];
        FILE *f;
        const char *p = e->d_name;
        int is_num = 1;
        if (*p == '\0') continue;
        while (*p != '\0') { if (*p < '0' || *p > '9') { is_num = 0; break; } p++; }
        if (!is_num) continue;
        jc_snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
        f = fopen(path, "rb");
        if (f == NULL) continue;
        if (fgets(buf, sizeof buf, f) != NULL) {
            /* Fields: pid (comm) state ppid pgrp(5) ... rss(24). comm can hold
             * spaces/parens, so parse from AFTER the last ')'. */
            char *rp = strrchr(buf, ')');
            if (rp != NULL) {
                char st;
                long ppid, pgrp, v[19], rss = 0;
                int nf = sscanf(rp + 1,
                    " %c %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld "
                    "%ld %ld %ld %ld %ld %ld %ld %ld",
                    &st, &ppid, &pgrp, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                    &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13],
                    &v[14], &v[15], &v[16], &v[17], &rss);
                /* state + ppid + pgrp + 18 skipped + rss = 22 fields to rss. */
                if (nf == 22 && pgrp == pgid) total_pages += rss;
            }
        }
        fclose(f);
    }
    closedir(d);
#ifdef _SC_PAGESIZE
    page_kb = (long)(sysconf(_SC_PAGESIZE) / 1024);
#else
    page_kb = 0;   /* M834: DJGPP has no _SC_PAGESIZE; the 4 KB fallback below */
#endif
    if (page_kb <= 0) page_kb = 4;
    return total_pages * page_kb;
}
