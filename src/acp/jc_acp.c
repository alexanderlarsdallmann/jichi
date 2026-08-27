/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_acp.c - the Agent Client Protocol server loop (I/O).
 *
 * Reads newline-delimited JSON-RPC 2.0 requests from stdin, dispatches them, and
 * writes responses + `session/update` notifications to stdout. The framing is
 * the inverse of the MCP stdio client (jc_mcp_stdio.c): there we spawn a server
 * and read its replies; here we *are* the server, reading the editor's requests.
 *
 * The loop is fully synchronous and single-threaded. A `session/prompt` request
 * drives jc_agent_run_turn; the agent callbacks turn streamed text and tool
 * activity into `session/update` notifications, and the tool-permission gate
 * issues a `session/request_permission` request and blocks reading its response
 * (handling an interleaved `session/cancel` notification by aborting the turn).
 *
 * POSIX: read/write on fds. This translation unit relies on _POSIX_C_SOURCE
 * (set globally by the Makefile).
 */

#include "jc_acp.h"
#include "jc_mcp.h"
#include "jc_app.h"
#include "jc_agent.h"
#include "jc_message.h"
#include "jc_session.h"
#include "jc_mem.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "jc_cli.h"
#include "jc_uuid.h"
#include "jc_log.h"
#include "jc_snprintf.h"
#include "jc_transcribe.h"
#include "jc_base64.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>

#define ACP_RESULT_MAX 8192   /* cap on tool-result text echoed to the client */

struct acp_server {
    struct jc_app    *app;
    int               in_fd;
    int               out_fd;
    struct jc_sb      inbuf;        /* unconsumed input bytes (line framing)   */
    int               eof;
    long              next_id;      /* our outgoing request ids                */
    struct jc_session sess;         /* current session (id + history), persisted*/
    struct jc_arena  *sess_arena;   /* backs sess.id/title/workspace, or NULL  */
    int               have_session;
    struct jc_vec     always;       /* of char*: tools auto-allowed this run   */
    char              tool_id[40];  /* current tool call id                    */
    int               have_tool;
    int               cancelled;    /* a session/cancel arrived this turn      */
    struct jc_fs_delegate fs;       /* installed on app when the client has fs */
    struct jc_cmd_delegate cmd;     /* installed when the client has terminals */
};

/* The active session id (for notifications), or "" when none. */
static const char *acp_sid(struct acp_server *s)
{
    return (s->have_session && s->sess.id != NULL) ? s->sess.id : "";
}

/* --- raw I/O --- */

