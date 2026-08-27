/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_bg.c - background command execution (see jc_bg.h).
 *
 * Process discipline mirrors the fork pool in jc_tool_parallel.c (pipe, fork,
 * select/non-blocking drain, SIGTERM->SIGKILL reap), and the malloc/free-of-OS-
 * handles lifecycle mirrors jc_user_tool_mgr.
 */

#include "jc_bg.h"
#include "jc_platform.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_proc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

void jc_bg_mgr_init(struct jc_bg_mgr *m)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        m->procs[i].id = 0;
        m->procs[i].pid = 0;
        m->procs[i].fd = -1;
        m->procs[i].state = JC_BG_FREE;
        m->procs[i].exit_code = -1;
        m->procs[i].read_off = 0;
        m->procs[i].overflowed = 0;
        m->procs[i].cmd[0] = '\0';
        jc_sb_init(&m->procs[i].buf);
    }
    m->next_id = 1;
}

static struct jc_bg_proc *find(struct jc_bg_mgr *m, int id)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        if (m->procs[i].state != JC_BG_FREE && m->procs[i].id == id) {
            return &m->procs[i];
        }
    }
    return NULL;
}

/* Non-blocking: pull whatever is readable from the child's pipe into its buf. */
static void drain(struct jc_bg_proc *p)
{
    char chunk[4096];
    if (p->fd < 0) {
        return;
    }
    for (;;) {
        ssize_t n = read(p->fd, chunk, sizeof(chunk));
        if (n > 0) {
            if (p->buf.len < (jc_size)JC_BG_BUF_MAX) {
                jc_size room = (jc_size)JC_BG_BUF_MAX - p->buf.len;
                jc_sb_append_n(&p->buf, chunk,
                               ((jc_size)n < room) ? (jc_size)n : room);
                if ((jc_size)n >= room) p->overflowed = 1;
            } else {
                p->overflowed = 1;
            }
            continue;
        }
        if (n == 0) {
            /* EOF: writer closed (child exiting). */
            close(p->fd);
            p->fd = -1;
            return;
        }
        /* n < 0 */
        if (errno == EINTR) continue;
        return; /* EAGAIN/EWOULDBLOCK: nothing more for now */
    }
}

/* Reap if the child has exited; sets state + exit_code. */
static void reap(struct jc_bg_proc *p)
{
    int status;
    pid_t r;
    if (p->state != JC_BG_RUNNING) {
        return;
    }
    r = waitpid((pid_t)p->pid, &status, WNOHANG);
    if (r == (pid_t)p->pid) {
        if (WIFEXITED(status)) {
            p->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            p->exit_code = 128 + WTERMSIG(status);
        } else {
            p->exit_code = -1;
        }
        p->state = JC_BG_EXITED;
    }
}

int jc_bg_start(struct jc_bg_mgr *m, const char *command)
{
    int i;
    struct jc_bg_proc *p = NULL;
    int pfd[2];
    pid_t pid;

    if (command == NULL || command[0] == '\0') {
        return -1;
    }
    /* Prefer a free slot; otherwise reclaim a finished (exited/killed) one so a
     * long session isn't capped at JC_BG_MAX *lifetime* background commands. A
     * still-RUNNING slot is never reused. */
    for (i = 0; i < JC_BG_MAX; i++) {
        if (m->procs[i].state == JC_BG_FREE) {
            p = &m->procs[i];
            break;
        }
    }
    if (p == NULL) {
        for (i = 0; i < JC_BG_MAX; i++) {
            if (m->procs[i].state == JC_BG_EXITED ||
                m->procs[i].state == JC_BG_KILLED) {
                p = &m->procs[i];
                if (p->fd >= 0) {
                    close(p->fd);
                    p->fd = -1;
                }
                jc_sb_free(&p->buf); /* release the prior capture before reuse */
                break;
            }
        }
    }
    if (p == NULL) {
        return 0; /* all slots are running */
    }
    if (jc_pipe_cloexec(pfd) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        /* Child: own process group (so kill reaches grandchildren), both std
         * streams to the pipe, then exec the shell. */
        setpgid(0, 0);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[0]);
        close(pfd[1]);
        jc_proc_scrub_secret_env(); /* background command must not see keys */
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execl(jc_shell_path(), "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    /* Parent. Also set the child's process group here (racing the child's own
     * setpgid harmlessly -- both target the same pgid), so a kill(-pid) issued
     * immediately after start can't miss a child that hasn't run setpgid yet. */
    setpgid(pid, pid);
    close(pfd[1]);
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);
    p->id = m->next_id++;
    p->pid = (long)pid;
    p->fd = pfd[0];
    p->state = JC_BG_RUNNING;
    p->exit_code = -1;
    p->read_off = 0;
    p->overflowed = 0;
    jc_sb_init(&p->buf);
    jc_snprintf(p->cmd, sizeof(p->cmd), "%s", command);
    return p->id;
}

