/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lsp.c - LSP client manager, stdio transport, and the diagnostics flow.
 *
 * Lazily spawns a configured language server, runs the initialize handshake,
 * opens a file, and collects textDocument/publishDiagnostics. Content-Length
 * framed JSON-RPC over the server's stdin/stdout (POSIX fork/exec/pipe/select;
 * relies on the global -D_POSIX_C_SOURCE). Server-to-client requests are
 * answered with a null result so the server never stalls.
 */

#include "jc_lsp.h"
#include "jc_proc.h"
#include "jc_app.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_platform.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>

#define LSP_INIT_TIMEOUT  30.0
#define LSP_DIAG_TIMEOUT  15.0
#define LSP_DIAG_GRACE    0.4   /* keep reading briefly after the first batch */
#define LSP_NAV_TIMEOUT   10.0  /* definition/references/symbols round-trip */
#define LSP_CA_DIAG_TIMEOUT 3.0 /* bounded diagnostics wait for code actions (M57) */

struct jc_lsp_conn {
    const struct jc_lsp_server_cfg *cfg;
    pid_t                 pid;
    int                   in_fd;
    int                   out_fd;
    struct jc_lsp_framer  framer;
    long                  next_id;
    int                   initialized;
    int                   dead;
    struct jc_vec         opened; /* of char*: uris opened (heap) */
    volatile int         *abort;
};

void jc_lsp_manager_init(struct jc_lsp_manager *m, struct jc_app *app)
{
    m->app = app;
    jc_vec_init(&m->conns, sizeof(struct jc_lsp_conn *));
}

/* ----- transport ------------------------------------------------------- */

static char **build_argv(const struct jc_lsp_server_cfg *cfg)
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

static jc_status spawn_server(const struct jc_lsp_server_cfg *cfg, pid_t *pid,
                              int *in_fd, int *out_fd)
{
    int inp[2];
    int outp[2];
    char **argv;
    pid_t child;
    int devnull;

    if (jc_pipe_cloexec(inp) != 0) {
        return JC_ERR_IO;
    }
    if (jc_pipe_cloexec(outp) != 0) {
        close(inp[0]); close(inp[1]);
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
        dup2(inp[0], STDIN_FILENO);
        dup2(outp[1], STDOUT_FILENO);
        /* Silence the server's stderr (LSP servers are very chatty). */
        devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execvp(argv[0], argv);
        _exit(127);
    }
    free(argv);
    close(inp[0]);
    close(outp[1]);
    *pid = child;
    *in_fd = inp[1];
    *out_fd = outp[0];
    return JC_OK;
}

static jc_status write_all(int fd, const char *data, jc_size len)
{
    jc_size off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return JC_ERR_IO;
        }
        off += (jc_size)w;
    }
    return JC_OK;
}

/* Serialise and frame `msg` (consumed) and write it to the server. */
static jc_status send_msg(struct jc_lsp_conn *c, cJSON *msg)
{
    char *s;
    struct jc_sb sb;
    jc_status st;
    s = jc_json_print(msg);
    cJSON_Delete(msg);
    if (s == NULL) {
        return JC_ERR_OOM;
    }
    jc_sb_init(&sb);
    jc_lsp_frame_encode(s, &sb);
    free(s);
    st = write_all(c->in_fd, sb.data, sb.len);
    jc_sb_free(&sb);
    if (st != JC_OK) {
        c->dead = 1;
    }
    return st;
}

static jc_status send_request(struct jc_lsp_conn *c, long id,
                              const char *method, cJSON *params)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(m, "id", (double)id);
    cJSON_AddStringToObject(m, "method", method);
    cJSON_AddItemToObject(m, "params", params != NULL ? params
                                                      : cJSON_CreateObject());
    return send_msg(c, m);
}

static jc_status send_notify(struct jc_lsp_conn *c, const char *method,
                             cJSON *params)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddStringToObject(m, "method", method);
    cJSON_AddItemToObject(m, "params", params != NULL ? params
                                                      : cJSON_CreateObject());
    return send_msg(c, m);
}

/* Read one complete message body (malloc'd) before `deadline`, or NULL on
 * timeout/abort/EOF (EOF sets c->dead). */
