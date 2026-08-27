/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_proc.c - child process capture (see jc_proc.h). */

#include "jc_proc.h"
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
#include <sys/select.h>

/* --- secret env scrubbing (M130) ------------------------------------------ */

#define JC_SECRET_ENV_MAX 32
#define JC_SECRET_ENV_LEN 128
static char g_secret_env[JC_SECRET_ENV_MAX][JC_SECRET_ENV_LEN];
static int  g_secret_env_n;

/* Well-known provider key vars are always dropped, even when unconfigured here,
 * so a stray export in the parent's shell can't leak either.
 *
 * M608: jichi's OWN two names lead the list. Thirteen third-party names were
 * here and neither of the names the setup wizard, the scaffolder and the HRZ
 * onboarding actually write -- so the "stray export" promise above held for
 * every provider's key except the one this project's users have (measured:
 * both reached a shell tool's child, tests/smoke/secret_env_subcommands.sh).
 * tests/smoke/secret_env_lint.sh pins every apiKeyEnv default the tree ships
 * to a row here. */
static const char *const g_secret_env_builtin[] = {
    "JICHI_API_KEY", "JLU_API_KEY",
    "ANTHROPIC_API_KEY", "OPENAI_API_KEY", "OPENROUTER_API_KEY",
    "GEMINI_API_KEY", "GOOGLE_API_KEY", "GROQ_API_KEY", "MISTRAL_API_KEY",
    "DEEPSEEK_API_KEY", "TOGETHER_API_KEY", "FIREWORKS_API_KEY",
    "TAVILY_API_KEY", "COHERE_API_KEY", "XAI_API_KEY", NULL
};

void jc_proc_secret_env_add(const char *name)
{
    int i;
    if (name == NULL || name[0] == '\0' ||
        strlen(name) >= (size_t)JC_SECRET_ENV_LEN) {
        return;
    }
    /* A POSIX env-var name is [A-Za-z_][A-Za-z0-9_]*. Reject anything else so a
     * config-supplied apiKeyEnv can never inject shell metacharacters into the
     * unset-prefix built for the popen path. M326e moved the test to the shared
     * jc_envvar_name_valid so this refusal, doctor's lint and the setup
     * wizard's validation cannot drift apart -- they are three consequences of
     * one rule. */
    if (!jc_envvar_name_valid(name)) {
        return;
    }
    for (i = 0; i < g_secret_env_n; i++) {
        if (strcmp(g_secret_env[i], name) == 0) {
            return; /* already registered */
        }
    }
    if (g_secret_env_n < JC_SECRET_ENV_MAX) {
        jc_snprintf(g_secret_env[g_secret_env_n], JC_SECRET_ENV_LEN, "%s", name);
        g_secret_env_n++;
    }
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

int jc_proc_secret_env_prefix(char *buf, jc_size cap)
{
    struct jc_sb sb;
    int i, any = 0;
    char *s;
    if (buf == NULL || cap == 0) {
        return 0;
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
    if (any && s != NULL && strlen(s) < (size_t)cap) {
        jc_snprintf(buf, cap, "%s", s);
    } else {
        any = 0; /* nothing registered, or would overflow: emit no prefix */
    }
    jc_sb_free(&sb);
    return any;
}

/* Apply `env` (jc_vec of "KEY=VALUE") in the current (child) process. */
static void apply_env(const struct jc_vec *env)
{
    jc_size i;
    if (env == NULL) {
        return;
    }
    for (i = 0; i < env->len; i++) {
        const char *kv = *(char **)jc_vec_at((struct jc_vec *)env, i);
        const char *eq = strchr(kv, '=');
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

/* See the long note in jc_proc.h: exec preserves an IGNORED disposition, so a
 * child inherits jichi's SIGPIPE = SIG_IGN and a pipeline producer stops dying
 * when its consumer exits. SIGPIPE is the only signal jichi ignores (checked
 * tree-wide), so resetting it is the whole fix rather than a first instalment. */
void jc_proc_child_sigreset(void)
{
    signal(SIGPIPE, SIG_DFL);
}

FILE *jc_proc_popen(const char *cmd, const char *mode)
{
    FILE *f;
    void (*prev)(int);
    prev = signal(SIGPIPE, SIG_DFL);
    f = popen(cmd, mode);
    signal(SIGPIPE, prev);
    /* Re-arm close-on-exec on the end WE keep (M472). glibc's popen creates its
     * pipe with O_CLOEXEC and then deliberately clears the flag on the parent's
     * end -- observed directly:
     *
     *   pipe2([6, 7], O_CLOEXEC)   = 0
     *   fcntl(6, F_SETFD, 0)       = 0
     *
     * because libc closes other popen streams in a popen child by walking its
     * own chain, which only covers ITS children. Every child jichi forks and
     * execs itself would inherit this descriptor, and one of those is `sh -c
     * <whatever the model chose>`. Marking it here is the only place that can:
     * the descriptor does not exist before popen returns. */
    if (f != NULL) {
        jc_fd_cloexec(fileno(f));
    }
    return f;
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
    int timed_out = 0;
    int read_done = 0;
    int stdin_done;
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
    pid = fork();
    if (pid < 0) {
        close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(inp[0], STDIN_FILENO);
        dup2(outp[1], STDOUT_FILENO);
        dup2(outp[1], STDERR_FILENO);
        close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
        /* M461: lead our own process group, so the timeout/abort path below can
         * kill the WHOLE pipeline. Without this only the shell was killed and
         * every other member of `a | b` was orphaned to init and kept running.
         * Measured: eight `yes` processes left over from timed-out captures,
         * still spinning 3h50m later at load average 9 inside a test VM.
         * jc_bg.c has done this since it was written; this path never did. */
        setpgid(0, 0);
        jc_proc_scrub_secret_env(); /* drop inherited keys before... */
        apply_env(env);             /* ...any explicitly-configured env */
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset();   /* M461: do not inherit SIG_IGN */
        execvp(argv[0], argv);
        _exit(127);
    }

    /* Parent. Also set the child's process group here, racing the child's own
     * setpgid harmlessly -- both target the same pgid -- so a kill(-pid) issued
     * before the child has run cannot miss it. Same reasoning as jc_bg.c. */
    setpgid(pid, pid);

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
            timed_out = 1;
            /* The GROUP: `kill(pid, ...)` reaped the shell and orphaned every
             * other member of its pipeline. The direct kill stays as a fallback
             * for the case where setpgid did not take. */
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
        /* Drain stdout. */
        if (FD_ISSET(outp[0], &rfds)) {
            char chunk[4096];
            ssize_t n = read(outp[0], chunk, sizeof(chunk));
            if (n > 0) {
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
    if (truncated) {
        jc_sb_append(out, "\n... [output truncated]");
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (timed_out) {
        return -2;
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
        f = fopen(path, "r");
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
    page_kb = (long)(sysconf(_SC_PAGESIZE) / 1024);
    if (page_kb <= 0) page_kb = 4;
    return total_pages * page_kb;
}
