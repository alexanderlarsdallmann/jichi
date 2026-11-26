/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_control.c - the control-channel socket manager (M159).
 *
 * See jc_control.h for the model. The transport is deliberately minimal: one
 * request line per connection, one response line back, close. The loop only
 * calls in here at tool-call boundaries (jc_control_boundary), so a command
 * can never interleave with a streaming response or a half-applied tool.
 * Nothing here can widen the run's permissions: the five verbs are read-only
 * (status), narrowing (inject/pause/abort), or a wake (resume). */

#include "jc_control.h"
#include "jc_app.h"
#include "jc_perm.h"
#include "jc_message.h"
#include "jc_envelope.h"
#include "jc_eventlog.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "cJSON.h"
#include "jc_proc.h"   /* jc_fd_cloexec (M472) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include "jc_select.h"  /* M834: <sys/select.h> where it exists */
#include <sys/time.h> /* struct timeval: MiNTLib's <sys/select.h> only forward-declares it (M723) */
#include <sys/stat.h>
#ifndef JC_NO_SOCKETS
#include <sys/socket.h>
#include <sys/un.h>
#endif

/* The reply to an accepted inject-task. It says what the parent knows when it
 * answers: the note is in the task's pipe. It does NOT say "applied": a task
 * reads its notes only at a tool boundary, and a task whose next reply is its
 * last has none, so the note is then never read (the threadwork review's F22;
 * the operator's decision of 2026-11-26). Reporting which notes were read is
 * a later step and needs a field in the child's result. Outside the
 * JC_NO_SOCKETS block: serve_pending uses it in both builds (REL-0150D's gate
 * went red on fallbacks-compile while it stood inside). */
#define JC_CTL_INJECT_TASK_NOTE \
    "queued; it is delivered at the task's next tool boundary, if it reaches one"

/* Cap the total queued steering text (several injects between boundaries). */
#define CONTROL_PENDING_MAX (4 * JC_CONTROL_LINE_MAX)

/* Forward decl: jc_control_close (above the body) calls this for its final
 * CLOSING-boundary drain (M1192 cell 4). */
static int serve_pending(struct jc_app *app, struct jc_control *c,
                         long wait_ms, int abort_mode);

void jc_control_default_path(char *out, jc_size cap)
{
    jc_snprintf(out, cap, "%s/.jichi.d/control/%ld.sock",
                jc_home_dir(), (long)getpid());
}

int jc_control_available(void)
{
#ifdef JC_NO_SOCKETS
    return 0;
#else
    return 1;
#endif
}

#ifdef JC_NO_SOCKETS
jc_status jc_control_open(struct jc_control *c, const char *path)
{
    if (c == NULL || path == NULL || path[0] == '\0') {
        return JC_ERR_INVALID;
    }
    memset(c, 0, sizeof(*c));
    c->listen_fd = -1;
    jc_sb_init(&c->pending);
    jc_sb_init(&c->pending_from);
    c->started = (long)time(NULL);
    c->serve_pid = (long)getpid();  /* SV:1848: dead weight on this build; one body */
    return JC_ERR_IO;
}

/* listen_fd is always -1 on this build and every caller of serve_pending
 * checks that first, so this is never reached; it keeps serve_pending one body
 * for both builds. */