static char *lsp_recv(struct jc_lsp_conn *c, double deadline)
{
    char *body = NULL;
    if (jc_lsp_framer_pop(&c->framer, &body)) {
        return body;
    }
    for (;;) {
        fd_set rfds;
        struct timeval tv;
        int rc;
        if (c->abort != NULL && *c->abort) {
            return NULL;
        }
        if (jc_now_seconds() > deadline) {
            return NULL;
        }
        FD_ZERO(&rfds);
        FD_SET(c->out_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        rc = select(c->out_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            c->dead = 1;
            return NULL;
        }
        if (rc == 0) {
            continue;
        }
        {
            char chunk[4096];
            ssize_t n = read(c->out_fd, chunk, sizeof(chunk));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                c->dead = 1;
                return NULL;
            }
            if (n == 0) {
                c->dead = 1;
                return NULL;
            }
            jc_lsp_framer_push(&c->framer, chunk, (jc_size)n);
            if (jc_lsp_framer_pop(&c->framer, &body)) {
                return body;
            }
        }
    }
}

/* If `body` is a server->client request, answer it with a null result. */
static int reply_if_request(struct jc_lsp_conn *c, const char *body)
{
    cJSON *root = jc_json_parse(body);
    cJSON *id;
    const char *method;
    int is_req;
    if (root == NULL) {
        return 0;
    }
    id = cJSON_GetObjectItem(root, "id");
    method = jc_json_get_str(root, "method", NULL);
    is_req = (method != NULL && id != NULL);
    if (is_req) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
        if (cJSON_IsNumber(id)) {
            cJSON_AddNumberToObject(resp, "id", id->valuedouble);
        } else if (cJSON_IsString(id)) {
            cJSON_AddStringToObject(resp, "id", id->valuestring);
        } else {
            cJSON_AddNullToObject(resp, "id");
        }
        cJSON_AddNullToObject(resp, "result");
        send_msg(c, resp);
    }
    cJSON_Delete(root);
    return is_req;
}

/* Send a request and block until its id-matched response arrives (replying to
 * any interleaved server->client requests). On success returns JC_OK and, when
 * `result_out` is non-NULL, the response's `result` serialised to a malloc'd
 * string (NULL if absent). `params` is consumed. Bounded by `deadline`. */
static jc_status lsp_request(struct jc_lsp_conn *c, const char *method,
                             cJSON *params, double deadline, char **result_out)
{
    long id;
    int got = 0;

    if (result_out != NULL) {
        *result_out = NULL;
    }
    id = c->next_id++;
    if (send_request(c, id, method, params) != JC_OK) {
        return JC_ERR_IO;
    }
    while (!got) {
        char *body = lsp_recv(c, deadline);
        cJSON *root;
        if (body == NULL) {
            return JC_ERR_HTTP; /* timeout / abort / dead */
        }
        root = jc_json_parse(body);
        if (root != NULL) {
            cJSON *rid = cJSON_GetObjectItem(root, "id");
            const char *m2 = jc_json_get_str(root, "method", NULL);
            if (m2 == NULL && cJSON_IsNumber(rid) &&
                (long)rid->valuedouble == id) {
                got = 1;
                if (result_out != NULL) {
                    cJSON *res = cJSON_GetObjectItem(root, "result");
                    if (res != NULL) {
                        *result_out = jc_json_print(res);
                    }
                }
            }
            cJSON_Delete(root);
        }
        if (!got) {
            reply_if_request(c, body);
        }
        free(body);
    }
    return JC_OK;
}

/* ----- lifecycle ------------------------------------------------------- */

static jc_status do_initialize(struct jc_lsp_conn *c, const char *cwd)
{
    cJSON *params;
    cJSON *caps;
    cJSON *td;
    cJSON *info;
    char rooturi[1100];
    char *result = NULL;
    jc_status st;

    params = cJSON_CreateObject();
    cJSON_AddNullToObject(params, "processId");
    jc_lsp_path_to_uri(".", cwd, rooturi, sizeof(rooturi));
    cJSON_AddStringToObject(params, "rootUri", rooturi);
    caps = cJSON_CreateObject();
    td = cJSON_CreateObject();
    cJSON_AddItemToObject(td, "publishDiagnostics", cJSON_CreateObject());
    /* Code actions (M44): advertise CodeAction-literal + edit-resolve support so
     * servers return CodeAction objects (not just Commands) and can lazily fill
     * the `edit` on resolve (rust-analyzer); an empty valueSet means all kinds. */
    {
        cJSON *ca = cJSON_CreateObject();
        cJSON *lit = cJSON_CreateObject();
        cJSON *kinds = cJSON_CreateObject();
        cJSON *res = cJSON_CreateObject();
        cJSON *props = cJSON_CreateArray();
        cJSON_AddItemToObject(kinds, "valueSet", cJSON_CreateArray());
        cJSON_AddItemToObject(lit, "codeActionKind", kinds);
        cJSON_AddItemToObject(ca, "codeActionLiteralSupport", lit);
        cJSON_AddItemToArray(props, cJSON_CreateString("edit"));
        cJSON_AddItemToObject(res, "properties", props);
        cJSON_AddItemToObject(ca, "resolveSupport", res);
        cJSON_AddItemToObject(ca, "dataSupport", cJSON_CreateBool(1));
        cJSON_AddItemToObject(td, "codeAction", ca);
    }
    cJSON_AddItemToObject(caps, "textDocument", td);
    cJSON_AddItemToObject(params, "capabilities", caps);
    info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", "jichi");
    cJSON_AddItemToObject(params, "clientInfo", info);

    st = lsp_request(c, "initialize", params,
                     jc_now_seconds() + LSP_INIT_TIMEOUT, &result);
    free(result);
    if (st != JC_OK) {
        return st;
    }
    send_notify(c, "initialized", NULL);
    c->initialized = 1;
    return JC_OK;
}