/* Write `s` then a newline to out_fd (full write). */
static void acp_write_line(struct acp_server *s, const char *json)
{
    jc_size len;
    jc_size off = 0;
    if (json == NULL) {
        return;
    }
    len = (jc_size)strlen(json);
    while (off < len) {
        ssize_t n = write(s->out_fd, json + off, len - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
        off += (jc_size)n;
    }
    for (;;) {
        ssize_t n = write(s->out_fd, "\n", 1);
        if (n == 1) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return;
    }
}

/* Read one newline-terminated line from in_fd into *out (malloc'd, no newline).
 * Returns 1 on a line, 0 on EOF (with any trailing partial line returned once). */
static int acp_read_line(struct acp_server *s, char **out)
{
    char buf[4096];

    *out = NULL;
    for (;;) {
        char *nl;
        ssize_t n;

        /* A complete line already buffered? */
        nl = (s->inbuf.data != NULL)
             ? (char *)memchr(s->inbuf.data, '\n', s->inbuf.len) : NULL;
        if (nl != NULL) {
            jc_size linelen = (jc_size)(nl - s->inbuf.data);
            jc_size rest;
            char *line = (char *)malloc(linelen + 1);
            if (line == NULL) {
                return 0;
            }
            memcpy(line, s->inbuf.data, linelen);
            line[linelen] = '\0';
            /* Shift the remainder (after the newline) to the front. */
            rest = s->inbuf.len - linelen - 1;
            memmove(s->inbuf.data, s->inbuf.data + linelen + 1, rest);
            s->inbuf.len = rest;
            *out = line;
            return 1;
        }
        if (s->eof) {
            /* Flush any final unterminated line. */
            if (s->inbuf.len > 0) {
                char *line = (char *)malloc(s->inbuf.len + 1);
                if (line == NULL) {
                    return 0;
                }
                memcpy(line, s->inbuf.data, s->inbuf.len);
                line[s->inbuf.len] = '\0';
                s->inbuf.len = 0;
                *out = line;
                return 1;
            }
            return 0;
        }
        n = read(s->in_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            s->eof = 1;
            continue;
        }
        if (n == 0) {
            s->eof = 1;
            continue;
        }
        jc_sb_append_n(&s->inbuf, buf, (jc_size)n);
    }
}

/* Non-blocking mid-turn cancel poll, wired as the agent's on_progress hook so
 * libcurl calls it periodically during a model stream (the main loop is blocked
 * in jc_agent_run_turn, so reading in_fd here is conflict-free). Without this a
 * session/cancel sits unread in stdin until the stream yields, so a stalled or
 * very long model call couldn't be cancelled. Consumes only a leading
 * session/cancel line; anything else is left for the main loop. A closed stdin
 * mid-turn (client went away) also aborts. */
static void acp_poll_cancel(void *user)
{
    struct acp_server *s = (struct acp_server *)user;
    fd_set rf;
    struct timeval tv;
    char buf[4096];
    char *nl;

    FD_ZERO(&rf);
    FD_SET(s->in_fd, &rf);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(s->in_fd + 1, &rf, NULL, NULL, &tv) > 0) {
        ssize_t n = read(s->in_fd, buf, sizeof(buf));
        if (n > 0) {
            jc_sb_append_n(&s->inbuf, buf, (jc_size)n);
        } else if (n == 0) {
            /* Client closed the pipe mid-turn: treat as a cancel. */
            s->eof = 1;
            s->cancelled = 1;
            s->app->abort_flag = 1;
            return;
        }
    }
    /* Only a *complete* leading line is safe to inspect. */
    nl = (s->inbuf.data != NULL)
         ? (char *)memchr(s->inbuf.data, '\n', s->inbuf.len) : NULL;
    if (nl != NULL) {
        jc_size linelen = (jc_size)(nl - s->inbuf.data);
        char save = s->inbuf.data[linelen];
        int is_cancel;
        s->inbuf.data[linelen] = '\0';
        is_cancel = strstr(s->inbuf.data, "session/cancel") != NULL;
        s->inbuf.data[linelen] = save;
        if (is_cancel) {
            jc_size rest = s->inbuf.len - linelen - 1;
            memmove(s->inbuf.data, s->inbuf.data + linelen + 1, rest);
            s->inbuf.len = rest;
            s->cancelled = 1;
            s->app->abort_flag = 1;
        }
    }
}

/* --- notification senders --- */

static void send_update(struct acp_server *s, cJSON *update)
{
    char *line = jc_acp_build_update(acp_sid(s), update);
    if (line != NULL) {
        acp_write_line(s, line);
        free(line);
    }
}

/* Send a JSON-RPC request (`params` is consumed) and block until its matching
 * response arrives, returning the parsed response root in *out (caller
 * cJSON_Delete's it). An interleaved session/cancel notification aborts the
 * turn and returns 0 (with *out = NULL); EOF returns 0 too. Other messages
 * received mid-wait are ignored. Used for both request_permission and the
 * client-side fs read/write round-trips. */
static int acp_request_await(struct acp_server *s, const char *method,
                             cJSON *params, cJSON **out)
{
    char *req;
    long req_id;

    *out = NULL;
    req_id = ++s->next_id;
    req = jc_mcp_build_request(req_id, method, params);
    if (req == NULL) {
        return 0;
    }
    acp_write_line(s, req);
    free(req);

    for (;;) {
        char *line = NULL;
        cJSON *root;
        cJSON *idobj;
        const char *m;
        long id = 0;
        int has_id;

        if (!acp_read_line(s, &line) || line == NULL) {
            return 0; /* client went away */
        }
        root = jc_json_parse(line);
        free(line);
        if (root == NULL) {
            continue;
        }
        m = jc_json_get_str(root, "method", NULL);
        idobj = cJSON_GetObjectItem(root, "id");
        has_id = cJSON_IsNumber(idobj);
        if (has_id) {
            id = (long)idobj->valuedouble;
        }
        if (m != NULL && strcmp(m, "session/cancel") == 0) {
            s->cancelled = 1;
            s->app->abort_flag = 1;
            cJSON_Delete(root);
            return 0;
        }
        if (m == NULL && has_id && id == req_id) {
            *out = root; /* our response */
            return 1;
        }
        cJSON_Delete(root); /* unrelated message mid-wait: ignore */
    }
}

