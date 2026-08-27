/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_parallel.c - the spawn_parallel tool.
 *
 * Runs several subagents concurrently in a fork-based worker pool sized to the
 * CPU. Read-only tasks run against the live workspace; "write" tasks each run
 * in an isolated git worktree (from the snapshot shadow repo) and the parent
 * merges their file changes back file-level, first-wins, conflicts reported.
 * Each child returns {answer,tokens,error} as JSON over a pipe; the parent
 * gathers with select (honouring the abort flag) and reaps every child. The
 * pure cores (sizing, change parsing, the merge claim) live in jc_parallel.c.
 * See docs/PARALLEL.md. */

#include "jc_delegreport.h"
#include "tool_util.h"
#include "jc_app.h"
#include "jc_agent.h"
#include "jc_provider.h"
#include "jc_sysmsg.h"
#include "jc_message.h"
#include "jc_snapshot.h"
#include "jc_envelope.h"
#include "jc_parallel.h"
#include "jc_workerpool.h"
#include "jc_index.h"
#include "jc_path.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_platform.h"
#include "jc_proc.h"   /* jc_pipe_cloexec (M472) */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>

#define JC_PAR_MAX_TASKS  32
#define JC_PAR_CEILING     8
#define JC_PAR_ANSWER_CAP  16384
#define JC_PAR_TASK_TIMEOUT_DEFAULT 300   /* per-child watchdog (s); 0 cfg => this */
#define JC_PAR_TERM_GRACE_MS        300   /* SIGTERM -> SIGKILL grace window */

/* One subtask: resolved plan (parent) + runtime state (pool) + parsed result. */
struct ptask {
    const char               *task;       /* prompt                          */
    const char               *model_sel;  /* resolved selector, or NULL      */
    const struct jc_agentdef *def;        /* agent profile, or NULL          */
    int                       write;      /* edit in an isolated worktree    */
    int                       invalid;    /* skipped before fork             */
    char                      wt[1200];   /* worktree path ("" if none)      */
    char                      note[160];  /* a fallback/skip note, or ""     */

    pid_t                     pid;
    int                       fd;         /* pipe read end, -1 when closed   */
    int                       launched;
    int                       done;
    long                      launch_ms;  /* monotonic stamp at fork (watchdog) */
    double                    tok_budget;  /* per-child token slice (0 => none) */
    int                       tool_budget; /* per-child tool-call slice (-1 none) */
    struct jc_sb              out;        /* unconsumed child pipe bytes     */
    const char               *model_name; /* resolved, for the board         */
    char                      cur_tool[48];/* last tool the child ran         */
    int                       finished;   /* a "done" message was parsed     */

    char                     *answer;     /* parsed (malloc), or NULL        */
    char                     *error;      /* parsed (malloc), or NULL        */
    double                    tokens;
    int                       tool_calls; /* parsed from the child (budget)  */
    /* M437: the child's own report, parsed from its "done" message. */
    char                      stop[16];
    char                      ftool[JC_DELEG_TOOL_MAX];
    char                      fmsg[JC_DELEG_MSG_MAX];
    int                       fcls;
    /* M437: the paths this write child actually changed, bounded and rendered as
     * it goes -- the one report field the fork pool can fill and the synchronous
     * spawn_subagent cannot, because a worktree gives a per-delegate baseline
     * that a nested in-process run has no equivalent of. */
    char                      changed[JC_DELEG_MSG_MAX];
};

/* Child-side callback state: serializes progress to the result pipe. */
struct child_cb {
    int    wfd;
    double total;
    int    tools;   /* tool calls this child made (merged into the budget) */
};

static void write_all(int fd, const char *s, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, s + off, n - off);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        off += (size_t)w;
    }
}

/* Write one compact JSON object + '\n' to the pipe (newline-framed). */
static void child_emit(int wfd, cJSON *o)
{
    char *s = jc_json_print(o);
    if (s != NULL) {
        write_all(wfd, s, strlen(s));
        write_all(wfd, "\n", 1);
        free(s);
    }
    cJSON_Delete(o);
}

/* M442 added the tool-call id to the callback. A fork-pool child does NOT forward
 * it: the parent renders a per-child board line and an aggregated report, never a
 * paired timeline, so an id would be bytes over the pipe that nothing reads. Named
 * and voided rather than silently accepted, so the omission is a decision. */
static void child_on_tool(void *u, const char *name, const char *args,
                          const char *id)
{
    struct child_cb *cc = (struct child_cb *)u;
    cJSON *o = cJSON_CreateObject();
    (void)args;
    (void)id;
    cc->tools++;
    cJSON_AddStringToObject(o, "t", "tool");
    cJSON_AddStringToObject(o, "name", name != NULL ? name : "?");
    child_emit(cc->wfd, o);
}

static void child_on_usage(void *u, double in_tok, double out_tok,
                           double cache_read, double cache_write)
{
    struct child_cb *cc = (struct child_cb *)u;
    cJSON *o = cJSON_CreateObject();
    (void)cache_read;   /* progress board reports total tokens only */
    (void)cache_write;
    cc->total += in_tok + out_tok;
    cJSON_AddStringToObject(o, "t", "tok");
    cJSON_AddNumberToObject(o, "n", cc->total);
    child_emit(cc->wfd, o);
}

/* In a forked child: run the subagent, streaming {tool}/{tok} progress lines to
 * the pipe, then a final {done,answer|error,tokens} line. Never returns to the
 * pool; the caller _exit()s. */