/* ----- manager / matching --------------------------------------------- */

static const char *path_ext(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = (slash != NULL) ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    return (dot != NULL && dot[1] != '\0') ? dot + 1 : "";
}

static const struct jc_lsp_server_cfg *find_cfg(struct jc_lsp_manager *m,
                                                const char *path)
{
    const char *ext = path_ext(path);
    jc_size i;
    jc_size j;
    if (m->app == NULL || ext[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < m->app->config.lsp_servers.len; i++) {
        struct jc_lsp_server_cfg *s =
            (struct jc_lsp_server_cfg *)jc_vec_at(&m->app->config.lsp_servers,
                                                  i);
        for (j = 0; j < s->extensions.len; j++) {
            const char *e = *(char **)jc_vec_at(&s->extensions, j);
            if (strcmp(e, ext) == 0) {
                return s;
            }
        }
    }
    return NULL;
}

int jc_lsp_handles(struct jc_lsp_manager *m, const char *path)
{
    return find_cfg(m, path) != NULL;
}

/* Find an existing connection for `cfg`, or spawn + initialize a new one. */
static struct jc_lsp_conn *get_conn(struct jc_lsp_manager *m,
                                    const struct jc_lsp_server_cfg *cfg)
{
    struct jc_lsp_conn *c;
    jc_size i;
    pid_t pid;
    int in_fd;
    int out_fd;

    for (i = 0; i < m->conns.len; i++) {
        c = *(struct jc_lsp_conn **)jc_vec_at(&m->conns, i);
        if (c->cfg == cfg) {
            return c->dead ? NULL : c;
        }
    }
    if (spawn_server(cfg, &pid, &in_fd, &out_fd) != JC_OK) {
        jc_logf(JC_LOG_WARN, "lsp '%s': could not start", cfg->name);
        return NULL;
    }
    c = (struct jc_lsp_conn *)calloc(1, sizeof(*c));
    if (c == NULL) {
        close(in_fd); close(out_fd);
        kill(pid, SIGTERM); waitpid(pid, NULL, 0);
        return NULL;
    }
    c->cfg = cfg;
    c->pid = pid;
    c->in_fd = in_fd;
    c->out_fd = out_fd;
    c->next_id = 1;
    jc_lsp_framer_init(&c->framer);
    jc_vec_init(&c->opened, sizeof(char *));
    c->abort = (m->app != NULL) ? &m->app->abort_flag : NULL;
    jc_vec_push(&m->conns, &c);

    if (do_initialize(c, m->app != NULL ? m->app->cwd : ".") != JC_OK) {
        jc_logf(JC_LOG_WARN, "lsp '%s': initialize failed", cfg->name);
        c->dead = 1;
        return NULL;
    }
    return c;
}

static int uri_is_open(struct jc_lsp_conn *c, const char *uri)
{
    jc_size i;
    for (i = 0; i < c->opened.len; i++) {
        if (strcmp(*(char **)jc_vec_at(&c->opened, i), uri) == 0) {
            return 1;
        }
    }
    return 0;
}

/* didClose (if previously open) then didOpen with the current text. */
static void open_document(struct jc_lsp_conn *c, const char *uri,
                          const char *lang, const char *text)
{
    cJSON *td;
    cJSON *params;

    if (uri_is_open(c, uri)) {
        td = cJSON_CreateObject();
        cJSON_AddStringToObject(td, "uri", uri);
        params = cJSON_CreateObject();
        cJSON_AddItemToObject(params, "textDocument", td);
        send_notify(c, "textDocument/didClose", params);
    } else {
        char *copy = jc_strdup(uri);
        jc_vec_push(&c->opened, &copy);
    }
    td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddStringToObject(td, "languageId", lang);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text", text != NULL ? text : "");
    params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    send_notify(c, "textDocument/didOpen", params);
}

/* Wait (bounded by `timeout` seconds, with the usual grace shortcut after the
 * first batch) for the latest `textDocument/publishDiagnostics` matching `uri`,
 * and return its `params` object serialized (malloc'd), or NULL if none arrived.
 * The document must already be opened on `c`. Shared by jc_lsp_diagnostics (text
 * rendering) and jc_lsp_code_actions (line-filtered request context, M57). */
static char *collect_diag_params(struct jc_lsp_conn *c, const char *uri,
                                 double timeout)
{
    char *latest = NULL;
    double deadline = jc_now_seconds() + timeout;
    double grace = deadline;

    for (;;) {
        char *body;
        double now = jc_now_seconds();
        const char *method;
        cJSON *root;
        if (now > deadline || now > grace) {
            break;
        }
        body = lsp_recv(c, deadline < grace ? deadline : grace);
        if (body == NULL) {
            if (c->dead) {
                break;
            }
            continue; /* slice elapsed; re-check deadlines */
        }
        root = jc_json_parse(body);
        method = (root != NULL) ? jc_json_get_str(root, "method", NULL) : NULL;
        if (method != NULL &&
            strcmp(method, "textDocument/publishDiagnostics") == 0) {
            cJSON *params = jc_json_get_obj(root, "params");
            const char *puri = (params != NULL)
                ? jc_json_get_str(params, "uri", NULL) : NULL;
            if (puri != NULL && strcmp(puri, uri) == 0) {
                char *ps = jc_json_print(params);
                if (ps != NULL) {
                    free(latest);
                    latest = ps;
                    grace = jc_now_seconds() + LSP_DIAG_GRACE;
                }
            }
        } else if (method != NULL) {
            reply_if_request(c, body);
        }
        if (root != NULL) {
            cJSON_Delete(root);
        }
        free(body);
    }
    return latest;
}

char *jc_lsp_diagnostics(struct jc_lsp_manager *m, const char *path,
                         int *count_out)
{
    const struct jc_lsp_server_cfg *cfg;
    struct jc_lsp_conn *c;
    char *content;
    char uri[1100];
    const char *lang;
    char *params;
    char *latest = NULL;
    int latest_count = 0;
    struct jc_sb out;

    if (count_out != NULL) {
        *count_out = 0;
    }
    cfg = find_cfg(m, path);
    if (cfg == NULL) {
        return NULL; /* no server for this file type */
    }
    c = get_conn(m, cfg);
    if (c == NULL) {
        struct jc_sb e;
        jc_sb_init(&e);
        jc_sb_append_fmt(&e, "[lsp %s: unavailable]", cfg->name);
        return jc_sb_finish(&e);
    }
    /* M197: scratch -- didOpen consumes the text; it is not session state. */
    if (jc_read_file(path, &content, NULL,
                     jc_app_tool_scratch(m->app)) != JC_OK) {
        struct jc_sb e;
        jc_sb_init(&e);
        jc_sb_append_fmt(&e, "[lsp: could not read %s]", path);
        return jc_sb_finish(&e);
    }

    jc_lsp_path_to_uri(path, m->app->cwd, uri, sizeof(uri));
    lang = jc_lsp_language_id(path_ext(path));
    open_document(c, uri, lang, content);

    params = collect_diag_params(c, uri, LSP_DIAG_TIMEOUT);
    if (params != NULL) {
        struct jc_sb tmp;
        jc_sb_init(&tmp);
        if (jc_lsp_format_diagnostics(params, uri, path, &tmp, &latest_count)) {
            latest = jc_sb_finish(&tmp);
        } else {
            jc_sb_free(&tmp);
        }
        free(params);
    }

    if (count_out != NULL) {
        *count_out = latest_count;
    }
    jc_sb_init(&out);
    if (latest == NULL) {
        jc_sb_append_fmt(&out, "[lsp %s: no diagnostics received]", cfg->name);
    } else if (latest_count == 0) {
        jc_sb_append(&out, "No diagnostics.");
    } else {
        jc_sb_append_fmt(&out, "Diagnostics (%d):\n", latest_count);
        jc_sb_append(&out, latest);
    }
    free(latest);
    return jc_sb_finish(&out);
}

/* ----- navigation ------------------------------------------------------ */

static cJSON *position_params(const char *uri, long line, long col,
                              int include_decl)
{
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddNumberToObject(pos, "line", (double)line);
    cJSON_AddNumberToObject(pos, "character", (double)col);
    cJSON_AddItemToObject(params, "position", pos);
    if (include_decl) {
        cJSON *ctx = cJSON_CreateObject();
        cJSON_AddBoolToObject(ctx, "includeDeclaration", 1);
        cJSON_AddItemToObject(params, "context", ctx);
    }
    return params;
}

/* Ensure path's server is up and the file is open. Returns conn or NULL; on
 * success sets *content_out (arena-owned) and fills `uri`. */
static struct jc_lsp_conn *open_for(struct jc_lsp_manager *m, const char *path,
                                    char *uri, jc_size ucap, char **content_out)
{
    const struct jc_lsp_server_cfg *cfg = find_cfg(m, path);
    struct jc_lsp_conn *c;
    char *content = NULL;

    if (content_out != NULL) {
        *content_out = NULL;
    }
    if (cfg == NULL) {
        return NULL;
    }
    c = get_conn(m, cfg);
    if (c == NULL) {
        return NULL;
    }
    /* M197: scratch -- didOpen consumes the text; it is not session state. */
    if (jc_read_file(path, &content, NULL,
                     jc_app_tool_scratch(m->app)) != JC_OK) {
        return NULL;
    }
    jc_lsp_path_to_uri(path, m->app->cwd, uri, ucap);
    open_document(c, uri, jc_lsp_language_id(path_ext(path)), content);
    if (content_out != NULL) {
        *content_out = content;
    }
    return c;
}

/* Send a location-returning request (definition/references) and format it. */
static char *loc_query(struct jc_lsp_conn *c, const char *method,
                       cJSON *params, int *count, const char *empty_msg)
{
    char *result = NULL;
    struct jc_sb out;
    int n = 0;

    if (lsp_request(c, method, params, jc_now_seconds() + LSP_NAV_TIMEOUT,
                    &result) != JC_OK) {
        return jc_strdup("[lsp: no response from server]");
    }
    jc_sb_init(&out);
    jc_lsp_format_locations(result, &out, &n);
    free(result);
    if (count != NULL) {
        *count = n;
    }
    if (n == 0) {
        jc_sb_free(&out);
        return jc_strdup(empty_msg);
    }
    return jc_sb_finish(&out);
}

/* First configured server's connection (workspace/symbol isn't file-specific). */
static struct jc_lsp_conn *any_conn(struct jc_lsp_manager *m)
{
    const struct jc_lsp_server_cfg *cfg;
    if (m->app == NULL || m->app->config.lsp_servers.len == 0) {
        return NULL;
    }
    cfg = (struct jc_lsp_server_cfg *)
          jc_vec_at(&m->app->config.lsp_servers, 0);
    return get_conn(m, cfg);
}

/* workspace/symbol query, formatted as symbols. */
static char *workspace_symbols(struct jc_lsp_manager *m, const char *symbol,
                               int *count)
{
    struct jc_lsp_conn *c = any_conn(m);
    cJSON *params;
    char *result = NULL;
    struct jc_sb out;
    int n = 0;

    if (c == NULL) {
        return NULL;
    }
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "query", symbol != NULL ? symbol : "");
    if (lsp_request(c, "workspace/symbol", params,
                    jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
        return jc_strdup("[lsp: no response from server]");
    }
    jc_sb_init(&out);
    jc_lsp_format_symbols(result, &out, &n);
    free(result);
    if (count != NULL) {
        *count = n;
    }
    if (n == 0) {
        jc_sb_free(&out);
        return jc_strdup("No matching symbol.");
    }
    return jc_sb_finish(&out);
}

char *jc_lsp_symbols(struct jc_lsp_manager *m, const char *path, int *count)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    cJSON *params;
    cJSON *td;
    char *result = NULL;
    struct jc_sb out;
    int n = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (find_cfg(m, path) == NULL) {
        return NULL;
    }
    c = open_for(m, path, uri, sizeof(uri), NULL);
    if (c == NULL) {
        return jc_strdup("[lsp: server unavailable]");
    }
    td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    if (lsp_request(c, "textDocument/documentSymbol", params,
                    jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
        return jc_strdup("[lsp: no response from server]");
    }
    jc_sb_init(&out);
    jc_lsp_format_symbols(result, &out, &n);
    free(result);
    if (count != NULL) {
        *count = n;
    }
    if (n == 0) {
        jc_sb_free(&out);
        return jc_strdup("No symbols found.");
    }
    return jc_sb_finish(&out);
}

char *jc_lsp_definition(struct jc_lsp_manager *m, const char *path,
                        const char *symbol, long line_hint, int *count)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    char *content = NULL;
    long line = 0;
    long col = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (path == NULL) {
        return workspace_symbols(m, symbol, count); /* by name, project-wide */
    }
    if (find_cfg(m, path) == NULL) {
        return NULL;
    }
    c = open_for(m, path, uri, sizeof(uri), &content);
    if (c == NULL) {
        return jc_strdup("[lsp: server unavailable]");
    }
    if (line_hint > 0) {
        line = line_hint - 1;
    } else if (!jc_lsp_locate_symbol(content, symbol, &line, &col)) {
        return jc_strdup("symbol not found in file (pass a line, or omit the "
                         "path to search the workspace)");
    }
    return loc_query(c, "textDocument/definition",
                     position_params(uri, line, col, 0), count,
                     "No definition found.");
}