static int control_accept(int listen_fd)
{
    (void)listen_fd;
    return -1;
}
#else
jc_status jc_control_open(struct jc_control *c, const char *path)
{
    struct sockaddr_un addr;
    char dir[1024];
    char *slash;

    if (c == NULL || path == NULL || path[0] == '\0') {
        return JC_ERR_INVALID;
    }
    memset(c, 0, sizeof(*c));
    c->listen_fd = -1;
    jc_sb_init(&c->pending);
    jc_sb_init(&c->pending_from);
    c->started = (long)time(NULL);
    if (strlen(path) >= sizeof(addr.sun_path) ||
        strlen(path) >= sizeof(c->path)) {
        return JC_ERR_INVALID;
    }
    jc_snprintf(c->path, sizeof(c->path), "%s", path);

    /* Private parent directory (0700), like every other jichi sink. */
    jc_snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL && slash != dir) {
        *slash = '\0';
        /* 0700 on what we create. --control takes a path from the operator, so
         * this must not re-permission a directory they already had (M488). */
        if (jc_mkdir_p_private(dir) != JC_OK) {
            return JC_ERR_IO;
        }
    }

    unlink(path); /* a stale socket from a dead pid would block bind() */

    c->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (c->listen_fd < 0) {
        return JC_ERR_IO;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    jc_snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (bind(c->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(c->listen_fd, 8) != 0) {
        close(c->listen_fd);
        c->listen_fd = -1;
        return JC_ERR_IO;
    }
    jc_make_private(path); /* 0600: same-user only -- the whole ACL */
    /* The 0600 above is the ACL for anyone who has to OPEN the socket. A child
     * that inherits the descriptor never opens anything, so it would be inside
     * the ACL by accident -- and this socket drives the agent (M472). */
    jc_fd_cloexec(c->listen_fd);
    fcntl(c->listen_fd, F_SETFL, O_NONBLOCK);
    c->serve_pid = (long)getpid();
    /* SV:1848 (M1103): the socket's owner is this process; only it serves
     * the socket (the serve_pid guard at the two service points). */
    return JC_OK;
}

static int control_accept(int listen_fd)
{
    return accept(listen_fd, NULL, NULL);
}
#endif /* JC_NO_SOCKETS */

void jc_control_close(struct jc_control *c)
{
    if (c == NULL) {
        return;
    }
    if (c->listen_fd >= 0) {
        /* M1192 cell 4: the CLOSING boundary serve. The agent loop's last
         * serve point (the post-final-answer pre-request boundary) already ran
         * by the time main.c reaches teardown; a supervisor that connects in
         * this final window (the driver's second cancel-cmd) would otherwise
         * sit in the listen backlog until the close below, and its reply would
         * be lost -- the socket closes before anything accepts it. A final
         * non-blocking drain answers it here, while the app (and thus the
         * cancel arm's in_flight/why state) is still valid: main.c frees the
         * run's env + arena only AFTER this call. abort_mode 1 defers any
         * abort to the (already-finished) boundary, and the drain is bounded,
         * so a closing run can never hang. c->app is NULL on a NO_SOCKETS
         * init and on a control that served no app boundary yet -- both are
         * no-ops. */
        if (c->app != NULL && c->serve_pid == (long)getpid()) {
            (void)serve_pending(c->app, c, 0, 1);
        }
        close(c->listen_fd);
        c->listen_fd = -1;
    }
    if (c->path[0] != '\0') {
        unlink(c->path);
    }
    c->app = NULL;
    jc_sb_free(&c->pending);
    jc_sb_free(&c->pending_from);
}

/* Read one bounded request line from `fd` (the client writes immediately;
 * wait up to 2s). Returns malloc'd line (no newline) or NULL.
 *
 * M1149: the bound is JC_CONTROL_LINE_MAX_INJECT (8 * the old cap), not the old
 * 16384 -- a supervisor's inject-task carries its text in the line, and driver
 * cell 14 sends a ~70000-byte request over the old cap. Every other verb's
 * input stays far under the old cap; this just stops the socket from truncating
 * a legitimately large inject-task line into incomplete JSON ("not a JSON
 * object"). Still bounded: a line over the cap is refused at the codec
 * (JC_ERR_PARSE) or the pool's per-task queue cap (JC_ERR_TOOBIG), never
 * stored. */
static char *read_request(int fd)
{
    char *buf;
    jc_size used = 0;
    long deadline = (long)time(NULL) + 2;

    buf = (char *)malloc(JC_CONTROL_LINE_MAX_INJECT + 1);
    if (buf == NULL) {
        return NULL;
    }
    while (used < JC_CONTROL_LINE_MAX_INJECT &&
           (long)time(NULL) <= deadline) {
        fd_set rf;
        struct timeval tv;
        long n;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) {
            continue;
        }
        n = (long)read(fd, buf + used, JC_CONTROL_LINE_MAX_INJECT - used);
        if (n <= 0) {
            break;
        }
        used += (jc_size)n;
        if (memchr(buf, '\n', used) != NULL) {
            break;
        }
    }
    if (used == 0) {
        free(buf);
        return NULL;
    }
    {
        char *nl = (char *)memchr(buf, '\n', used);
        if (nl != NULL) {
            *nl = '\0';
        } else {
            buf[used] = '\0';
        }
    }
    return buf;
}

static void write_reply(int fd, char *line /* malloc'd, consumed */)
{
    if (line == NULL) {
        return;
    }
    /* Best-effort single write; the peer reads one line and closes. */
    if (write(fd, line, strlen(line)) < 0) {
        /* peer went away -- nothing to do */
    }
    free(line);
}

/* Journal an accepted command (bounded + secret-scrubbed text) so a steered
 * run is auditable; also logged. `extend` marks a pause --extend; `credited`
 * >= 0 records deadline seconds given back on resume (M162); pass 0 / -1 to
 * omit. No-op without an active journal. */
