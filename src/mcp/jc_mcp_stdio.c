/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mcp_stdio.c - stdio transport for the MCP client.
 *
 * The server is spawned as a subprocess; JSON-RPC messages are exchanged as
 * newline-delimited JSON over its stdin/stdout (the MCP stdio framing: one
 * message per line, never containing an embedded newline -- which our compact
 * builders guarantee). The child's stderr is inherited so its diagnostics
 * reach the user.
 *
 * POSIX: fork/exec/pipe/select/waitpid. This translation unit relies on
 * _POSIX_C_SOURCE (set globally by the Makefile).
 */

#include "mcp_internal.h"
#include "jc_str.h"
#include "jc_log.h"
#include "jc_platform.h"
#include "jc_proc.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>

#define MCP_IO_TIMEOUT_SECS 120.0

struct stdio_state {
    pid_t        pid;
    int          in_fd;   /* write end: child's stdin  */
    int          out_fd;  /* read end:  child's stdout */
    struct jc_sb rbuf;    /* unconsumed bytes from out_fd (line framing) */
};

/* Split "KEY=VALUE" and apply with setenv; runs in the forked child only. */
static void apply_env(struct jc_vec *env)
{
    jc_size i;
    for (i = 0; i < env->len; i++) {
        const char *kv = *(char **)jc_vec_at(env, i);
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

/* Build a NULL-terminated argv from command + args (malloc'd; caller frees). */
static char **build_argv(const struct jc_mcp_server_cfg *cfg)
{
    char **argv;
    jc_size n = cfg->args.len;
    jc_size i;

    argv = (char **)malloc((n + 2) * sizeof(char *));
    if (argv == NULL) {
        return NULL;
    }
    argv[0] = cfg->command;
    for (i = 0; i < n; i++) {
        argv[i + 1] = *(char **)jc_vec_at((struct jc_vec *)&cfg->args, i);
    }
    argv[n + 1] = NULL;
    return argv;
}

/* Spawn the server. On success fills pid/in_fd/out_fd and returns JC_OK. */
static jc_status spawn(const struct jc_mcp_server_cfg *cfg, pid_t *pid,
                       int *in_fd, int *out_fd)
{
    int inp[2];
    int outp[2];
    char **argv;
    pid_t child;

    if (cfg->command == NULL || cfg->command[0] == '\0') {
        jc_logf(JC_LOG_ERROR, "mcp '%s': no command configured", cfg->name);
        return JC_ERR_INVALID;
    }
    if (jc_pipe_cloexec(inp) != 0) {
        return JC_ERR_IO;
    }
    if (jc_pipe_cloexec(outp) != 0) {
        close(inp[0]);
        close(inp[1]);
        return JC_ERR_IO;
    }
    argv = build_argv(cfg);
    if (argv == NULL) {
        close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
        return JC_ERR_OOM;
    }

    child = fork();
    if (child < 0) {
        free(argv);
        close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
        return JC_ERR_IO;
    }
    if (child == 0) {
        /* Child: wire stdin/stdout to the pipes, keep stderr. */
        dup2(inp[0], STDIN_FILENO);
        dup2(outp[1], STDOUT_FILENO);
        close(inp[0]); close(inp[1]);
        close(outp[0]); close(outp[1]);
        jc_proc_scrub_secret_env(); /* drop our keys before... */
        apply_env(&((struct jc_mcp_server_cfg *)cfg)->env); /* ...server env */
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execvp(argv[0], argv);
        /* exec failed. */
        _exit(127);
    }

    /* Parent. */
    free(argv);
    close(inp[0]);
    close(outp[1]);
    *pid = child;
    *in_fd = inp[1];
    *out_fd = outp[0];
    return JC_OK;
}

/* Write `line` followed by '\n', retrying short writes and EINTR. */
static jc_status write_line(int fd, const char *line)
{
    jc_size total = strlen(line);
    jc_size off = 0;
    while (off < total) {
        ssize_t w = write(fd, line + off, total - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return JC_ERR_IO;
        }
        off += (jc_size)w;
    }
    for (;;) {
        ssize_t w = write(fd, "\n", 1);
        if (w == 1) {
            break;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        return JC_ERR_IO;
    }
    return JC_OK;
}

/* Pop the next complete line (without the newline) from `buf` into a malloc'd
 * string. Returns 1 if a line was available, else 0. */
static int pop_line(struct jc_sb *buf, char **out)
{
    jc_size i;
    if (buf->data == NULL) {
        return 0;
    }
    for (i = 0; i < buf->len; i++) {
        if (buf->data[i] == '\n') {
            char *line = (char *)malloc(i + 1);
            if (line != NULL) {
                memcpy(line, buf->data, i);
                line[i] = '\0';
            }
            /* Shift the remainder down. */
            memmove(buf->data, buf->data + i + 1, buf->len - i - 1);
            buf->len -= i + 1;
            buf->data[buf->len] = '\0';
            *out = line;
            return line != NULL;
        }
    }
    return 0;
}

/* Block until a full line is available, returning it in *out (malloc'd).
 * Honours the abort flag and an overall deadline. */
static jc_status read_line(struct stdio_state *s, volatile int *abort,
                           char **out)
{
    double deadline = jc_now_seconds() + MCP_IO_TIMEOUT_SECS;

    *out = NULL;
    if (pop_line(&s->rbuf, out)) {
        return JC_OK;
    }
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int rc;

        if (abort != NULL && *abort) {
            return JC_ERR_ABORTED;
        }
        if (jc_now_seconds() > deadline) {
            jc_logf(JC_LOG_ERROR, "mcp: timed out waiting for response");
            return JC_ERR_IO;
        }
        FD_ZERO(&rfds);
        FD_SET(s->out_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000; /* 200ms: bounded so abort/deadline are checked */
        rc = select(s->out_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return JC_ERR_IO;
        }
        if (rc == 0) {
            continue; /* timeout slice elapsed; re-check abort/deadline */
        }
        {
            char chunk[4096];
            ssize_t n = read(s->out_fd, chunk, sizeof(chunk));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return JC_ERR_IO;
            }
            if (n == 0) {
                jc_logf(JC_LOG_ERROR, "mcp: server closed the connection");
                return JC_ERR_IO; /* EOF: child exited */
            }
            jc_sb_append_n(&s->rbuf, chunk, (jc_size)n);
            if (pop_line(&s->rbuf, out)) {
                return JC_OK;
            }
        }
    }
}

static jc_status stdio_request(struct jc_mcp_conn *c, const char *line,
                               long id, char **resp_out)
{
    struct stdio_state *s = (struct stdio_state *)c->t;
    jc_status st;
    int guard;

    *resp_out = NULL;
    st = write_line(s->in_fd, line);
    if (st != JC_OK) {
        return st;
    }
    /* Read messages until the one whose id matches our request. Server-initiated
     * notifications (no id) and unrelated ids are skipped. */
    for (guard = 0; guard < 10000; guard++) {
        char *msg = NULL;
        long got;
        st = read_line(s, c->abort, &msg);
        if (st != JC_OK) {
            return st;
        }
        if (jc_mcp_message_id(msg, &got) && got == id) {
            *resp_out = msg;
            return JC_OK;
        }
        free(msg); /* a notification or a stray response: ignore */
    }
    return JC_ERR_IO;
}

static jc_status stdio_notify(struct jc_mcp_conn *c, const char *line)
{
    struct stdio_state *s = (struct stdio_state *)c->t;
    return write_line(s->in_fd, line);
}

static void stdio_close(struct jc_mcp_conn *c)
{
    struct stdio_state *s = (struct stdio_state *)c->t;
    int status;
    if (s == NULL) {
        return;
    }
    if (s->in_fd >= 0) {
        close(s->in_fd);
    }
    if (s->out_fd >= 0) {
        close(s->out_fd);
    }
    if (s->pid > 0) {
        /* Closing stdin should prompt a clean exit; nudge then reap. */
        kill(s->pid, SIGTERM);
        waitpid(s->pid, &status, 0);
    }
    jc_sb_free(&s->rbuf);
    free(s);
    c->t = NULL;
}

static const struct jc_mcp_transport_vt STDIO_VT = {
    stdio_request,
    stdio_notify,
    stdio_close
};

jc_status jc_mcp_stdio_open(struct jc_mcp_conn **out,
                            const struct jc_mcp_server_cfg *cfg,
                            volatile int *abort)
{
    struct jc_mcp_conn *c;
    struct stdio_state *s;
    jc_status st;

    /* Writing to a server that has exited would otherwise kill us. */
    signal(SIGPIPE, SIG_IGN);

    s = (struct stdio_state *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return JC_ERR_OOM;
    }
    s->in_fd = -1;
    s->out_fd = -1;
    jc_sb_init(&s->rbuf);

    st = spawn(cfg, &s->pid, &s->in_fd, &s->out_fd);
    if (st != JC_OK) {
        jc_sb_free(&s->rbuf);
        free(s);
        return st;
    }

    c = jc_mcp_conn_alloc(cfg->name, abort);
    if (c == NULL) {
        close(s->in_fd);
        close(s->out_fd);
        kill(s->pid, SIGTERM);
        waitpid(s->pid, NULL, 0);
        jc_sb_free(&s->rbuf);
        free(s);
        return JC_ERR_OOM;
    }
    c->vt = &STDIO_VT;
    c->t = s;
    *out = c;
    return JC_OK;
}