char *jc_lsp_references(struct jc_lsp_manager *m, const char *path,
                        const char *symbol, long line_hint, int *count)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    char *content = NULL;
    long line = 0;
    long col = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (path != NULL) {
        if (find_cfg(m, path) == NULL) {
            return NULL;
        }
        c = open_for(m, path, uri, sizeof(uri), &content);
        if (c == NULL) {
            return jc_strdup("[lsp: server unavailable]");
        }
        if (line_hint > 0) {
            line = line_hint - 1;
        } else if (!jc_lsp_locate_symbol(content, symbol, &line, &col)) {
            return jc_strdup("symbol not found in file (pass a line)");
        }
        return loc_query(c, "textDocument/references",
                         position_params(uri, line, col, 1), count,
                         "No references found.");
    }
    /* No path: resolve the symbol via workspace/symbol, then references at it. */
    {
        struct jc_lsp_conn *wc = any_conn(m);
        cJSON *params;
        char *result = NULL;
        char turi[1100];
        const char *tp;
        const struct jc_lsp_server_cfg *cfg;
        struct jc_lsp_conn *tc;
        int ok;

        if (wc == NULL) {
            return NULL;
        }
        params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "query", symbol != NULL ? symbol : "");
        if (lsp_request(wc, "workspace/symbol", params,
                        jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
            return jc_strdup("[lsp: no response from server]");
        }
        ok = (result != NULL) &&
             jc_lsp_first_symbol_location(result, symbol, turi, sizeof(turi),
                                          &line, &col);
        free(result);
        if (!ok) {
            return jc_strdup("No matching symbol to find references for.");
        }
        tp = turi;
        if (strncmp(tp, "file://", 7) == 0) {
            tp += 7;
        }
        cfg = find_cfg(m, tp);
        if (cfg == NULL) {
            return jc_strdup("[lsp: no server for the symbol's file]");
        }
        tc = get_conn(m, cfg);
        if (tc != NULL &&
            jc_read_file(tp, &content, NULL, /* M197: scratch */
                         jc_app_tool_scratch(m->app)) == JC_OK) {
            open_document(tc, turi, jc_lsp_language_id(path_ext(tp)), content);
            return loc_query(tc, "textDocument/references",
                             position_params(turi, line, col, 1), count,
                             "No references found.");
        }
        return jc_strdup("[lsp: could not open the symbol's file]");
    }
}