static void journal_cmd(struct jc_app *app, const char *cmd, const char *text,
                        int extend, long credited)
{
    jc_logf(JC_LOG_INFO, "control: %s%s%s", cmd,
            text != NULL ? " " : "", text != NULL ? text : "");
    if (app->env != NULL) {
        cJSON *o = jc_env_journal_begin(app->env, "control");
        if (o != NULL) {
            cJSON_AddStringToObject(o, "cmd", cmd);
            if (text != NULL) {
                jc_eventlog_add_text(o, "text", text, JC_CONTROL_LINE_MAX);
            }
            if (extend) {
                cJSON_AddBoolToObject(o, "extend", 1);
            }
            if (credited >= 0) {
                cJSON_AddNumberToObject(o, "credited", (double)credited);
            }
        }
        jc_env_journal_end(app->env, o);
    }
}

/* Close out an --extend pause: credit the paused seconds back to the
 * envelope's deadline (the ONLY thing allowed to stretch it) and journal
 * how much. Safe to call when no extend-pause is live (no-op). */
static void pause_credit_apply(struct jc_app *app, struct jc_control *c)
{
    if (!c->pause_extend) {
        return;
    }
    c->pause_extend = 0;
    if (app->env != NULL) {
        long credited = (long)time(NULL) - c->pause_started;
        if (credited < 0) {
            credited = 0;
        }
        app->env->deadline_credit += credited;
        journal_cmd(app, "resume", NULL, 0, credited);
    } else {
        journal_cmd(app, "resume", NULL, 0, -1);
    }
}

static char *status_reply(struct jc_app *app, struct jc_control *c)
{
    struct jc_control_status s;

    memset(&s, 0, sizeof(s));
    s.paused = c->paused;
    s.injected = c->injected;                       /* M1045 */
    s.queued_injects = c->pending_notes;  /* M1045-b: notes, not lines */
    s.last_tool = c->last_tool;
    s.mode = jc_agent_mode_name((enum jc_agent_mode)app->mode);  /* M304 */
    s.outcome = "running";
    /* M1149: the live pool's snapshot, or NULL (no batch). The builder emits
     * the "batch" object ONLY when non-NULL, so an idle run's reply is
     * byte-identical to today (driver cell 12's ST_BEFORE half). */
    s.batch = (c->pool_ops != NULL) ? c->pool_ops->snap : NULL;
    if (app->env != NULL) {
        s.run_id = app->env->run_id;
        s.elapsed = (long)time(NULL) - app->env->start_time;
        s.tokens_used = app->env->tokens_used;
        s.budget_tokens = app->env->budget_tokens;
        s.tool_calls = app->env->tool_calls;
        s.max_tool_calls = app->env->max_tool_calls;
        s.deadline_secs = app->env->deadline_secs;
        s.deadline_credit = app->env->deadline_credit;
        if (c->paused && c->pause_extend) {
            /* Reflect the in-flight credit an --extend pause is accruing. */
            long live = (long)time(NULL) - c->pause_started;
            if (live > 0) {
                s.deadline_credit += live;
            }
        }
        s.outcome = jc_env_outcome_name(app->env->outcome);
    } else {
        s.elapsed = (long)time(NULL) - c->started;
    }
    return jc_control_build_status(&s);
}

/* Serve every connection currently waiting (or, with wait_ms > 0, wait that
 * long for one). Returns the number of commands served. */
/* abort_mode (M801, refined M827/N14): what an ABORT served here does.
 *   0 = apply now (a tool boundary): set app->abort_flag, reply "graceful".
 *   1 = defer to the boundary (a long SHELL call): the wait loop does not act on
 *       app->abort_flag, so the abort is queued (c->deferred_abort) and applied at
 *       the next boundary -- reply "queued to the next tool boundary".
 *   2 = the spawn_parallel POOL: also deferred (c->deferred_abort), but the pool
 *       drains it each ~200 ms loop (jc_control_apply_deferred_abort) and stops the
 *       batch THEN, preserving every worker's work first (P1/M804) -- so the reply
 *       says the batch stops now and the work is kept, not "queued to a boundary"
 *       that a 90 s batch will not reach (N14).
 * Modes 1 and 2 both set c->deferred_abort; only the operator-facing reply differs.
 * Every other verb is safe mid-call: pause/resume set a flag the wait loops do not
 * block on, inject appends to the pending buffer, and status is read-only. */
