/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_oneshot.c - one non-streaming prompt->text model call (see jc_oneshot.h). */

#include "jc_oneshot.h"
#include "jc_provider.h"
#include "jc_message.h"
#include "jc_http.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

char *jc_oneshot(struct jc_provider *prov, const char *system_msg,
                 const char *user_msg, long timeout_secs,
                 volatile int *abort_flag)
{
    return jc_oneshot_ex(prov, system_msg, user_msg, timeout_secs, abort_flag,
                         NULL);
}

void jc_oneshot_result_free(struct jc_oneshot_result *r)
{
    if (r == NULL) {
        return;
    }
    free(r->text);
    free(r->call_name);
    r->text = NULL;
    r->call_name = NULL;
}

jc_status jc_oneshot_probe(struct jc_provider *prov, const char *system_msg,
                           const char *user_msg, const void *tools,
                           long timeout_secs, volatile int *abort_flag,
                           struct jc_oneshot_result *out)
{
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    char *resp = NULL;
    long http_status = 0;
    jc_status st;

    if (out == NULL) {
        return JC_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    if (prov == NULL || user_msg == NULL) {
        return JC_ERR_INVALID;
    }

    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, user_msg);
    /* Deliberately mirror run_agent_loop's history shape, including the empty
     * assistant message it appends to stream into (jc_agent.c). Without this the
     * probe would build a request no real turn ever builds, and would therefore
     * not test the thing it exists to test: M166 was exactly a placeholder that
     * reached the wire, and a probe that omits it reports a clean bill of health
     * on a broken build. Verified: with the placeholder serialised, a 7.5B local
     * model answers this very request with one end-of-turn token; with it
     * skipped, it calls the tool 4/4. */
    jc_history_add(&mini, JC_ROLE_ASSISTANT, NULL);
    st = prov->vt->build_request(prov, &mini, system_msg, (const cJSON *)tools,
                                 0, &body);
    if (st != JC_OK) {
        jc_history_free(&mini);
        return st;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = (timeout_secs > 0) ? timeout_secs : 60;
    req.abort_flag = abort_flag;

    st = jc_http_perform(&req, &http_status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);
    out->http_status = http_status;

    if (st == JC_OK && http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                 NULL);
        st = prov->vt->parse_full(prov, resp, reply);
        if (st == JC_OK) {
            if (reply->content != NULL && reply->content[0] != '\0') {
                out->text = jc_strdup(reply->content);
            }
            out->ncalls = (int)jc_msg_tool_call_count(reply);
            if (out->ncalls > 0) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(reply, 0);
                if (tc != NULL && tc->name != NULL) {
                    out->call_name = jc_strdup(tc->name);
                }
            }
            if (prov->vt->get_usage != NULL) {
                double in_tok = 0.0, out_tok = 0.0;
                prov->vt->get_usage(prov, &in_tok, &out_tok);
                out->in_tokens = in_tok;
            }
        }
    } else if (st == JC_OK) {
        st = JC_ERR_HTTP; /* completed, but a 4xx/5xx or an empty body */
    }
    free(resp);
    jc_history_free(&mini);
    return st;
}

char *jc_oneshot_ex(struct jc_provider *prov, const char *system_msg,
                    const char *user_msg, long timeout_secs,
                    volatile int *abort_flag, int *timed_out)
{
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *body = NULL;
    char *resp = NULL;
    char *result = NULL;
    long http_status = 0;
    jc_status st;

    if (timed_out != NULL) {
        *timed_out = 0;
    }
    if (prov == NULL || user_msg == NULL) {
        return NULL;
    }
    jc_history_init(&mini);
    jc_history_add(&mini, JC_ROLE_USER, user_msg);

    st = prov->vt->build_request(prov, &mini, system_msg, NULL, 0, &body);
    if (st != JC_OK) {
        jc_history_free(&mini);
        return NULL;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = (timeout_secs > 0) ? timeout_secs : 60;
    req.abort_flag = abort_flag;

    st = jc_http_perform(&req, &http_status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);

    if (st == JC_ERR_TIMEOUT && timed_out != NULL) {
        *timed_out = 1;
    }
    if (st == JC_OK && http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                  NULL);
        if (prov->vt->parse_full(prov, resp, reply) == JC_OK &&
            reply->content != NULL && reply->content[0] != '\0') {
            result = jc_strdup(reply->content);
        }
    }
    free(resp);
    jc_history_free(&mini);
    return result;
}