static void run_child(struct jc_app *app, struct ptask *t, int wfd)
{
    struct jc_history h;
    struct jc_provider *prov;
    struct jc_model_cfg *mc;
    struct jc_model_cfg *active;
    const char *sysmsg;
    char *answer = NULL;
    double before;
    double used;
    int cc_tools = 0;
    int include_mutating;
    int found = 1;
    cJSON *res;
    char *s;
    jc_status st;

    if (t->write && t->wt[0] != '\0') {
        if (chdir(t->wt) == 0) {
            char canon[JC_PATH_MAX];
            jc_snprintf(app->cwd, sizeof(app->cwd), "%s", t->wt);
            /* Move the path-fence root to the worktree too: otherwise a
             * write_file into it resolves outside app->root (still the real
             * workspace) and the fence -- on by default in --auto, the very
             * mode write tasks are for -- denies every write, silently
             * dropping all changes so nothing merges back. Safe in the forked
             * child (COW; the parent's app->root is untouched). */
            if (jc_path_resolve(t->wt, canon, sizeof(canon)) == JC_OK) {
                jc_snprintf(app->root, sizeof(app->root), "%s", canon);
            } else {
                jc_snprintf(app->root, sizeof(app->root), "%s", t->wt);
            }
        }
    }

    /* Per-child budget reservation (M62 #4): cap this child's slice of the
     * run's remaining budget so N siblings can't collectively N-times overspend
     * (the parent only reconciles real usage after the pool exits). Writes here
     * touch only the child's COW copy of the envelope, so the parent is
     * unaffected. */
    if (app->env != NULL) {
        if (t->tok_budget > 0.0) {
            app->env->budget_tokens = app->env->tokens_used + t->tok_budget;
        }
        if (t->tool_budget >= 0) {
            app->env->max_tool_calls = app->env->tool_calls + t->tool_budget;
        }
    }

    active = jc_config_model_at(&app->config, app->config.active);
    mc = jc_subagent_resolve_model(&app->config, t->model_sel, &found);
    if (mc == NULL || mc == active) {
        prov = app->provider;
    } else {
        prov = jc_provider_create(mc);
        if (prov == NULL) {
            prov = app->provider;
        }
    }
    include_mutating = (t->write && !app->readonly) ? 1 : 0;
    /* M596: a profile body is the identity paragraph, and the sections jichi
     * enforces at depth (untrusted rule, constraints, edit scope -- M434) are
     * appended after it. Before M596 the bare profile text WAS the whole prompt,
     * so a profiled delegate was fenced by rules it had never seen. */
    sysmsg = jc_sysmsg_build_sub_as(app,
                                    (t->def != NULL) ? t->def->system_prompt : NULL,
                                    NULL);

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, t->task);
    before = (app->env != NULL) ? app->env->tokens_used : 0.0;
    {
        struct child_cb cc;
        struct jc_agent_callbacks cb;
        cc.wfd = wfd;
        cc.total = 0.0;
        cc.tools = 0;
        memset(&cb, 0, sizeof(cb));
        cb.on_tool_start = child_on_tool;
        cb.on_usage = child_on_usage;
        cb.user = &cc;
        app->agent_depth++;
        st = jc_agent_run_subagent(app, &h, prov, sysmsg, include_mutating,
                                   app->config.max_subagent_iters,
                                   (t->def != NULL && t->def->tools.len > 0)
                                       ? &t->def->tools : NULL,
                                   &cb, &answer);
        app->agent_depth--;
        /* Prefer the streamed running total; fall back to the envelope delta. */
        used = (cc.total > 0.0) ? cc.total
             : ((app->env != NULL) ? app->env->tokens_used - before : 0.0);
        cc_tools = cc.tools;
    }

    res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "t", "done");
    if (st == JC_OK && answer != NULL && answer[0] != '\0') {
        cJSON_AddStringToObject(res, "answer", answer);
    } else if (st == JC_ERR_ABORTED) {
        cJSON_AddStringToObject(res, "error", "interrupted");
    } else if (st != JC_OK) {
        cJSON_AddStringToObject(res, "error", "run failed");
    } else {
        cJSON_AddStringToObject(res, "error", "no final answer");
    }
    cJSON_AddNumberToObject(res, "tokens", used);
    cJSON_AddNumberToObject(res, "tools", (double)cc_tools);
    /* M437: the child's stop reason and last failing call. A capped parallel child
     * previously got NO note at all -- spawn_subagent had carried one since M62,
     * so a truncated answer from the fork pool was indistinguishable from a
     * complete one, which is the worse of the two defects this closes. Sent as
     * separate fields, not a rendered string, so the PARENT renders every child
     * through the same jc_delegreport_render as the synchronous tool. */
    cJSON_AddStringToObject(res, "stop", jc_delegreport_stop_name(
        jc_delegreport_stop_from(st == JC_OK, st == JC_ERR_ABORTED,
                                 app->last_run_capped,
                                 app->last_run_budget_stopped,
                                 answer != NULL && answer[0] != '\0')));
    if (app->last_fail_tool[0] != '\0') {
        cJSON_AddStringToObject(res, "ftool", app->last_fail_tool);
        cJSON_AddStringToObject(res, "fmsg", app->last_fail_msg);
        cJSON_AddNumberToObject(res, "fcls", (double)app->last_fail_cls);
    }
    s = jc_json_print(res);
    if (s != NULL) {
        write_all(wfd, s, strlen(s));
        write_all(wfd, "\n", 1);
        free(s);
    }
    cJSON_Delete(res);
    jc_history_free(&h);
}