/* ----- refactors (rename / format) — M40 ------------------------------- */

char *jc_lsp_rename(struct jc_lsp_manager *m, const char *path, long line_hint,
                    const char *symbol, const char *new_name)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    char *content = NULL;
    char *result = NULL;
    cJSON *params;
    long line = 0, col = 0;

    if (path == NULL || new_name == NULL || new_name[0] == '\0') {
        return NULL;
    }
    if (find_cfg(m, path) == NULL) {
        return NULL;
    }
    c = open_for(m, path, uri, sizeof(uri), &content);
    if (c == NULL) {
        return NULL;
    }
    if (line_hint > 0) {
        line = line_hint - 1;
    } else if (!jc_lsp_locate_symbol(content, symbol, &line, &col)) {
        return NULL; /* symbol not found in file */
    }
    params = position_params(uri, line, col, 0);
    cJSON_AddStringToObject(params, "newName", new_name);
    if (lsp_request(c, "textDocument/rename", params,
                    jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
        return NULL;
    }
    return result; /* WorkspaceEdit JSON, or NULL when the server declined */
}

char *jc_lsp_format(struct jc_lsp_manager *m, const char *path)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    char *result = NULL;
    cJSON *params;
    cJSON *td;
    cJSON *opts;

    if (path == NULL) {
        return NULL;
    }
    if (find_cfg(m, path) == NULL) {
        return NULL;
    }
    c = open_for(m, path, uri, sizeof(uri), NULL);
    if (c == NULL) {
        return NULL;
    }
    params = cJSON_CreateObject();
    td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    opts = cJSON_CreateObject();
    cJSON_AddNumberToObject(opts, "tabSize", 4.0);
    cJSON_AddBoolToObject(opts, "insertSpaces", 1);
    cJSON_AddItemToObject(params, "options", opts);
    if (lsp_request(c, "textDocument/formatting", params,
                    jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
        return NULL;
    }
    return result; /* TextEdit[] JSON, or NULL */
}

