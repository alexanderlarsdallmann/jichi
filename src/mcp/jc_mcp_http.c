/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mcp_http.c - streamable-HTTP transport for the MCP client.
 *
 * Each JSON-RPC request is POSTed to the server's endpoint. The response is
 * either a single JSON object (Content-Type: application/json) or a
 * Server-Sent-Events stream (Content-Type: text/event-stream) whose data
 * events carry JSON-RPC messages; we pick the one whose id matches the
 * request. A session id handed back via the Mcp-Session-Id response header is
 * echoed on subsequent requests.
 *
 * Requires libcurl; when built without it, jc_mcp_http_open fails cleanly so
 * the rest of the agent still works (stdio servers are unaffected).
 */

#include "mcp_internal.h"
#include "jc_str.h"
#include "jc_log.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#ifdef JC_HAVE_CURL

#include "jc_http.h"
#include "jc_sse.h"

#define MCP_PROTOCOL_VERSION "2025-06-18"
#define MCP_HTTP_TIMEOUT_SECS 120

struct http_state {
    const struct jc_mcp_server_cfg *cfg; /* alive for the whole run */
    char *session_id;                    /* from Mcp-Session-Id, or NULL */
};

/* Case-insensitive byte compare up to n. */
static int ci_ncmp(const char *a, const char *b, jc_size n)
{
    jc_size i;
    for (i = 0; i < n; i++) {
        int ca = a[i];
        int cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) {
            return ca - cb;
        }
    }
    return 0;
}

/* Find the value of header `name` in a collected header set; the returned
 * pointer is into the header line (after ":" and surrounding spaces), valid
 * until the set is freed. NULL if absent. */
static const char *header_value(const struct jc_http_headers *h,
                                const char *name)
{
    jc_size i;
    jc_size nlen = strlen(name);
    for (i = 0; i < h->items.len; i++) {
        const char *line = *(char **)jc_vec_at((struct jc_vec *)&h->items, i);
        if (ci_ncmp(line, name, nlen) == 0 && line[nlen] == ':') {
            const char *v = line + nlen + 1;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            return v;
        }
    }
    return NULL;
}

/* Build the request headers for one call. */
static void build_headers(struct http_state *s, struct jc_http_headers *h)
{
    jc_size i;
    jc_http_headers_init(h);
    jc_http_headers_add(h, "Content-Type: application/json");
    jc_http_headers_add(h, "Accept: application/json, text/event-stream");
    jc_http_headers_add(h, "MCP-Protocol-Version: " MCP_PROTOCOL_VERSION);
    if (s->session_id != NULL) {
        char line[512];
        jc_snprintf(line, sizeof(line), "Mcp-Session-Id: %s", s->session_id);
        jc_http_headers_add(h, line);
    }
    for (i = 0; i < s->cfg->headers.len; i++) {
        const char *hl =
            *(char **)jc_vec_at((struct jc_vec *)&s->cfg->headers, i);
        jc_http_headers_add(h, hl);
    }
}

struct sse_pick {
    long  want_id;
    char *found; /* malloc'd matching JSON-RPC message, or NULL */
};

static void sse_cb(const struct jc_sse_event *ev, void *user)
{
    struct sse_pick *p = (struct sse_pick *)user;
    long got;
    if (p->found != NULL || ev->data == NULL || ev->data[0] == '\0') {
        return;
    }
    if (jc_mcp_message_id(ev->data, &got) && got == p->want_id) {
        p->found = jc_strdup(ev->data);
    }
}

/* Extract the JSON-RPC response with id == want_id from a response body, which
 * is either an SSE stream or a single JSON object. */
static char *extract_response(const char *body, const char *content_type,
                              long want_id)
{
    if (content_type != NULL && strstr(content_type, "text/event-stream")
            != NULL) {
        struct sse_pick pick;
        struct jc_sse_parser parser;
        pick.want_id = want_id;
        pick.found = NULL;
        jc_sse_init(&parser, sse_cb, &pick);
        jc_sse_feed(&parser, body, strlen(body));
        jc_sse_free(&parser);
        return pick.found;
    }
    /* Plain JSON: assume the body is the single response message. */
    if (body != NULL && body[0] != '\0') {
        return jc_strdup(body);
    }
    return NULL;
}