/* Copy (A/M) or delete (D) one path from worktree `wt` into the live workspace
 * (app->cwd). Returns 1 on success. */
static int apply_change(struct jc_app *app, const char *wt, char status,
                        const char *rel)
{
    char dst[2048];

    jc_snprintf(dst, sizeof(dst), "%s/%s", app->cwd, rel);
    if (status == 'D') {
        remove(dst);
        return 1;
    } else {
        char src[2048];
        char dstdir[2048];
        char *slash;
        char *content = NULL;
        jc_size len = 0;

        jc_snprintf(src, sizeof(src), "%s/%s", wt, rel);
        /* The content is written to dst on the next line, so it is PER-CALL
         * transient: read it onto the per-tool-call arena, not the per-turn one
         * (M610). apply_change is called once per changed path, so on
         * jc_app_scratch (per turn) a merge of M files retained M copies to
         * turn end; jc_app_tool_scratch is reset before the next tool call, and
         * is safe here for the same reason its M199 sibling below is -- the
         * write children are forked processes, so no nested in-process agent
         * run resets this arena mid-merge. (M62's original note predated the
         * per-call arena; this is the correction it was owed.) */
        if (jc_read_file(src, &content, &len, jc_app_tool_scratch(app)) != JC_OK) {
            return 0;
        }
        jc_snprintf(dstdir, sizeof(dstdir), "%s", dst);
        slash = strrchr(dstdir, '/');
        if (slash != NULL) {
            *slash = '\0';
            jc_mkdir_p(dstdir);
        }
        return jc_write_file(dst, content, len) == JC_OK ? 1 : 0;
    }
}

/* Emit one indented board line for task `idx`, only if a front-end wants it. */
static void board_line(struct jc_app *app, int idx, const char *model,
                       int state, const char *detail, double tokens)
{
    char s[400];
    if (app->cb == NULL || app->cb->on_status == NULL) {
        return; /* headless / no status sink: board is purely additive */
    }
    jc_parallel_board_line(s, sizeof(s), idx, model, state, detail, tokens);
    app->cb->on_status(app->cb->user, s);
}

/* Consume complete '\n'-framed messages from t->out, updating live state and
 * the board; a "done" message records the answer/error/tokens. */
static void process_lines(struct jc_app *app, struct ptask *t, int idx)
{
    for (;;) {
        char *nl;
        char *line;
        jc_size linelen;
        jc_size rest;
        struct jc_pmsg m;

        if (t->out.data == NULL) {
            break;
        }
        nl = (char *)memchr(t->out.data, '\n', t->out.len);
        if (nl == NULL) {
            break;
        }
        linelen = (jc_size)(nl - t->out.data);
        line = (char *)malloc(linelen + 1);
        if (line == NULL) {
            break;
        }
        memcpy(line, t->out.data, linelen);
        line[linelen] = '\0';
        rest = t->out.len - linelen - 1;
        memmove(t->out.data, t->out.data + linelen + 1, rest);
        t->out.len = rest;
        t->out.data[rest] = '\0';

        switch (jc_parallel_parse_msg(line, &m)) {
        case JC_PMSG_TOOL: {
            jc_snprintf(t->cur_tool, sizeof(t->cur_tool), "%s", m.tool);
            board_line(app, idx, t->model_name, JC_BOARD_RUN, m.tool,
                       t->tokens);
            break;
        }
        case JC_PMSG_TOK:
            t->tokens = m.tokens;
            break;
        case JC_PMSG_DONE: {
            t->answer = m.answer;
            t->error = m.error;
            m.answer = NULL;
            m.error = NULL;
            t->tokens = m.tokens;
            t->tool_calls = m.tool_calls;
            /* M437: carry the child's report to the aggregation below. */
            jc_snprintf(t->stop, sizeof t->stop, "%s", m.stop);
            jc_snprintf(t->ftool, sizeof t->ftool, "%s", m.ftool);
            jc_snprintf(t->fmsg, sizeof t->fmsg, "%s", m.fmsg);
            t->fcls = m.fcls;
            t->finished = 1;
            board_line(app, idx, t->model_name,
                       t->error != NULL ? JC_BOARD_FAIL : JC_BOARD_DONE,
                       t->error != NULL ? t->error : "", t->tokens);
            break;
        }
        default:
            break;
        }
        free(m.answer);
        free(m.error);
        free(line);
    }
}

static int eff_max_parallel(struct jc_app *app, int n_tasks)
{
    return jc_parallel_eff_max(n_tasks, app->config.max_parallel_agents,
                               jc_cpu_count(), JC_PAR_CEILING);
}

/* Reap a child that has already been sent SIGTERM: poll for it within a short
 * grace window, then SIGKILL and block, so a child that ignores or traps SIGTERM
 * can't wedge the parent (M62 #2). Delegates to the shared jc_workerpool
 * primitive (also used by the daemon worker pool). */
static void reap_grace(pid_t pid)
{
    jc_worker_reap_grace(pid, JC_PAR_TERM_GRACE_MS);
}

/* SIGTERM, then escalate to SIGKILL after a grace window, then reap one task. */
static void kill_one(struct ptask *t)
{
    if (!t->launched || t->done) {
        return;
    }
    if (t->fd >= 0) {
        close(t->fd);
        t->fd = -1;
    }
    if (t->pid > 0) {
        kill(t->pid, SIGTERM);
        reap_grace(t->pid);
    }
    t->done = 1;
}