char *jc_lsp_code_actions(struct jc_lsp_manager *m, const char *path, long line,
                          const char *only)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    char *result = NULL;
    cJSON *params;
    cJSON *td;
    cJSON *range;
    cJSON *start;
    cJSON *end;
    cJSON *ctx;
    long ln = (line > 0) ? line - 1 : 0; /* 1-based -> 0-based */

    if (path == NULL || find_cfg(m, path) == NULL) {
        return NULL;
    }
    c = open_for(m, path, uri, sizeof(uri), NULL);
    if (c == NULL) {
        return NULL;
    }
    params = cJSON_CreateObject();
    td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    /* A whole-line range: actions offered anywhere on the line. */
    range = cJSON_CreateObject();
    start = cJSON_CreateObject();
    end = cJSON_CreateObject();
    cJSON_AddNumberToObject(start, "line", (double)ln);
    cJSON_AddNumberToObject(start, "character", 0.0);
    cJSON_AddNumberToObject(end, "line", (double)ln);
    cJSON_AddNumberToObject(end, "character", 100000.0);
    cJSON_AddItemToObject(range, "start", start);
    cJSON_AddItemToObject(range, "end", end);
    cJSON_AddItemToObject(params, "range", range);
    /* Thread the structured diagnostics on this line into the request context so
     * the server offers diagnostic-tied quick-fixes (add import, fix error), not
     * just refactor/source actions (M57). open_for did didOpen, so a short wait
     * catches the server's publishDiagnostics; an empty/absent set is fine. */
    ctx = cJSON_CreateObject();
    {
        char *dp = collect_diag_params(c, uri, LSP_CA_DIAG_TIMEOUT);
        cJSON *darr = NULL;
        if (dp != NULL) {
            char *filtered = jc_lsp_diagnostics_for_line(dp, uri, ln);
            if (filtered != NULL) {
                darr = jc_json_parse(filtered);
                free(filtered);
            }
            free(dp);
        }
        if (!cJSON_IsArray(darr)) {
            if (darr != NULL) {
                cJSON_Delete(darr);
            }
            darr = cJSON_CreateArray();
        }
        cJSON_AddItemToObject(ctx, "diagnostics", darr);
    }
    /* Optional `context.only`: restrict the kinds the server returns (M58). */
    if (only != NULL && only[0] != '\0') {
        char *oa = jc_lsp_only_array(only);
        if (oa != NULL) {
            cJSON *o = jc_json_parse(oa);
            free(oa);
            if (cJSON_IsArray(o)) {
                cJSON_AddItemToObject(ctx, "only", o);
            } else if (o != NULL) {
                cJSON_Delete(o);
            }
        }
    }
    cJSON_AddItemToObject(params, "context", ctx);

    if (lsp_request(c, "textDocument/codeAction", params,
                    jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
        return NULL;
    }
    return result; /* (CodeAction|Command)[] JSON, or NULL */
}