void jc_bg_poll(struct jc_bg_mgr *m)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        struct jc_bg_proc *p = &m->procs[i];
        if (p->state == JC_BG_RUNNING) {
            drain(p);
            reap(p);
        }
    }
}

jc_status jc_bg_read(struct jc_bg_mgr *m, int id, struct jc_sb *out)
{
    struct jc_bg_proc *p = find(m, id);
    if (p == NULL) {
        return JC_ERR_NOTFOUND;
    }
    drain(p);
    reap(p);
    if (p->buf.len > p->read_off) {
        jc_sb_append_n(out, p->buf.data + p->read_off,
                       p->buf.len - p->read_off);
        p->read_off = p->buf.len;
    }
    if (out->len == 0) {
        jc_sb_append(out, "(no new output)");
    }
    if (p->state == JC_BG_RUNNING) {
        jc_sb_append_fmt(out, "\n[background %d: running, pid %ld]", id, p->pid);
    } else {
        jc_sb_append_fmt(out, "\n[background %d: exited, status %d]",
                         id, p->exit_code);
    }
    if (p->overflowed) {
        jc_sb_append(out, " [output truncated]");
    }
    return JC_OK;
}

jc_status jc_bg_kill(struct jc_bg_mgr *m, int id)
{
    struct jc_bg_proc *p = find(m, id);
    int waited;
    if (p == NULL) {
        return JC_ERR_NOTFOUND;
    }
    if (p->state == JC_BG_RUNNING) {
        /* Signal the whole group; grandchildren (e.g. a dev server's workers)
         * go too. */
        kill(-(pid_t)p->pid, SIGTERM);
        for (waited = 0; waited < 10; waited++) {
            reap(p);
            if (p->state != JC_BG_RUNNING) {
                break;
            }
            jc_sleep_ms(50, NULL);
        }
        if (p->state == JC_BG_RUNNING) {
            kill(-(pid_t)p->pid, SIGKILL);
            waitpid((pid_t)p->pid, NULL, 0);
            p->state = JC_BG_KILLED;
        }
        /* else it exited on its own during the grace window: reap() already set
         * EXITED + the real exit_code -- don't clobber it with KILLED. */
    }
    drain(p);
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
    return JC_OK;
}

void jc_bg_mgr_free(struct jc_bg_mgr *m)
{
    int i;
    for (i = 0; i < JC_BG_MAX; i++) {
        struct jc_bg_proc *p = &m->procs[i];
        if (p->state == JC_BG_RUNNING) {
            kill(-(pid_t)p->pid, SIGTERM);
        }
    }
    /* Brief grace, then hard-kill and reap any stragglers. */
    jc_sleep_ms(80, NULL);
    for (i = 0; i < JC_BG_MAX; i++) {
        struct jc_bg_proc *p = &m->procs[i];
        if (p->state == JC_BG_RUNNING) {
            reap(p);
            if (p->state == JC_BG_RUNNING) {
                kill(-(pid_t)p->pid, SIGKILL);
                waitpid((pid_t)p->pid, NULL, 0);
                p->state = JC_BG_KILLED;
            }
        }
        if (p->fd >= 0) {
            close(p->fd);
            p->fd = -1;
        }
        jc_sb_free(&p->buf);
        p->state = JC_BG_FREE;
    }
}