/* Kill and reap any still-running children (on abort). SIGTERM them all first so
 * they shut down in parallel, then escalate+reap each (M62 #2). */
static void kill_live(struct ptask *tasks, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (tasks[i].launched && !tasks[i].done && tasks[i].pid > 0) {
            kill(tasks[i].pid, SIGTERM);
        }
    }
    for (i = 0; i < n; i++) {
        if (tasks[i].launched && !tasks[i].done) {
            if (tasks[i].fd >= 0) {
                close(tasks[i].fd);
                tasks[i].fd = -1;
            }
            reap_grace(tasks[i].pid);
            tasks[i].done = 1;
        }
    }
}

/* The fork pool: keep <= cap children live, gather each child's pipe with a
 * 200ms-sliced select (checking app->abort_flag), reap on EOF, refill. */
static void run_pool(struct jc_app *app, struct ptask *tasks, int ntasks,
                     int cap, long timeout_ms, int *aborted)
{
    int next = 0;
    int live = 0;
    int done = 0;
    int i;

    *aborted = 0;
    while (done < ntasks) {
        fd_set rf;
        struct timeval tv;
        int maxfd = -1;
        int rc;

        while (live < cap && next < ntasks) {
            struct ptask *t = &tasks[next];
            int fds[2];
            pid_t pid;

            if (t->invalid) {
                t->done = 1;
                done++;
                next++;
                continue;
            }
            if (jc_pipe_cloexec(fds) != 0) {
                t->error = jc_strdup("pipe failed");
                t->done = 1;
                done++;
                next++;
                continue;
            }
            pid = fork();
            if (pid < 0) {
                /* M325: say WHICH failure. A bare "fork failed" cannot tell
                 * EAGAIN (the process/thread limit -- lower maxParallelAgents,
                 * or raise the ulimit) from ENOMEM (out of memory -- fewer
                 * children, or a smaller model), and those want opposite
                 * responses. Seen for real: a workload where 3 of 6
                 * spawn_parallel failures were forks that could not happen, with
                 * nothing in the log to say why. */
                char forkerr[160];
                int e = errno;
                close(fds[0]);
                close(fds[1]);
                jc_snprintf(forkerr, sizeof(forkerr),
                            "fork failed: %s%s", strerror(e),
                            e == EAGAIN
                                ? " (process/thread limit -- lower "
                                  "maxParallelAgents or raise the ulimit)"
                                : (e == ENOMEM ? " (out of memory -- run fewer "
                                                 "children)" : ""));
                t->error = jc_strdup(forkerr);
                t->done = 1;
                done++;
                next++;
                continue;
            }
            if (pid == 0) {
                close(fds[0]);
                run_child(app, t, fds[1]);
                close(fds[1]);
                _exit(0);
            }
            close(fds[1]);
            t->pid = pid;
            t->fd = fds[0];
            t->launched = 1;
            t->launch_ms = (long)jc_now_millis();
            jc_sb_init(&t->out);
            /* Resolve the model name for the board and announce the launch. */
            {
                int f = 1;
                struct jc_model_cfg *mc =
                    jc_subagent_resolve_model(&app->config, t->model_sel, &f);
                t->model_name = (mc != NULL) ? mc->name : NULL;
            }
            board_line(app, next, t->model_name, JC_BOARD_RUN, "", 0.0);
            live++;
            next++;
        }

        if (live == 0) {
            break;
        }

        FD_ZERO(&rf);
        for (i = 0; i < ntasks; i++) {
            if (tasks[i].launched && !tasks[i].done && tasks[i].fd >= 0) {
                FD_SET(tasks[i].fd, &rf);
                if (tasks[i].fd > maxfd) {
                    maxfd = tasks[i].fd;
                }
            }
        }
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        rc = select(maxfd + 1, &rf, NULL, NULL, &tv);

        if (app->abort_flag) {
            kill_live(tasks, ntasks);
            *aborted = 1;
            return;
        }
        /* Per-child watchdog (M62 #1): a child wedged in a tool or a stalled
         * stream must not hang the whole swarm. Kill any task past its deadline
         * and report it failed; siblings keep running. */
        if (timeout_ms > 0) {
            long now = (long)jc_now_millis();
            for (i = 0; i < ntasks; i++) {
                struct ptask *t = &tasks[i];
                if (t->launched && !t->done &&
                    now - t->launch_ms > timeout_ms) {
                    kill_one(t);
                    if (t->error == NULL && t->answer == NULL) {
                        /* M325: name the knob and the limit that fired. This was
                         * the dominant spawn_parallel failure in a measured
                         * workload -- 3 of 6 -- on a project whose SUCCESSFUL
                         * parallel calls ran 300-462 s against this 300 s
                         * default. "sub-agent timed out" alone sends an operator
                         * looking for a hung child instead of at the setting. */
                        char toerr[160];
                        jc_snprintf(toerr, sizeof(toerr),
                                    "sub-agent timed out after %lds "
                                    "(parallelTaskTimeout) -- raise it if the "
                                    "task legitimately takes longer",
                                    timeout_ms / 1000L);
                        t->error = jc_strdup(toerr);
                    }
                    board_line(app, i, t->model_name, JC_BOARD_TIMEOUT,
                               "timed out", t->tokens);
                    done++;
                    live--;
                }
            }
            if (done >= ntasks) {
                return;
            }
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            kill_live(tasks, ntasks);
            return;
        }
        if (rc == 0) {
            continue;
        }
        for (i = 0; i < ntasks; i++) {
            struct ptask *t = &tasks[i];
            char buf[4096];
            ssize_t n;
            if (!t->launched || t->done || t->fd < 0 ||
                !FD_ISSET(t->fd, &rf)) {
                continue;
            }
            n = read(t->fd, buf, sizeof(buf));
            if (n > 0) {
                jc_sb_append_n(&t->out, buf, (jc_size)n);
                process_lines(app, t, i);
            } else {
                close(t->fd);
                t->fd = -1;
                waitpid(t->pid, NULL, 0);
                t->done = 1;
                done++;
                live--;
                if (!t->finished && t->answer == NULL && t->error == NULL) {
                    t->error = jc_strdup("sub-agent ended without a result");
                    board_line(app, i, t->model_name, JC_BOARD_FAIL,
                               "ended without a result", t->tokens);
                }
            }
        }
    }
}