char *jc_lsp_code_action_resolve(struct jc_lsp_manager *m, const char *path,
                                 const char *action_json)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    char *result = NULL;
    cJSON *params;

    if (path == NULL || action_json == NULL || find_cfg(m, path) == NULL) {
        return NULL;
    }
    c = open_for(m, path, uri, sizeof(uri), NULL);
    if (c == NULL) {
        return NULL;
    }
    params = jc_json_parse(action_json); /* send the action object verbatim */
    if (params == NULL) {
        return NULL;
    }
    if (lsp_request(c, "codeAction/resolve", params,
                    jc_now_seconds() + LSP_NAV_TIMEOUT, &result) != JC_OK) {
        return NULL;
    }
    return result; /* the resolved CodeAction (with `edit`), or NULL */
}

/* Ack a workspace/applyEdit request with {applied:true}. */
static void ack_apply_edit(struct jc_lsp_conn *c, cJSON *rid)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    if (cJSON_IsNumber(rid)) {
        cJSON_AddNumberToObject(resp, "id", rid->valuedouble);
    } else if (cJSON_IsString(rid)) {
        cJSON_AddStringToObject(resp, "id", rid->valuestring);
    } else {
        cJSON_AddNullToObject(resp, "id");
    }
    cJSON_AddBoolToObject(r, "applied", 1);
    cJSON_AddItemToObject(resp, "result", r);
    send_msg(c, resp);
}