static int serve_pending(struct jc_app *app, struct jc_control *c,
                         long wait_ms, int abort_mode)
{
    int served = 0;

    /* M1192 cell 4: remember the owning app so jc_control_close can still
     * serve a connection that lands in the backlog at the CLOSING boundary
     * (the driver's second cancel) after the agent loop has returned. The
     * app outlives this socket: main.c frees the run's env + arena only
     * AFTER jc_control_close, so the pointer is valid for that final drain. */
    c->app = app;
    for (;;) {
        fd_set rf;
        struct timeval tv;
        int fd;
        char *line;
        struct jc_control_cmd cmd;
        jc_status st;

        FD_ZERO(&rf);
        FD_SET(c->listen_fd, &rf);
        tv.tv_sec = wait_ms / 1000;
        tv.tv_usec = (wait_ms % 1000) * 1000;
        if (select(c->listen_fd + 1, &rf, NULL, NULL, &tv) <= 0) {
            return served;
        }
        fd = control_accept(c->listen_fd);
        jc_fd_cloexec(fd); /* M472: an accepted control conn is not a child's */
        if (fd < 0) {
            return served;
        }
        wait_ms = 0; /* drain the rest without waiting */

        line = read_request(fd);
        st = jc_control_parse_request(line, &cmd, jc_app_scratch(app));
        free(line);
        if (st == JC_ERR_PARSE) {
            write_reply(fd, jc_control_build_err("not a JSON object"));
            close(fd);
            continue;
        }
        if (st == JC_ERR_TOOBIG) {
            /* M1149: an inject-task whose text ALONE exceeds the per-task queue
             * cap. The codec will not store it; the refusal names the target
             * ("task <N>'s queue is full") -- behaviour 10. cmd.task is already
             * set (the index was validated before the text was length-checked). */
            char msg[96];
            jc_snprintf(msg, sizeof(msg),
                        "task %d's queue is full", cmd.task);
            write_reply(fd, jc_control_build_err(msg));
            close(fd);
            continue;
        }
        if (st != JC_OK || cmd.type == JC_CTL_UNKNOWN) {
            /* M752: the verb list is the codec's, not typed here -- this reply
             * named five verbs and omitted `mode` from M304 to M752. */
            char verbs[96], msg[192];
            jc_control_verb_list(verbs, sizeof(verbs), "|");
            jc_snprintf(msg, sizeof(msg),
                        "unknown command (%s; inject needs \"text\", "
                        "mode needs \"mode\")", verbs);
            write_reply(fd, jc_control_build_err(msg));
            close(fd);
            continue;
        }
        served++;
        switch (cmd.type) {
        case JC_CTL_STATUS:
            /* Read-only: not journaled (a supervisor may poll often). */
            write_reply(fd, status_reply(app, c));
            break;
        case JC_CTL_INJECT:
            if (c->pending.len + strlen(cmd.text) > CONTROL_PENDING_MAX) {
                write_reply(fd, jc_control_build_err("steering queue full"));
                break;
            }
            if (c->pending.len > 0) {
                jc_sb_append(&c->pending, "\n");
                jc_sb_append(&c->pending_from, "\n");
            }
            jc_sb_append(&c->pending, cmd.text);
            jc_sb_append(&c->pending_from, cmd.from != NULL ? cmd.from : "operator");
            c->injected++; /* M1045: accepted this run (the journal's live view) */
            c->pending_notes++; /* M1045-b: the note is now queued in `pending` */
            journal_cmd(app, "inject", cmd.text, 0, -1);
            write_reply(fd, jc_control_build_ok("applied at next model call"));
            break;
        case JC_CTL_PAUSE:
            c->paused = 1;
            c->pause_extend = cmd.extend;                 /* M162 */
            c->pause_started = (long)time(NULL);
            journal_cmd(app, "pause", NULL, cmd.extend, -1);
            write_reply(fd, jc_control_build_ok(cmd.extend
                ? "paused at tool boundary (--extend: deadline clock stopped)"
                : "paused at tool boundary (deadline keeps running)"));
            break;
        case JC_CTL_RESUME:
            if (c->paused && c->pause_extend) {
                pause_credit_apply(app, c);               /* journals it */
            } else {
                journal_cmd(app, "resume", NULL, 0, -1);
            }
            c->paused = 0;
            write_reply(fd, jc_control_build_ok(NULL));
            break;
        case JC_CTL_ABORT:
            if (abort_mode == 2) {
                c->deferred_abort = 1;
                write_reply(fd, jc_control_build_ok(
                    "aborting: the batch stops now and its workers' work is kept"));
            } else if (abort_mode == 1) {
                c->deferred_abort = 1;
                write_reply(fd, jc_control_build_ok(
                    "aborting (queued to the next tool boundary)"));
            } else {
                app->abort_flag = 1;
                journal_cmd(app, "abort", NULL, 0, -1);
                write_reply(fd, jc_control_build_ok("aborting (graceful)"));
            }
            break;
        case JC_CTL_MODE: {
            /* M304: NARROW the posture mid-run. Refused unless it strictly reduces
             * what the agent may do unattended -- the control channel's founding
             * rule is that it never widens, and a run an operator can loosen from
             * outside is a privilege-escalation surface wearing a convenience hat.
             *
             * The narrowing PERSISTS past this turn: an operator who said "plan
             * mode" meant it, and silently reverting at the next turn boundary
             * would be the same class of surprise as a gate that forgets. */
            enum jc_agent_mode from = (enum jc_agent_mode)app->mode;
            enum jc_agent_mode to = (enum jc_agent_mode)cmd.mode;
            if (from == to) {
                char m[96];
                jc_snprintf(m, sizeof(m), "already in %s mode",
                            jc_agent_mode_name(to));
                write_reply(fd, jc_control_build_ok(m));
                break;
            }
            if (!jc_perm_mode_narrows(from, to)) {
                char m[220];
                jc_snprintf(m, sizeof(m),
                    "refused: %s -> %s would WIDEN what the agent may do "
                    "unattended. The control channel only tightens; restart the "
                    "run to widen it.",
                    jc_agent_mode_name(from), jc_agent_mode_name(to));
                write_reply(fd, jc_control_build_err(m));
                break;
            }
            jc_app_set_mode(app, to);
            journal_cmd(app, "mode", jc_agent_mode_name(to), 0, -1);
            {
                char m[160];
                jc_snprintf(m, sizeof(m),
                            "narrowed to %s mode at the tool boundary%s",
                            jc_agent_mode_name(to),
                            to == JC_MODE_PLAN
                              ? " (read-only from here)" : "");
                write_reply(fd, jc_control_build_ok(m));
            }
            break;
        }
        case JC_CTL_STOP_TASK: {
            /* M1149: stop one pool worker by index. The hook is NULL when no
             * batch is live -> the EXACT refusal "no batch is live". A refusal
             * signals nothing (no child touched) and is NOT journaled; an
             * accepted stop IS journaled (index + reason visible). The pool
             * acts on it at its next ~200 ms wake (kill_one), so the reply
             * names the wake, not an instant. */
            char why[192];
            char jtxt[256];
            jc_status rst;
            if (c->pool_ops == NULL || c->pool_ops->stop == NULL) {
                write_reply(fd, jc_control_build_err("no batch is live"));
                break;
            }
            rst = c->pool_ops->stop(cmd.task, cmd.reason, why, sizeof(why));
            if (rst != JC_OK) {
                write_reply(fd, jc_control_build_err(why));
                break;
            }
            jc_snprintf(jtxt, sizeof(jtxt), "task=%d%s%s",
                        cmd.task,
                        cmd.reason != NULL && cmd.reason[0] != '\0'
                            ? " reason=" : "",
                        cmd.reason != NULL ? cmd.reason : "");
            journal_cmd(app, "stop-task", jtxt, 0, -1);
            write_reply(fd, jc_control_build_ok(
                "stopping the task at the next pool wake"));
            break;
        }
        case JC_CTL_INJECT_TASK: {
            /* M1149: steer one pool worker by index. Same NULL-hook / refusal
             * shape as stop-task; the text follows the INJECT journal's
             * convention (the steering text, bounded + scrubbed), prefixed with
             * the target index so a reader joins it to the right worker.
             * M1149-b2: the journal text is BOUNDED to JC_CONTROL_LINE_MAX
             * (journal_cmd scrubs + truncates to that cap anyway), so it is
             * heap-allocated -- a 65 KB stack frame was the wave-2 review's
             * hygiene finding. The bound is the same text cap the journal
             * applies, so nothing is kept that would not have been kept. */
            char why[192];
            char *jtxt = NULL;
            jc_status rst;
            if (c->pool_ops == NULL || c->pool_ops->inject == NULL) {
                write_reply(fd, jc_control_build_err("no batch is live"));
                break;
            }
            rst = c->pool_ops->inject(cmd.task, cmd.text, why, sizeof(why));
            if (rst != JC_OK) {
                write_reply(fd, jc_control_build_err(why));
                break;
            }
            jtxt = (char *)malloc(JC_CONTROL_LINE_MAX + 64);
            if (jtxt == NULL) {
                journal_cmd(app, "inject-task", NULL, 0, -1);
                write_reply(fd, jc_control_build_ok(
                    JC_CTL_INJECT_TASK_NOTE));
                break;
            }
            jc_snprintf(jtxt, JC_CONTROL_LINE_MAX + 64, "task=%d %s",
                        cmd.task,
                        cmd.text != NULL ? cmd.text : "");
            journal_cmd(app, "inject-task", jtxt, 0, -1);
            free(jtxt);
            write_reply(fd, jc_control_build_ok(
                JC_CTL_INJECT_TASK_NOTE));
            break;
        }
        case JC_CTL_CANCEL_CMD: {
            /* M1192 B3: cancel the CURRENT command. LEAD form (task == -1,
             * the field was absent): set app->cmd_cancel (the watched
             * command's select loop notices it and takes the cancel kill-arm,
             * D2) -- but refuse EXACTLY "no command is running" when no shell
             * command is in flight (B3.7). WORKER form (task >= 0): route to
             * the pool's cancel op (SIGUSR1 -> the child's abort flag). The
             * refusals are EXACT (the handler replies with *why verbatim):
             *   no live pool        -> "no batch is live"
             *   dead / out-of-range -> "task N is not live (M tasks)"
             *   live, not in cmd    -> "task N is not in a command (tool: X)"
             *   non-cancellable cmd -> "task N's command is not cancellable"
             * An accepted cancel is journaled; a refusal signals nothing and
             * is NOT journaled (the stop-task shape). */
            char why[192];
            char jtxt[256];
            jc_status rst;
            if (cmd.task < 0) {
                /* LEAD form: cancel the lead's own current shell command. */
                if (!app->cmd_in_flight) {
                    /* B3.7: the lead is not currently inside a shell command,
                     * so there is nothing to cancel. */
                    write_reply(fd, jc_control_build_err(
                        "no command is running"));
                    break;
                }
                app->cmd_cancel = 1;
                if (cmd.reason != NULL && cmd.reason[0] != '\0') {
                    jc_snprintf(app->cmd_cancel_reason,
                                sizeof(app->cmd_cancel_reason), "%s",
                                cmd.reason);
                } else {
                    app->cmd_cancel_reason[0] = '\0';
                }
                jc_snprintf(jtxt, sizeof(jtxt), "task=-1%s%s",
                            cmd.reason != NULL && cmd.reason[0] != '\0'
                                ? " reason=" : "",
                            cmd.reason != NULL ? cmd.reason : "");
                journal_cmd(app, "cancel-cmd", jtxt, 0, -1);
                write_reply(fd, jc_control_build_ok(
                    "cancelling the lead's current command"));
                break;
            }
            /* WORKER form: cancel one pool task's current command. */
            if (c->pool_ops == NULL || c->pool_ops->cancel == NULL) {
                write_reply(fd, jc_control_build_err("no batch is live"));
                break;
            }
            rst = c->pool_ops->cancel(cmd.task, cmd.reason, why, sizeof(why));
            if (rst != JC_OK) {
                write_reply(fd, jc_control_build_err(why));
                break;
            }
            jc_snprintf(jtxt, sizeof(jtxt), "task=%d%s%s",
                        cmd.task,
                        cmd.reason != NULL && cmd.reason[0] != '\0'
                            ? " reason=" : "",
                        cmd.reason != NULL ? cmd.reason : "");
            journal_cmd(app, "cancel-cmd", jtxt, 0, -1);
            {
                char m[96];
                /* D2: the reply MUST name the target ("task N"), per the
                 * driver's cell-2 assertion. */
                jc_snprintf(m, sizeof(m),
                            "cancelling task %d's current command "
                            "(SIGUSR1 at the next pool wake)",
                            cmd.task);
                write_reply(fd, jc_control_build_ok(m));
            }
            break;
        }
        default:
            write_reply(fd, jc_control_build_err("unknown command"));
            break;
        }
        close(fd);
    }
}