static cJSON *parallel_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props = cJSON_GetObjectItem(s, "properties");
    cJSON *req = cJSON_GetObjectItem(s, "required");
    cJSON *tasks = cJSON_CreateObject();
    cJSON *items = cJSON_CreateObject();
    cJSON *iprops = cJSON_CreateObject();
    cJSON *p;

    cJSON_AddStringToObject(tasks, "type", "array");
    cJSON_AddStringToObject(tasks, "description",
        "The subtasks to run concurrently (each an autonomous sub-agent). Use "
        "independent, self-contained tasks; for editing, set write:true and "
        "give each task a disjoint set of files.");
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", "The subtask to complete.");
    cJSON_AddItemToObject(iprops, "task", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description",
        "Optional model selector (name/index/role).");
    cJSON_AddItemToObject(iprops, "model", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description",
        "Optional named agent profile (.jichi/agents/*.md).");
    cJSON_AddItemToObject(iprops, "agent", p);
    p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "boolean");
    cJSON_AddStringToObject(p, "description",
        "If true, the sub-agent may edit files; it runs in an isolated git "
        "worktree and its changes are merged back. Default false (read-only).");
    cJSON_AddItemToObject(iprops, "write", p);
    cJSON_AddStringToObject(items, "type", "object");
    cJSON_AddItemToObject(items, "properties", iprops);
    cJSON_AddItemToObject(tasks, "items", items);

    cJSON_AddItemToObject(props, "tasks", tasks);
    cJSON_AddItemToArray(req, cJSON_CreateString("tasks"));
    return s;
}