/* Absolute path for the editor's fs methods (ACP requires absolute paths). */
static void acp_abspath(struct acp_server *s, const char *path,
                        char *buf, jc_size cap)
{
    if (path != NULL && path[0] == '/') {
        jc_snprintf(buf, cap, "%s", path);
    } else {
        jc_snprintf(buf, cap, "%s/%s", s->app->cwd, path != NULL ? path : "");
    }
}

/* fs delegate read: ask the editor for the file (its buffer, possibly unsaved)
 * via fs/read_text_file. Returns JC_OK with *out arena-owned, else an error so
 * the app helper falls back to disk. */
static jc_status acp_fs_read(void *ctx, const char *path, char **out,
                             jc_size *len, struct jc_arena *a)
{
    struct acp_server *s = (struct acp_server *)ctx;
    char abspath[1100];
    cJSON *resp = NULL;
    char *content;

    acp_abspath(s, path, abspath, sizeof(abspath));
    if (!acp_request_await(s, "fs/read_text_file",
                           jc_acp_fs_read_params(acp_sid(s), abspath), &resp)) {
        return JC_ERR_IO;
    }
    {
        char *rs = jc_json_print(resp);
        cJSON_Delete(resp);
        content = (rs != NULL) ? jc_acp_parse_fs_read_result(rs) : NULL;
        free(rs);
    }
    if (content == NULL) {
        return JC_ERR_NOTFOUND; /* error response / no content -> disk */
    }
    *out = jc_arena_strdup(a, content);
    if (len != NULL) {
        *len = (jc_size)strlen(content);
    }
    free(content);
    return (*out != NULL) ? JC_OK : JC_ERR_OOM;
}

/* fs delegate write: hand the new content to the editor via fs/write_text_file
 * (so it lands in the buffer). JC_OK on a non-error response, else fall back. */
static jc_status acp_fs_write(void *ctx, const char *path, const char *data,
                              jc_size len)
{
    struct acp_server *s = (struct acp_server *)ctx;
    char abspath[1100];
    cJSON *resp = NULL;
    cJSON *err;
    char *buf;
    int ok;
    (void)len; /* `data` is NUL-terminated by callers */

    acp_abspath(s, path, abspath, sizeof(abspath));
    /* Copy data NUL-terminated for the JSON string. */
    buf = (char *)malloc(len + 1);
    if (buf == NULL) {
        return JC_ERR_OOM;
    }
    memcpy(buf, data, len);
    buf[len] = '\0';
    ok = acp_request_await(s, "fs/write_text_file",
                           jc_acp_fs_write_params(acp_sid(s), abspath, buf),
                           &resp);
    free(buf);
    if (!ok) {
        return JC_ERR_IO;
    }
    err = jc_json_get_obj(resp, "error");
    cJSON_Delete(resp);
    return cJSON_IsObject(err) ? JC_ERR_IO : JC_OK;
}

/* Send a request without awaiting its response (fire-and-forget). Used for
 * terminal cleanup (kill/release) while unwinding -- any response the client
 * later sends is ignored by the dispatcher as a stray reply. */
static void acp_send_noreply(struct acp_server *s, const char *method,
                             cJSON *params)
{
    char *req = jc_mcp_build_request(++s->next_id, method, params);
    if (req != NULL) {
        acp_write_line(s, req);
        free(req);
    }
}

/* cmd delegate: run `command` in the editor's terminal. create -> advertise the
 * live terminal in the current tool card -> wait_for_exit -> output -> release.
 * Returns JC_OK with `out`/exit filled when it ran; JC_ERR_IO only when create
 * yielded no terminalId (so jc_app_run_command falls back to local exec). A
 * cancel/EOF mid-run returns JC_OK (output may be empty) so we never re-run a
 * possibly-mutating command locally. */
static jc_status acp_cmd_run(void *ctx, const char *command, jc_size byte_limit,
                             struct jc_sb *out, int *exit_code, int *truncated)
{
    struct acp_server *s = (struct acp_server *)ctx;
    cJSON *resp = NULL;
    char *rs;
    char *tid;
    int code = -1;

    if (!acp_request_await(s, "terminal/create",
            jc_acp_terminal_create_params(acp_sid(s), command, s->app->cwd,
                                          (long)byte_limit), &resp)) {
        return JC_OK; /* cancelled/EOF: do not double-run locally */
    }
    rs = jc_json_print(resp);
    cJSON_Delete(resp);
    tid = (rs != NULL) ? jc_acp_parse_terminal_id(rs) : NULL;
    free(rs);
    if (tid == NULL) {
        return JC_ERR_IO; /* client errored / no terminalId -> local fallback */
    }