jc_status jc_lsp_execute_command(struct jc_lsp_manager *m, const char *path,
                                 const char *command, const char *args_json,
                                 char **edits_out)
{
    char uri[1100];
    struct jc_lsp_conn *c;
    cJSON *params;
    cJSON *args;
    long id;
    int got = 0;
    int nedits = 0;
    struct jc_sb edits;
    double deadline = jc_now_seconds() + LSP_NAV_TIMEOUT;

    if (edits_out != NULL) {
        *edits_out = NULL;
    }
    if (m == NULL || path == NULL || command == NULL ||
        find_cfg(m, path) == NULL) {
        return JC_ERR_INVALID;
    }
    c = open_for(m, path, uri, sizeof(uri), NULL);
    if (c == NULL) {
        return JC_ERR_HTTP;
    }
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "command", command);
    args = (args_json != NULL) ? jc_json_parse(args_json) : NULL;
    if (!cJSON_IsArray(args)) {
        if (args != NULL) {
            cJSON_Delete(args);
        }
        args = cJSON_CreateArray();
    }
    cJSON_AddItemToObject(params, "arguments", args);

    id = c->next_id++;
    if (send_request(c, id, "workspace/executeCommand", params) != JC_OK) {
        return JC_ERR_IO;
    }
    jc_sb_init(&edits);
    jc_sb_append_char(&edits, '[');
    while (!got) {
        char *body = lsp_recv(c, deadline);
        cJSON *root;
        int handled = 0;
        if (body == NULL) {
            jc_sb_free(&edits);
            return JC_ERR_HTTP;
        }
        root = jc_json_parse(body);
        if (root != NULL) {
            cJSON *rid = cJSON_GetObjectItem(root, "id");
            const char *m2 = jc_json_get_str(root, "method", NULL);
            if (m2 == NULL && cJSON_IsNumber(rid) &&
                (long)rid->valuedouble == id) {
                got = 1;
                handled = 1;
            } else if (m2 != NULL &&
                       strcmp(m2, "workspace/applyEdit") == 0 && rid != NULL) {
                cJSON *prm = cJSON_GetObjectItem(root, "params");
                cJSON *edit = (prm != NULL)
                    ? cJSON_GetObjectItem(prm, "edit") : NULL;
                if (edit != NULL) {
                    char *ej = jc_json_print(edit);
                    if (ej != NULL) {
                        if (nedits > 0) {
                            jc_sb_append_char(&edits, ',');
                        }
                        jc_sb_append(&edits, ej);
                        free(ej);
                        nedits++;
                    }
                }
                ack_apply_edit(c, rid);
                handled = 1;
            }
            cJSON_Delete(root);
        }
        if (!handled) {
            reply_if_request(c, body); /* other server requests -> null */
        }
        free(body);
    }
    jc_sb_append_char(&edits, ']');
    if (nedits > 0 && edits_out != NULL) {
        *edits_out = jc_sb_finish(&edits);
    } else {
        jc_sb_free(&edits);
    }
    return JC_OK;
}

/* ----- shutdown -------------------------------------------------------- */

static void close_conn(struct jc_lsp_conn *c)
{
    jc_size i;
    int status;
    if (!c->dead && c->initialized) {
        long id = c->next_id++;
        send_request(c, id, "shutdown", NULL);
        send_notify(c, "exit", NULL);
    }
    if (c->in_fd >= 0) {
        close(c->in_fd);
    }
    if (c->out_fd >= 0) {
        close(c->out_fd);
    }
    if (c->pid > 0) {
        kill(c->pid, SIGTERM);
        waitpid(c->pid, &status, 0);
    }
    for (i = 0; i < c->opened.len; i++) {
        free(*(char **)jc_vec_at(&c->opened, i));
    }
    jc_vec_free(&c->opened);
    jc_lsp_framer_free(&c->framer);
    free(c);
}

void jc_lsp_manager_shutdown(struct jc_lsp_manager *m)
{
    jc_size i;
    for (i = 0; i < m->conns.len; i++) {
        close_conn(*(struct jc_lsp_conn **)jc_vec_at(&m->conns, i));
    }
    jc_vec_free(&m->conns);
}