/* True once the envelope's wall-clock deadline has passed -- a pause must not
 * outlive it (the budget check right after the boundary will trip DEADLINE). */
static int deadline_passed(const struct jc_app *app)
{
    if (app->env == NULL || app->env->deadline_secs <= 0) {
        return 0;
    }
    return ((long)time(NULL) - app->env->start_time)
               >= app->env->deadline_secs;
}

int jc_control_boundary(struct jc_app *app, struct jc_history *hist)
{
    struct jc_control *c;

    if (app == NULL || app->control == NULL) {
        return 0;
    }
    c = app->control;

    /* M1149: the POOL CHILD's inject drain. Guarded by the child-only flag the
     * pool sets in run_child (the lead never sets it), so it is reached here
     * BEFORE the serve_pid guard below -- a FORKED child inherits a different
     * pid and must not serve the lead's socket, but it must still fold its
     * parent's queued steering into ITS OWN history. The drain is non-blocking
     * (the parent wrote the queue at its ~200 ms wake); whatever is available
     * up to a newline folds as ONE "[operator] " user message into `hist` (the
     * M31 cached prefix stays byte-stable, the text lands in the NEXT model
     * call). A partial line (no newline yet) is left in a process-static
     * buffer to complete at a later boundary -- there is exactly one draining
     * child per process (the pool forks each child, and the lead itself never
     * sets the flag), so the buffer cannot be shared between drainers. It does
     * NOT touch the lead's `c->pending` buffer, and must not fire when `hist`
     * is NULL (the periodic service point passes hist=NULL). */
    if (app->pool_inject_fd_valid && hist != NULL) {
        static struct jc_sb sb_pending;
        int folded = 0;
        char rb[512];
        char *nl;
        char *line;
        long oldfl;
        ssize_t r;
        jc_size ll;
        jc_size rest;
        if (sb_pending.data == NULL) {
            jc_sb_init(&sb_pending);
        }
        for (;;) {
            oldfl = fcntl(app->pool_inject_fd, F_GETFL);
            if (oldfl >= 0) {
                (void)fcntl(app->pool_inject_fd, F_SETFL,
                            oldfl | O_NONBLOCK);
            }
            r = read(app->pool_inject_fd, rb, sizeof(rb) - 1);
            if (oldfl >= 0) {
                (void)fcntl(app->pool_inject_fd, F_SETFL, oldfl);
            }
            if (r <= 0) {
                break;   /* EAGAIN (nothing more) or 0 (parent closed the write end) */
            }
            rb[r] = '\0';
            jc_sb_append(&sb_pending, rb);
            /* Fold every complete newline-terminated line (one [operator] msg
             * each; in practice the parent sends whole notes). */
            for (;;) {
                nl = (char *)memchr(sb_pending.data != NULL
                                        ? sb_pending.data : "",
                                    '\n', sb_pending.len);
                if (nl == NULL) {
                    break;
                }
                ll = (jc_size)(nl - sb_pending.data);
                line = (char *)malloc(ll + 1);
                if (line == NULL) {
                    break;
                }
                memcpy(line, sb_pending.data, ll);
                line[ll] = '\0';
                /* Drop the line + its newline from the pending buffer. */
                rest = sb_pending.len - ll - 1;
                memmove(sb_pending.data, nl + 1, rest);
                sb_pending.len = rest;
                sb_pending.data[rest] = '\0';
                if (line[0] != '\0') {
                    jc_history_add_operator(hist, line);
                    folded = 1;
                }
                free(line);
            }
        }
        return folded;
    }

    /* SV:1848 (M1103): the serve guard is the socket's OWNER, not the depth.
     * A FORKED pool child inherits the listening fd and a different pid, and
     * must not answer it -- or it would steal the lead's supervisor
     * connection. An IN-PROCESS subagent raises agent_depth in the very
     * process that owns the socket (jc_tool_subagent.c), so it keeps the
     * right pid and serves: the old agent_depth != 0 guard darkened the whole
     * child run (status/pause/abort unanswered for minutes, SV:1848). The
     * inject FOLD below keeps its depth guard: folding steering into a
     * subagent's history would be wrong, so that stays depth-0 only. */
    if (c->listen_fd < 0 ||
        c->serve_pid != (long)getpid()) {
        return 0;
    }

    /* M801: apply an abort that arrived during a tool call, now that we are at a
     * safe boundary (no batch is mid-flight to lose worktrees to). */
    if (c->deferred_abort) {
        c->deferred_abort = 0;
        app->abort_flag = 1;
        journal_cmd(app, "abort", NULL, 0, -1);
    }

    serve_pending(app, c, 0, 0);

    /* A pause blocks HERE -- at the boundary, never mid-tool -- in 500ms
     * slices so resume/abort/status stay serviceable and Ctrl-C/SIGTERM
     * still win. The default pause loses to the deadline (wall-clock
     * honesty); an --extend pause freezes it (the paused time is credited
     * back on exit), so only resume/abort end it. */
    while (c->paused && !app->abort_flag &&
           (c->pause_extend || !deadline_passed(app))) {
        serve_pending(app, c, 500, 0);
    }
    if (c->paused && !c->pause_extend && deadline_passed(app)) {
        c->paused = 0;
        jc_logf(JC_LOG_WARN,
                "control: deadline passed while paused; resuming to stop");
    }
    /* Left the pause on a path other than RESUME (abort / Ctrl-C) while
     * --extend was live: still settle the credit so the books balance. */
    if (c->pause_extend) {
        pause_credit_apply(app, c);
        c->paused = 0;
    }

    /* Fold queued steering into history as ONE user-role message. The
     * provenance prefix is explicit for the model and the transcript; the fold
     * uses jc_history_add_with_prefix if the inject had a `from` field, otherwise
     * jc_history_add_operator (shared with the TUI type-ahead queue M254).
     *
     * SV:1848 (M1103): the fold stays DEPTH-guarded even though serving above is
     * pid-guarded. `c->pending` is the run-global steering buffer; folding it
     * into a subagent's history would steer the child with top-level instructions
     * and drain the buffer before the lead ever reads it. So the owner serves
     * status/pause/resume at any depth, but only depth 0 folds -- a child
     * boundary (hist = the child's history) serves but leaves the buffer
     * queued for the lead's next top-level boundary. */
    if (app->agent_depth == 0 && c->pending.len > 0 && hist != NULL) {
        const char *from;
        /* The pending_from buffer mirrors pending: same number of lines, one
         * per inject. Read the last line from pending_from to get the sender. */
        from = jc_sb_last_line(&c->pending_from);
        if (from != NULL && strcmp(from, "operator") != 0) {
            /* Custom sender prefix: [sender] */
            struct jc_sb prefix;
            char buf[256];
            jc_sb_init(&prefix);
            jc_snprintf(buf, sizeof(buf), "[%s] ", from);
            jc_sb_append(&prefix, buf);
            jc_history_add_with_prefix(hist, prefix.data, c->pending.data);
            jc_sb_free(&prefix);
        } else {
            /* No from field or "operator": use [operator] prefix */
            jc_history_add_operator(hist, c->pending.data);
        }
        jc_sb_free(&c->pending);
        jc_sb_init(&c->pending);
        jc_sb_free(&c->pending_from);
        jc_sb_init(&c->pending_from);
        c->pending_notes = 0; /* M1045-b: the fold emptied the buffer */
        return 1;
    }
    return 0;
}