    /* Show the live terminal in the active tool card, if there is one. */
    if (s->have_tool) {
        send_update(s, jc_acp_update_tool_call_terminal(s->tool_id, tid));
    }

    resp = NULL;
    if (!acp_request_await(s, "terminal/wait_for_exit",
            jc_acp_terminal_id_params(acp_sid(s), tid), &resp)) {
        /* cancelled/EOF mid-run: best-effort kill + release, keep nothing. */
        acp_send_noreply(s, "terminal/kill",
                         jc_acp_terminal_id_params(acp_sid(s), tid));
        acp_send_noreply(s, "terminal/release",
                         jc_acp_terminal_id_params(acp_sid(s), tid));
        free(tid);
        if (exit_code != NULL) {
            *exit_code = -1;
        }
        return JC_OK;
    }
    rs = jc_json_print(resp);
    cJSON_Delete(resp);
    if (rs != NULL) {
        jc_acp_parse_exit_status(rs, &code);
        free(rs);
    }

    /* Fetch the final captured output (and a possibly-more-precise exit code). */
    resp = NULL;
    if (acp_request_await(s, "terminal/output",
            jc_acp_terminal_id_params(acp_sid(s), tid), &resp)) {
        rs = jc_json_print(resp);
        cJSON_Delete(resp);
        if (rs != NULL) {
            char *text = NULL;
            int tr = 0;
            int oc = -1;
            int exited = 0;
            if (jc_acp_parse_terminal_output(rs, &text, &tr, &oc, &exited)) {
                if (text != NULL && text[0] != '\0') {
                    jc_size tl = (jc_size)strlen(text);
                    if (byte_limit > 0 && tl > byte_limit) {
                        jc_sb_append_n(out, text,
                                       jc_utf8_trunc_len(text, byte_limit));
                        tr = 1;
                    } else {
                        jc_sb_append_n(out, text, tl);
                    }
                }
                if (truncated != NULL) {
                    *truncated = tr;
                }
                if (exited) {
                    code = oc;
                }
            }
            free(text);
            free(rs);
        }
    }

    acp_send_noreply(s, "terminal/release",
                     jc_acp_terminal_id_params(acp_sid(s), tid));
    free(tid);
    if (exit_code != NULL) {
        *exit_code = code;
    }
    return JC_OK;
}

/* --- agent callbacks (mapped onto session/update + request_permission) --- */

static void cb_text(void *user, const char *delta, jc_size n)
{
    struct acp_server *s = (struct acp_server *)user;
    char *tmp;
    if (delta == NULL || n == 0) {
        return;
    }
    tmp = (char *)malloc(n + 1);
    if (tmp == NULL) {
        return;
    }
    memcpy(tmp, delta, n);
    tmp[n] = '\0';
    send_update(s, jc_acp_update_message_chunk(tmp));
    free(tmp);
}

/* M442's tool-call id is DELIBERATELY not adopted here. ACP mints its own
 * `toolCallId` per call (jc_uuid_v4 below) and the editor pairs updates by that
 * value, so swapping in the provider's id would change bytes on a wire an editor
 * is already speaking -- a behaviour change nobody asked for, in service of a
 * pairing that already works. Threaded and voided, so the choice is visible. */
static void cb_tool_start(void *user, const char *name, const char *args_json,
                          const char *id)
{
    struct acp_server *s = (struct acp_server *)user;
    char summary[256];
    char title[300];
    (void)id;

    jc_uuid_v4(s->tool_id);
    s->have_tool = 1;
    jc_tool_arg_summary(name, args_json, summary, sizeof(summary));
    if (summary[0] != '\0') {
        jc_snprintf(title, sizeof(title), "%s %s", name, summary);
    } else {
        jc_snprintf(title, sizeof(title), "%s", name);
    }
    send_update(s, jc_acp_update_tool_call(s->tool_id, title,
                                           jc_acp_tool_kind(name),
                                           "in_progress", args_json));
}