/* POST `line`; if want_response, return the matched message in *resp_out. */
static jc_status http_post(struct jc_mcp_conn *c, const char *line,
                           long id, int want_response, char **resp_out)
{
    struct http_state *s = (struct http_state *)c->t;
    struct jc_http_headers req_headers;
    struct jc_http_headers resp_headers;
    struct jc_http_request req;
    char *body = NULL;
    long status = 0;
    jc_status st;

    if (resp_out != NULL) {
        *resp_out = NULL;
    }
    build_headers(s, &req_headers);
    jc_http_headers_init(&resp_headers);

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = s->cfg->url;
    req.headers = &req_headers;
    req.body = line;
    req.body_len = strlen(line);
    req.timeout_secs = MCP_HTTP_TIMEOUT_SECS;
    req.abort_flag = c->abort;
    req.resp_headers = &resp_headers;

    st = jc_http_perform(&req, &status, &body, NULL);

    if (st == JC_OK) {
        /* Capture / refresh the session id (set on initialize). */
        const char *sid = header_value(&resp_headers, "Mcp-Session-Id");
        if (sid != NULL && sid[0] != '\0') {
            free(s->session_id);
            s->session_id = jc_strdup(sid);
        }
        if (status < 200 || status >= 300) {
            jc_logf(JC_LOG_ERROR, "mcp '%s': http status %ld", c->name, status);
            st = JC_ERR_HTTP;
        } else if (want_response) {
            const char *ct = header_value(&resp_headers, "Content-Type");
            char *msg = extract_response(body, ct, id);
            if (msg == NULL) {
                jc_logf(JC_LOG_ERROR,
                        "mcp '%s': no matching response in body", c->name);
                st = JC_ERR_PROVIDER;
            } else {
                *resp_out = msg;
            }
        }
    }

    free(body);
    jc_http_headers_free(&req_headers);
    jc_http_headers_free(&resp_headers);
    return st;
}

static jc_status http_request(struct jc_mcp_conn *c, const char *line,
                              long id, char **resp_out)
{
    return http_post(c, line, id, 1, resp_out);
}

static jc_status http_notify(struct jc_mcp_conn *c, const char *line)
{
    return http_post(c, line, 0, 0, NULL);
}

static void http_close(struct jc_mcp_conn *c)
{
    struct http_state *s = (struct http_state *)c->t;
    if (s == NULL) {
        return;
    }
    free(s->session_id);
    free(s);
    c->t = NULL;
}

static const struct jc_mcp_transport_vt HTTP_VT = {
    http_request,
    http_notify,
    http_close
};

jc_status jc_mcp_http_open(struct jc_mcp_conn **out,
                           const struct jc_mcp_server_cfg *cfg,
                           volatile int *abort)
{
    struct jc_mcp_conn *c;
    struct http_state *s;

    if (cfg->url == NULL || cfg->url[0] == '\0') {
        jc_logf(JC_LOG_ERROR, "mcp '%s': no url configured", cfg->name);
        return JC_ERR_INVALID;
    }
    s = (struct http_state *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return JC_ERR_OOM;
    }
    s->cfg = cfg;
    s->session_id = NULL;

    c = jc_mcp_conn_alloc(cfg->name, abort);
    if (c == NULL) {
        free(s);
        return JC_ERR_OOM;
    }
    c->vt = &HTTP_VT;
    c->t = s;
    *out = c;
    return JC_OK;
}

#else /* !JC_HAVE_CURL */

jc_status jc_mcp_http_open(struct jc_mcp_conn **out,
                           const struct jc_mcp_server_cfg *cfg,
                           volatile int *abort)
{
    (void)out;
    (void)abort;
    jc_logf(JC_LOG_ERROR,
            "mcp '%s': http transport unavailable (built without libcurl)",
            cfg != NULL ? cfg->name : "?");
    return JC_ERR_HTTP;
}

#endif /* JC_HAVE_CURL */