static jc_status parallel_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    cJSON *tasks_json = cJSON_GetObjectItem(args, "tasks");
    cJSON *e;
    struct ptask tasks[JC_PAR_MAX_TASKS];
    int ntasks = 0;
    int truncated = 0;
    int any_write = 0;
    int have_base = 0;
    int cap;
    int aborted = 0;
    int i;
    int applied = 0;
    int n_quarantined = 0;   /* M144: red-verify write children not merged */
    int n_ok = 0;
    char base[64];
    char wtbase[1100];
    struct jc_sb agg;
    struct jc_sb conflicts;
    struct jc_sb vfails;     /* M144: per-task verifier output tails */
    struct jc_vec seen;
    double tok_sum = 0.0;
    int tool_sum = 0;

    if (!jc_subagent_can_spawn(app->agent_depth,
                               app->config.max_subagent_depth)) {
        tu_err(out, "error: sub-agent depth limit reached");
        return JC_OK;
    }
    if (!cJSON_IsArray(tasks_json)) {
        tu_err(out, "error: 'tasks' must be an array of {task, ...} objects");
        return JC_OK;
    }

    /* Resolve each task up front so bad models/profiles are reported, not run. */
    memset(tasks, 0, sizeof(tasks));
    for (e = tasks_json->child; e != NULL; e = e->next) {
        struct ptask *t;
        const char *task_str;
        const char *arg_model;
        const char *agent_name;
        const struct jc_agentdef *def = NULL;
        const char *model_sel;
        int ro_ignored;
        int found = 1;

        if (ntasks >= JC_PAR_MAX_TASKS) {
            truncated = 1;
            break;
        }
        t = &tasks[ntasks];
        t->fd = -1;
        t->tokens = 0.0;
        t->tool_budget = -1; /* no cap unless the envelope reserves one below */

        task_str = (cJSON_IsObject(e)) ? jc_json_get_str(e, "task", NULL) : NULL;
        if (task_str == NULL || task_str[0] == '\0') {
            t->invalid = 1;
            t->error = jc_strdup("missing 'task'");
            t->task = "(invalid)";
            ntasks++;
            continue;
        }
        t->task = task_str; /* owned by args' cJSON tree (lives this call) */
        arg_model = jc_json_get_str(e, "model", NULL);
        agent_name = jc_json_get_str(e, "agent", NULL);
        t->write = (cJSON_IsTrue(cJSON_GetObjectItem(e, "write"))) ? 1 : 0;

        if (agent_name != NULL && agent_name[0] != '\0') {
            def = jc_agentdef_find(&app->agents, agent_name);
            if (def == NULL) {
                t->invalid = 1;
                t->error = jc_strdup("no such agent profile");
                ntasks++;
                continue;
            }
        }
        jc_agentdef_merge(def, arg_model, 0, 0, &model_sel, &ro_ignored);
        jc_subagent_resolve_model(&app->config, model_sel, &found);
        if (!found) {
            t->invalid = 1;
            t->error = jc_strdup("no model matches the selector");
            ntasks++;
            continue;
        }
        t->def = def;
        t->model_sel = model_sel;
        if (t->write) {
            any_write = 1;
        }
        ntasks++;
    }

    if (ntasks == 0) {
        tu_err(out, "error: no tasks provided");
        return JC_OK;
    }

    /* Pre-warm the workspace index so read-only children share it (incremental;
     * a no-op when no embed model is configured or it is already current). */
    {
        struct jc_model_cfg *em = jc_app_model_for_role(app, JC_ROLE_EMBED);
        if (em != NULL) {
            jc_index_build(app->cwd, em, 0, NULL, &app->index, NULL,
                           &app->abort_flag, &app->config.ignore_dirs);
        }
    }

    /* For write tasks, checkpoint the current tree and give each its own
     * worktree off that base. Degrade to read-only if snapshots are absent. */
    base[0] = '\0';
    if (any_write && jc_snapshot_available(app->snapshots)) {
        const char *c0;
        jc_snapshot_take(app->snapshots, "parallel base");
        c0 = jc_snapshot_commit(app->snapshots,
                                jc_snapshot_count(app->snapshots) - 1);
        if (c0 != NULL) {
            jc_snprintf(base, sizeof(base), "%s", c0);
            have_base = 1;
        }
    }
    if (have_base) {
        jc_snprintf(wtbase, sizeof(wtbase), "%s/.jichi.d/worktrees/%ld",
                    jc_home_dir(), (long)getpid());
        jc_mkdir_p(wtbase);
        for (i = 0; i < ntasks; i++) {
            if (tasks[i].invalid || !tasks[i].write) {
                continue;
            }
            jc_snprintf(tasks[i].wt, sizeof(tasks[i].wt), "%s/wt-%d",
                        wtbase, i);
            if (jc_snapshot_worktree_add(app->snapshots, base, tasks[i].wt)
                    != JC_OK) {
                tasks[i].wt[0] = '\0';
                tasks[i].write = 0;
                jc_snprintf(tasks[i].note, sizeof(tasks[i].note),
                            "(worktree unavailable; ran read-only)");
            }
        }
    } else if (any_write) {
        for (i = 0; i < ntasks; i++) {
            if (tasks[i].write) {
                tasks[i].write = 0;
                jc_snprintf(tasks[i].note, sizeof(tasks[i].note),
                            "(snapshots unavailable; ran read-only)");
            }
        }
    }

    cap = eff_max_parallel(app, ntasks);

    /* Reserve each child a 1/ntasks slice of the run's remaining budget (M62
     * #4): an upper bound that holds however the pool refills, since the parent
     * only learns real usage after the pool exits. Children inherit start_time,
     * so the deadline is shared.
     *
     * M431: these slices are now ENFORCED. Until then this comment claimed the
     * children self-checked, and they could not: jc_env_over_budget sat behind
     * env_active(), which requires agent_depth == 0, while run_child starts a
     * child at depth 1 -- so every slice computed here was applied to the
     * child's COW envelope and never once consulted, leaving the per-child
     * watchdog as the only real bound. env_budget_applies() (jc_agent.c) is the
     * predicate that fixed it. */
    if (app->env != NULL && ntasks > 0) {
        double rem_tok = (app->env->budget_tokens > 0.0)
            ? app->env->budget_tokens - app->env->tokens_used : 0.0;
        int rem_tools = (app->env->max_tool_calls > 0)
            ? app->env->max_tool_calls - app->env->tool_calls : -1;
        for (i = 0; i < ntasks; i++) {
            if (app->env->budget_tokens > 0.0) {
                tasks[i].tok_budget = (rem_tok > 0.0) ? rem_tok / ntasks : 0.0;
            }
            if (rem_tools >= 0) {
                /* Distribute the remainder so the slices sum to exactly
                 * rem_tools (no N-times overspend) AND a near-exhausted budget
                 * (rem_tools < ntasks) still lets the first rem_tools children
                 * make a call each, instead of truncating every slice to 0 and
                 * handing each child a zero tool budget. */
                tasks[i].tool_budget =
                    rem_tools / ntasks + (i < rem_tools % ntasks ? 1 : 0);
            }
        }
    }

    {
        long ttimeout = (app->config.parallel_task_timeout > 0)
            ? app->config.parallel_task_timeout
            : JC_PAR_TASK_TIMEOUT_DEFAULT;
        run_pool(app, tasks, ntasks, cap, ttimeout * 1000L, &aborted);
    }

    /* run_pool's streaming gather already set answer/error/tokens from each
     * child's "done" message (or an error on premature EOF); this is just a
     * final backstop. */
    for (i = 0; i < ntasks; i++) {
        if (tasks[i].launched && tasks[i].error == NULL &&
            tasks[i].answer == NULL) {
            tasks[i].error = jc_strdup("no result from sub-agent");
        }
        tok_sum += tasks[i].tokens;
        tool_sum += tasks[i].tool_calls;
        if (tasks[i].answer != NULL) {
            n_ok++;
        }
    }
    if (app->env != NULL) {
        /* Reconcile the children's real usage into the parent's budget: tokens
         * AND tool calls (M62 #3). Forked children meter into their COW copy of
         * the envelope, invisible to the parent, so without this merge a swarm
         * could evade max_tool_calls entirely. The parent's own struct was never
         * mutated by the children (COW writes stayed in their address spaces). */
        app->env->tokens_used += tok_sum;
        app->env->tool_calls += tool_sum;
    }

    /* Merge write tasks' file changes: first-wins by path, conflicts reported.
     * Only successful (answered) write children are merged. */
    jc_vec_init(&seen, sizeof(char *));
    jc_sb_init(&conflicts);
    jc_sb_init(&vfails);
    for (i = 0; i < ntasks; i++) {
        struct ptask *t = &tasks[i];
        struct jc_sb chg;
        struct jc_vec changes;
        jc_size k;

        if (!t->write || t->wt[0] == '\0' || t->answer == NULL) {
            continue;
        }
        /* Per-child verify gate (M144, opt-in `parallelVerify`): run the
         * configured verifier IN THE CHILD'S WORKTREE before its changes may
         * merge. A red child is quarantined -- reported, not merged -- so a
         * sibling's (or the parent's) tree never absorbs unverified breakage;
         * green children merge exactly as before. The worktree is the ideal
         * gate site: isolated cwd, the live tree untouched either way. */
        if (app->config.parallel_verify) {
            const char *vcmd = jc_parallel_verify_cmd(
                app->env != NULL ? app->env->verify_cmd : NULL,
                app->config.verify, app->config.test_command);
            if (vcmd != NULL) {
                struct jc_sb vout;
                int vcode;
                long vt = (app->env != NULL) ? app->env->verify_timeout : 0;
                board_line(app, i, t->model_name, JC_BOARD_RUN,
                           "verify (worktree)", t->tokens);
                jc_sb_init(&vout);
                vcode = jc_env_run_verify(vcmd, t->wt, &vout,
                                          &app->abort_flag, vt);
                if (vcode != 0) {
                    n_quarantined++;
                    jc_snprintf(t->note, sizeof(t->note),
                        "[worktree verify FAILED (exit %d) -- this task's "
                        "changes were NOT merged]", vcode);
                    board_line(app, i, t->model_name, JC_BOARD_FAIL,
                               "verify red: not merged", t->tokens);
                    /* A bounded tail of the verifier output, so the caller
                     * can see WHY without re-running anything. */
                    jc_sb_append_fmt(&vfails,
                        "### Task %d worktree verify (exit %d), output tail:\n",
                        i + 1, vcode);
                    if (vout.data != NULL && vout.len > 0) {
                        jc_size off = vout.len > 500 ? vout.len - 500 : 0;
                        jc_sb_append(&vfails, vout.data + off);
                        if (vout.data[vout.len - 1] != '\n') {
                            jc_sb_append(&vfails, "\n");
                        }
                    } else {
                        jc_sb_append(&vfails, "(no output)\n");
                    }
                    {
                        cJSON *j = jc_env_journal_begin(app->env,
                                                        "parallel_verify");
                        if (j != NULL) {
                            cJSON_AddNumberToObject(j, "task", (double)(i + 1));
                            cJSON_AddNumberToObject(j, "exit", (double)vcode);
                        }
                        jc_env_journal_end(app->env, j);
                    }
                    jc_sb_free(&vout);
                    continue; /* quarantined: gather no changes */
                }
                jc_sb_free(&vout);
                board_line(app, i, t->model_name, JC_BOARD_DONE,
                           "verify green", t->tokens);
            }
        }
        jc_sb_init(&chg);
        if (jc_snapshot_worktree_changes(app->snapshots, t->wt, base, &chg)
                != JC_OK || chg.data == NULL) {
            jc_sb_free(&chg);
            continue;
        }
        jc_vec_init(&changes, sizeof(struct jc_change));
        /* M199: per-call arena. The parsed change list is consumed by the merge
         * loop below and never referenced again, so it must not sit on the
         * session arena. Safe here: the children are forked processes, so no
         * nested in-process agent run can reset this arena mid-call. */
        jc_parallel_parse_changes(chg.data, jc_app_tool_scratch(app), &changes);
        for (k = 0; k < changes.len; k++) {
            struct jc_change *c =
                (struct jc_change *)jc_vec_at(&changes, k);
            /* Merge-time edit-scope fence (M133): even though each write child
             * enforces --edit-scope on its file tools, it could still touch a
             * file outside the scope via the shell inside its worktree. The
             * parent refuses to merge any out-of-scope change back. */
            if (app->env != NULL && app->env->edit_scope.len > 0 &&
                !jc_env_path_in_scope(app->env, app->root, c->path)) {
                jc_sb_append_fmt(&conflicts,
                                 "  %s (task %d, out of edit-scope: not merged)\n",
                                 c->path, i + 1);
                continue;
            }
            if (jc_parallel_claim(&seen, c->path)) {
                if (apply_change(app, t->wt, c->status, c->path)) {
                    applied++;
                    /* M437: record it for this child's report. Bounded by
                     * construction: once the buffer is nearly full an ellipsis is
                     * appended and nothing more is added, because a truncated list
                     * that does not SAY it is truncated reads as complete. */
                    if (t->changed[0] == '\0') {
                        jc_snprintf(t->changed, sizeof t->changed, "%s",
                                    c->path);
                    } else if (strlen(t->changed) + strlen(c->path) + 6
                                   < sizeof t->changed) {
                        jc_snprintf(t->changed + strlen(t->changed),
                                    sizeof t->changed - strlen(t->changed),
                                    ", %s", c->path);
                    } else if (strstr(t->changed, ", ...") == NULL &&
                               strlen(t->changed) + 6 < sizeof t->changed) {
                        jc_snprintf(t->changed + strlen(t->changed),
                                    sizeof t->changed - strlen(t->changed),
                                    ", ...");
                    }
                }
            } else {
                jc_sb_append_fmt(&conflicts, "  %s (task %d)\n",
                                 c->path, i + 1);
            }
        }
        jc_vec_free(&changes);
        jc_sb_free(&chg);
    }
    jc_vec_free(&seen);

    /* Remove all worktrees, then the (now-empty) per-run base directory. */
    for (i = 0; i < ntasks; i++) {
        if (tasks[i].wt[0] != '\0') {
            jc_snapshot_worktree_remove(app->snapshots, tasks[i].wt);
        }
    }
    if (have_base) {
        rmdir(wtbase);
    }

    /* Aggregate into one tool result. */
    jc_sb_init(&agg);
    jc_sb_append_fmt(&agg, "## Parallel agents: %d task(s), up to %d at once\n\n",
                     ntasks, cap);
    if (aborted) {
        jc_sb_append(&agg, "(interrupted; partial results below)\n\n");
    }
    for (i = 0; i < ntasks; i++) {
        struct ptask *t = &tasks[i];
        jc_sb_append_fmt(&agg, "### Task %d: %.70s%s\n", i + 1,
                         t->task != NULL ? t->task : "",
                         (t->task != NULL && strlen(t->task) > 70) ? "..." : "");
        if (t->note[0] != '\0') {
            jc_sb_append_fmt(&agg, "%s\n", t->note);
        }
        if (t->answer != NULL) {
            if (strlen(t->answer) > JC_PAR_ANSWER_CAP) {
                jc_sb_append_n(&agg, t->answer, JC_PAR_ANSWER_CAP);
                jc_sb_append(&agg, "\n... [answer truncated]\n");
            } else {
                jc_sb_append(&agg, t->answer);
                jc_sb_append(&agg, "\n");
            }
        } else {
            jc_sb_append_fmt(&agg, "(error: %s)\n",
                             t->error != NULL ? t->error : "unknown");
        }
        /* M437: the child's own report, rendered by the SAME function
         * spawn_subagent uses -- so the two delegation tools cannot describe one
         * outcome two ways. A child that reported nothing (an older message, or a
         * pre-fork skip) renders nothing, so this cannot invent a stop reason. */
        if (t->stop[0] != '\0') {
            struct jc_delegate_report cr;
            jc_delegreport_init(&cr);
            cr.stop = jc_delegreport_stop_parse(t->stop);
            cr.tokens = (t->tokens > 0.0) ? t->tokens : -1.0;
            cr.tool_calls = (t->tool_calls > 0) ? (long)t->tool_calls : -1;
            if (t->ftool[0] != '\0') {
                jc_snprintf(cr.fail_tool, sizeof cr.fail_tool, "%s", t->ftool);
                jc_snprintf(cr.fail_msg, sizeof cr.fail_msg, "%s", t->fmsg);
                cr.fail_cls = (enum jc_fail_class)t->fcls;
            }
            /* The one field the fork pool CAN fill and the sync tool cannot: a
             * write child ran in its own worktree, whose changed files the parent
             * already parsed to decide the merge. */
            if (t->write && t->changed[0] != '\0') {
                jc_snprintf(cr.files_changed, sizeof cr.files_changed, "%s",
                            t->changed);
            }
            jc_delegreport_render(&cr, &agg);
            jc_sb_append(&agg, "\n");
        }
        jc_sb_append(&agg, "\n");
    }
    if (any_write) {
        jc_sb_append_fmt(&agg, "## Merge: %d file(s) applied", applied);
        if (n_quarantined > 0) {
            jc_sb_append_fmt(&agg, "; %d task(s) QUARANTINED by the verify "
                             "gate (changes not merged)", n_quarantined);
        }
        if (conflicts.len > 0) {
            jc_sb_append(&agg, ", conflicts skipped (first writer kept):\n");
            jc_sb_append(&agg, conflicts.data);
        } else {
            jc_sb_append(&agg, ", no conflicts.\n");
        }
        if (vfails.len > 0) {
            jc_sb_append(&agg, "\n");
            jc_sb_append(&agg, vfails.data);
        }
    }
    if (truncated) {
        jc_sb_append_fmt(&agg, "\n(note: only the first %d tasks were run)\n",
                         JC_PAR_MAX_TASKS);
    }
    jc_sb_free(&conflicts);
    jc_sb_free(&vfails);

    /* Free per-task parsed strings + pipe buffers. */
    for (i = 0; i < ntasks; i++) {
        if (tasks[i].launched) {
            jc_sb_free(&tasks[i].out);
        }
        if (tasks[i].answer != NULL) {
            free(tasks[i].answer);
        }
        if (tasks[i].error != NULL) {
            free(tasks[i].error);
        }
    }

    out->content = jc_sb_finish(&agg);
    out->is_error = (n_ok == 0) ? 1 : 0;
    jc_sb_free(&agg);
    return JC_OK;
}

static const struct jc_tool PARALLEL_TOOL = {
    "spawn_parallel",
    "Run several independent subtasks concurrently as sub-agents and return "
    "their aggregated answers. Trigger shape: three or more independent "
    "look-ups or reviews sharing no state -- one task per angle, each naming "
    "its files and its question. Read-only by default; write:true lets a "
    "task edit in an isolated git worktree (give write tasks disjoint "
    "files). Each child gets a slice of the remaining budget. Prefer this "
    "over sequential spawn_subagent for independent tasks, and plain tools "
    "for a single look-up.",
    parallel_schema,
    0, /* mutating */
    parallel_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_parallel(void)
{
    return &PARALLEL_TOOL;
}