static void cb_tool_result(void *user, const char *name, const char *result,
                           int is_error, const char *id)
{
    struct acp_server *s = (struct acp_server *)user;
    char *bounded = NULL;
    const char *text = result;
    (void)name;
    (void)id;

    if (!s->have_tool) {
        return;
    }
    if (result != NULL && strlen(result) > ACP_RESULT_MAX) {
        /* M536: keep a WHOLE-CODEPOINT prefix. A raw byte cut leaves a stray
         * continuation byte inside a JSON-RPC string, and a strict client
         * (serde_json, encoding/json) rejects the whole notification -- so the
         * failure is not a mangled tail, it is a lost frame. This same file
         * already knew that: acp_cmd_run() above truncates with the helper. One
         * call site got it and its sibling did not, which is why the fix is the
         * helper and not a wider buffer. jc_utf8.h states the rule; ANECDOTES
         * #22 is the run it cost. */
        jc_size kept = jc_utf8_trunc_len(result, (jc_size)ACP_RESULT_MAX);
        bounded = (char *)malloc((size_t)kept + 32);
        if (bounded != NULL) {
            memcpy(bounded, result, (size_t)kept);
            jc_snprintf(bounded + kept, 32, "\n... [truncated]");
            text = bounded;
        }
    }
    send_update(s, jc_acp_update_tool_call_status(
        s->tool_id, is_error ? "failed" : "completed", text));
    free(bounded);
    s->have_tool = 0;
}

