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
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/stat.h>

/* Cap the total queued steering text (several injects between boundaries). */
#define CONTROL_PENDING_MAX (4 * JC_CONTROL_LINE_MAX)

void jc_control_default_path(char *out, jc_size cap)
{
    jc_snprintf(out, cap, "%s/.jichi.d/control/%ld.sock",
                jc_home_dir(), (long)getpid());
}

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
    return JC_OK;
}

void jc_control_close(struct jc_control *c)
{
    if (c == NULL) {
        return;
    }
    if (c->listen_fd >= 0) {
        close(c->listen_fd);
        c->listen_fd = -1;
    }
    if (c->path[0] != '\0') {
        unlink(c->path);
    }
    jc_sb_free(&c->pending);
}

/* Read one bounded request line from `fd` (the client writes immediately;
 * wait up to 2s). Returns malloc'd line (no newline) or NULL. */
static char *read_request(int fd)
{
    char *buf;
    jc_size used = 0;
    long deadline = (long)time(NULL) + 2;

    buf = (char *)malloc(JC_CONTROL_LINE_MAX + 1);
    if (buf == NULL) {
        return NULL;
    }
    while (used < JC_CONTROL_LINE_MAX && (long)time(NULL) <= deadline) {
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
        n = (long)read(fd, buf + used, JC_CONTROL_LINE_MAX - used);
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
    s.last_tool = c->last_tool;
    s.mode = jc_agent_mode_name((enum jc_agent_mode)app->mode);  /* M304 */
    s.outcome = "running";
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
static int serve_pending(struct jc_app *app, struct jc_control *c,
                         long wait_ms)
{
    int served = 0;

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
        fd = accept(c->listen_fd, NULL, NULL);
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
        if (st != JC_OK || cmd.type == JC_CTL_UNKNOWN) {
            write_reply(fd, jc_control_build_err(
                "unknown command (status|inject|pause|resume|abort; "
                "inject needs \"text\")"));
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
            }
            jc_sb_append(&c->pending, cmd.text);
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
            app->abort_flag = 1;
            journal_cmd(app, "abort", NULL, 0, -1);
            write_reply(fd, jc_control_build_ok("aborting (graceful)"));
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

    if (app == NULL || app->control == NULL || app->agent_depth != 0) {
        return 0;
    }
    c = app->control;
    if (c->listen_fd < 0) {
        return 0;
    }

    serve_pending(app, c, 0);

    /* A pause blocks HERE -- at the boundary, never mid-tool -- in 500ms
     * slices so resume/abort/status stay serviceable and Ctrl-C/SIGTERM
     * still win. The default pause loses to the deadline (wall-clock
     * honesty); an --extend pause freezes it (the paused time is credited
     * back on exit), so only resume/abort end it. */
    while (c->paused && !app->abort_flag &&
           (c->pause_extend || !deadline_passed(app))) {
        serve_pending(app, c, 500);
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
     * itself is jc_history_add_operator, shared with the TUI type-ahead queue
     * (M254) so both steering paths produce the identical wire shape. */
    if (c->pending.len > 0 && hist != NULL) {
        jc_history_add_operator(hist, c->pending.data);
        jc_sb_free(&c->pending);
        jc_sb_init(&c->pending);
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