void jc_control_poll(struct jc_app *app)
{
    /* Deliberately the same code path with hist = NULL: the fold is the only
     * part that is unsafe mid-round, and passing NULL is exactly how the boundary
     * already expresses "do not fold". One implementation, so a future change to
     * the pause semantics cannot apply to one caller and not the other. */
    (void)jc_control_boundary(app, NULL);
}

int jc_control_apply_deferred_abort(struct jc_app *app)
{
    /* M804 (L1b): apply an abort that arrived mid-call NOW, from inside the
     * parallel pool. Safe as of P1: the pool's teardown preserves every child's
     * worktree before it is removed, so killing the batch early loses no work. The
     * shell wait loop does NOT call this -- there abort still waits for the
     * boundary, unchanged. Returns 1 if an abort was pending. */
    if (app == NULL || app->control == NULL || !app->control->deferred_abort) {
        return 0;
    }
    app->control->deferred_abort = 0;
    app->abort_flag = 1;
    journal_cmd(app, "abort", NULL, 0, -1);
    return 1;
}

void jc_control_poll_during_call(struct jc_app *app, int in_pool)
{
    struct jc_control *c;
    /* M801 (L1a): served from inside a long tool call / spawn_parallel batch so a
     * supervisor's `status` is answered now instead of at the next boundary (which
     * a 20 s call or batch pushes ~15 s away). Owned-process-guarded (SV:1848):
     * a FORKED pool child inherits the listening fd and a different pid and must
     * not answer it -- or it would steal the lead's supervisor connection. An
     * IN-PROCESS subagent runs its shell tool at agent_depth 1 in the very process
     * that owns the socket, so it keeps the right pid and serves: the old
     * agent_depth != 0 guard darkened the whole child run (SV:1848). Non-blocking
     * (wait 0) and abort-deferred: it never blocks on pause and never sets
     * abort_flag here. */
    if (app == NULL || app->control == NULL) {
        return;
    }
    c = app->control;
    if (c->listen_fd < 0 || c->serve_pid != (long)getpid()) {
        return;
    }
    (void)serve_pending(app, c, 0, in_pool ? 2 : 1);
}