/* Is `name` already on the session's auto-allow list? */
static int always_has(struct acp_server *s, const char *name)
{
    jc_size i;
    for (i = 0; i < s->always.len; i++) {
        if (strcmp(*(char **)jc_vec_at(&s->always, i), name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cb_confirm(void *user, const char *name, const char *args_json,
                      char **edited)
{
    struct acp_server *s = (struct acp_server *)user;
    cJSON *params;
    cJSON *resp = NULL;
    char title[300];
    char summary[256];
    enum jc_acp_perm_outcome outcome = JC_ACP_PERM_REJECT;

    (void)edited;   /* ACP has no inline-edit affordance; allow/deny only */

    if (always_has(s, name)) {
        return 1;
    }

    jc_tool_arg_summary(name, args_json, summary, sizeof(summary));
    if (summary[0] != '\0') {
        jc_snprintf(title, sizeof(title), "%s %s", name, summary);
    } else {
        jc_snprintf(title, sizeof(title), "%s", name);
    }

    params = jc_acp_permission_params(acp_sid(s),
                                      s->have_tool ? s->tool_id : "",
                                      title, jc_acp_tool_kind(name));
    if (!acp_request_await(s, "session/request_permission", params, &resp)) {
        outcome = JC_ACP_PERM_CANCELLED; /* cancelled or client went away */
    } else {
        char *rs = jc_json_print(resp);
        if (rs != NULL) {
            outcome = jc_acp_parse_permission_outcome(rs);
            free(rs);
        }
        cJSON_Delete(resp);
    }

    if (outcome == JC_ACP_PERM_ALLOW_ALWAYS) {
        char *copy = jc_strdup(name);
        if (copy != NULL) {
            jc_vec_push(&s->always, &copy);
        }
        return 1;
    }
    if (outcome == JC_ACP_PERM_ALLOW_ONCE) {
        return 1;
    }
    if (outcome == JC_ACP_PERM_CANCELLED) {
        s->cancelled = 1;
        s->app->abort_flag = 1;
    }
    return 0;
}

/* --- request handlers --- */

/* Tear down the current session (history + its backing arena), if any. */
static void clear_session(struct acp_server *s)
{
    if (s->have_session) {
        jc_session_free(&s->sess);
        s->have_session = 0;
        s->app->todos = NULL; /* M606: that list is gone */
    }
    if (s->sess_arena != NULL) {
        jc_arena_free(s->sess_arena);
        s->sess_arena = NULL;
    }
}

/* Start a fresh session (its id becomes the ACP sessionId). 0 on failure. */
static int new_session(struct acp_server *s)
{
    clear_session(s);
    s->sess_arena = jc_arena_new(0);
    if (s->sess_arena == NULL) {
        return 0;
    }
    if (jc_session_new(&s->sess, s->app->cwd, s->sess_arena) != JC_OK) {
        jc_arena_free(s->sess_arena);
        s->sess_arena = NULL;
        return 0;
    }
    s->sess.mode = s->app->mode;
    s->have_session = 1;
    s->app->todos = &s->sess.todos; /* M606 */
    return 1;
}

/* Persist the current session (history + autotitle), unless --no-session. */
static void save_session(struct acp_server *s)
{
    if (!s->have_session || s->app->no_session) {
        return;
    }
    s->sess.mode = s->app->mode;
    jc_session_autotitle(&s->sess);
    jc_session_save(&s->sess);
}

/* Replay a loaded session's history to the client as session/update
 * notifications, so the editor can rebuild its transcript view. User turns
 * become user_message_chunk, assistant text agent_message_chunk, and each tool
 * call/result a tool_call + tool_call_update pair (status from the result). */
static void replay_history(struct acp_server *s)
{
    jc_size i, n;

    n = jc_history_len(&s->sess.history);
    for (i = 0; i < n; i++) {
        struct jc_message *m = jc_history_get(&s->sess.history, i);
        if (m == NULL) {
            continue;
        }
        if (m->role == JC_ROLE_USER) {
            if (m->content != NULL && m->content[0] != '\0') {
                send_update(s, jc_acp_update_user_message_chunk(m->content));
            }
        } else if (m->role == JC_ROLE_ASSISTANT) {
            jc_size j, tc;
            if (m->content != NULL && m->content[0] != '\0') {
                send_update(s, jc_acp_update_message_chunk(m->content));
            }
            tc = jc_msg_tool_call_count(m);
            for (j = 0; j < tc; j++) {
                struct jc_tool_call *t = jc_msg_tool_call_at(m, j);
                char summary[256];
                char title[300];
                if (t == NULL) {
                    continue;
                }
                jc_tool_arg_summary(t->name, t->arguments_json,
                                    summary, sizeof(summary));
                if (summary[0] != '\0') {
                    jc_snprintf(title, sizeof(title), "%s %s",
                                t->name, summary);
                } else {
                    jc_snprintf(title, sizeof(title), "%s", t->name);
                }
                send_update(s, jc_acp_update_tool_call(
                    t->id, title, jc_acp_tool_kind(t->name),
                    "completed", t->arguments_json));
            }
        } else if (m->role == JC_ROLE_TOOL) {
            send_update(s, jc_acp_update_tool_call_status(
                m->tool_call_id != NULL ? m->tool_call_id : "",
                m->is_error ? "failed" : "completed", m->content));
        }
    }
}

/* Map an ACP audio block's mimeType to a filename the transcription endpoint
 * recognizes (Whisper keys off the extension). */
static const char *acp_audio_filename(const char *mime)
{
    if (mime == NULL) {
        return "audio";
    }
    if (strcmp(mime, "audio/wav") == 0 || strcmp(mime, "audio/x-wav") == 0) {
        return "audio.wav";
    }
    if (strcmp(mime, "audio/mpeg") == 0) {
        return "audio.mp3";
    }
    if (strcmp(mime, "audio/mp4") == 0) {
        return "audio.m4a";
    }
    if (strcmp(mime, "audio/flac") == 0) {
        return "audio.flac";
    }
    if (strcmp(mime, "audio/ogg") == 0) {
        return "audio.ogg";
    }
    if (strcmp(mime, "audio/webm") == 0) {
        return "audio.webm";
    }
    return "audio";
}

/* Transcribe ACP audio prompt blocks (M33) and append each transcript to the
 * user message's text. The data is already base64 from the client; decode it,
 * send it to the transcribe-role model, and fold the text in. Gated by the
 * caller on a transcribe-role model existing; a per-block decode/size/transcribe
 * failure is skipped. */
static void acp_attach_audio(struct acp_server *s, const cJSON *params,
                             struct jc_message *um)
{
    struct jc_model_cfg *m;
    cJSON *prompt;
    cJSON *block;
    struct jc_sb sb;
    jc_size cap_bytes;
    int any = 0;

    if (um == NULL) {
        return;
    }
    m = jc_app_model_for_role(s->app, JC_ROLE_TRANSCRIBE);
    if (m == NULL) {
        return;
    }
    prompt = jc_json_get_obj((cJSON *)params, "prompt");
    if (!cJSON_IsArray(prompt)) {
        return;
    }
    cap_bytes = jc_config_cap(s->app->config.transcribe_max_bytes,
                              JC_TRANSCRIBE_MAX_BYTES);

    jc_sb_init(&sb);
    if (um->content != NULL) {
        jc_sb_append(&sb, um->content);
    }
    cJSON_ArrayForEach(block, prompt) {
        const char *type = jc_json_get_str(block, "type", "");
        const char *data;
        const char *mime;
        unsigned char *bytes;
        jc_size cap;
        jc_size len = 0;
        char *text = NULL;
        if (strcmp(type, "audio") != 0) {
            continue;
        }
        data = jc_json_get_str(block, "data", NULL);
        if (data == NULL || data[0] == '\0') {
            continue;
        }
        mime = jc_json_get_str(block, "mimeType", NULL);
        cap = jc_base64_decoded_len((jc_size)strlen(data));
        bytes = (unsigned char *)malloc(cap > 0 ? cap : 1);
        if (bytes == NULL) {
            continue;
        }
        if (jc_base64_decode(data, bytes, cap, &len) != JC_OK ||
            len > cap_bytes) {
            free(bytes);
            continue;
        }
        if (jc_transcribe_run(m, bytes, len, acp_audio_filename(mime),
                              mime != NULL ? mime : "audio/wav", NULL, &text,
                              &s->app->abort_flag, NULL) == JC_OK && text != NULL) {
            jc_sb_append(&sb, "\n\n--- transcribed audio ---\n");
            jc_sb_append(&sb, text);
            any = 1;
        }
        free(text);
        free(bytes);
    }
    if (any) {
        char *combined = jc_sb_finish(&sb);
        jc_msg_set_content(um, combined != NULL ? combined : "");
        free(combined);
    }
    jc_sb_free(&sb);
}

static void handle_prompt(struct acp_server *s, long id, cJSON *params)
{
    struct jc_agent_callbacks cb;
    char *text;
    char *resp;
    jc_status st;

    if (!s->have_session) {
        resp = jc_acp_build_error(id, -32602, "no active session");
        acp_write_line(s, resp);
        free(resp);
        return;
    }

    text = jc_acp_prompt_text(params);
    {
        struct jc_message *um = jc_history_add(&s->sess.history, JC_ROLE_USER,
                                               text != NULL ? text : "");
        /* Image prompt blocks (M29d), gated on the active model's vision
         * capability. The data is already base64 from the client. */
        if (um != NULL && s->app->config.model.vision) {
            jc_acp_prompt_images(params, um);
        }
        /* Audio prompt blocks (M33): transcribe to text and fold into the
         * message, when a transcribe-role model is configured. */
        acp_attach_audio(s, params, um);
    }
    free(text);

    memset(&cb, 0, sizeof(cb));
    cb.on_assistant_text = cb_text;
    cb.on_tool_start = cb_tool_start;
    cb.on_tool_result = cb_tool_result;
    cb.confirm_tool = cb_confirm;
    cb.on_progress = acp_poll_cancel; /* honor session/cancel mid model stream */
    cb.user = s;

    s->cancelled = 0;
    s->app->abort_flag = 0;
    st = jc_agent_run_turn(s->app, &s->sess.history, &cb);

    /* Persist after the turn, so the session can be reloaded later. */
    save_session(s);

    {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "stopReason",
            jc_acp_stop_reason(s->cancelled || st == JC_ERR_ABORTED));
        resp = jc_acp_build_response(id, result);
        acp_write_line(s, resp);
        free(resp);
    }
}

/* session/load: reload a persisted session by id and replay its history. */
static void handle_load(struct acp_server *s, long id, cJSON *params)
{
    const char *sid;
    char *resp;
    struct jc_arena *arena;
    struct jc_session loaded;

    sid = jc_json_get_str(params, "sessionId", NULL);
    if (sid == NULL || sid[0] == '\0') {
        resp = jc_acp_build_error(id, -32602, "missing sessionId");
        acp_write_line(s, resp);
        free(resp);
        return;
    }

    arena = jc_arena_new(0);
    if (arena == NULL ||
        jc_session_load_by_id(sid, &loaded, arena) != JC_OK) {
        if (arena != NULL) {
            jc_arena_free(arena);
        }
        resp = jc_acp_build_error(id, -32602, "unknown sessionId");
        acp_write_line(s, resp);
        free(resp);
        return;
    }

    /* Swap the loaded session in as the active one. */
    clear_session(s);
    s->sess = loaded;
    s->sess_arena = arena;
    s->have_session = 1;
    s->app->todos = &s->sess.todos; /* M606: the loaded session's list */
    jc_app_set_mode(s->app, s->sess.mode);

    /* Stream the transcript as session/update notifications, then respond:
     * per ACP, the response to session/load means "replay complete". */
    replay_history(s);
    resp = jc_acp_build_response(id, NULL);
    acp_write_line(s, resp);
    free(resp);
}

/* Dispatch one parsed message. Returns 0 to continue, 1 to stop the loop. */
static int dispatch(struct acp_server *s, const char *line)
{
    cJSON *root;
    const char *method;
    long id = 0;
    int has_id;
    cJSON *params;
    char *resp;

    root = jc_json_parse(line);
    if (root == NULL) {
        return 0; /* ignore malformed input */
    }
    method = jc_json_get_str(root, "method", NULL);
    has_id = jc_mcp_message_id(line, &id);
    params = jc_json_get_obj(root, "params");

    if (method == NULL) {
        cJSON_Delete(root);
        return 0; /* a stray response; nothing to do */
    }

    if (strcmp(method, "initialize") == 0) {
        int can_read = 0;
        int can_write = 0;
        /* If the editor offers a filesystem, route file tools through it so we
         * honor its unsaved buffers; otherwise tools hit disk (fs stays NULL). */
        jc_acp_client_fs_caps(params, &can_read, &can_write);
        s->fs.ctx = s;
        s->fs.read = can_read ? acp_fs_read : NULL;
        s->fs.write = can_write ? acp_fs_write : NULL;
        s->app->fs = (can_read || can_write) ? &s->fs : NULL;
        if (s->app->fs != NULL) {
            jc_logf(JC_LOG_INFO, "[acp] client fs delegation: read=%d write=%d",
                    can_read, can_write);
        }
        /* If the editor offers terminals, route run_terminal_command/run_tests
         * through them so commands run in (and are visible to) the editor. */
        if (jc_acp_client_terminal_cap(params)) {
            s->cmd.ctx = s;
            s->cmd.run = acp_cmd_run;
            s->app->cmd = &s->cmd;
            jc_logf(JC_LOG_INFO, "[acp] client terminal delegation enabled");
        } else {
            s->app->cmd = NULL;
        }
        resp = jc_acp_build_response(id,
            jc_acp_build_init_result(JC_ACP_PROTOCOL_VERSION,
                s->app->config.model.vision,
                jc_config_find_by_role(&s->app->config, JC_ROLE_TRANSCRIBE) >= 0));
        acp_write_line(s, resp);
        free(resp);
    } else if (strcmp(method, "authenticate") == 0) {
        resp = jc_acp_build_response(id, NULL);
        acp_write_line(s, resp);
        free(resp);
    } else if (strcmp(method, "session/new") == 0) {
        if (!new_session(s)) {
            resp = jc_acp_build_error(id, -32603, "could not create session");
            acp_write_line(s, resp);
            free(resp);
        } else {
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "sessionId", s->sess.id);
            resp = jc_acp_build_response(id, result);
            acp_write_line(s, resp);
            free(resp);
        }
    } else if (strcmp(method, "session/load") == 0) {
        handle_load(s, id, params);
    } else if (strcmp(method, "session/prompt") == 0) {
        handle_prompt(s, id, params);
    } else if (strcmp(method, "session/cancel") == 0) {
        /* Notification: abort an in-flight turn. (Outside a turn it is a
         * no-op; the flag is cleared at the next prompt.) */
        s->cancelled = 1;
        s->app->abort_flag = 1;
    } else if (has_id) {
        resp = jc_acp_build_error(id, -32601, "method not found");
        acp_write_line(s, resp);
        free(resp);
    }
    cJSON_Delete(root);
    return 0;
}

int jc_acp_serve(struct jc_app *app)
{
    struct acp_server s;

    memset(&s, 0, sizeof(s));
    s.app = app;
    s.in_fd = STDIN_FILENO;
    s.out_fd = STDOUT_FILENO;
    jc_sb_init(&s.inbuf);
    jc_vec_init(&s.always, sizeof(char *));

    jc_logf(JC_LOG_INFO, "[acp] server ready (protocol v%d)",
            JC_ACP_PROTOCOL_VERSION);

    for (;;) {
        char *line = NULL;
        if (!acp_read_line(&s, &line) || line == NULL) {
            break; /* EOF: the client closed the connection */
        }
        if (line[0] != '\0') {
            dispatch(&s, line);
        }
        free(line);
    }

    /* Teardown. */
    {
        jc_size i;
        for (i = 0; i < s.always.len; i++) {
            free(*(char **)jc_vec_at(&s.always, i));
        }
    }
    app->fs = NULL;  /* s.fs lived on this stack frame  */
    app->cmd = NULL; /* s.cmd lived on this stack frame */
    jc_vec_free(&s.always);
    clear_session(&s);
    jc_sb_free(&s.inbuf);
    return 0;
}
