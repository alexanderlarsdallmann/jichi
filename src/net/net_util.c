/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* net_util.c - shared helpers for the embeddings/rerank REST clients. */

#include "net_util.h"
#include "jc_net.h"
#include "jc_http.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

void jc_url_join(const char *base, const char *path, char *buf, jc_size cap)
{
    jc_size bl;
    int base_slash;
    int path_slash;

    if (base == NULL) {
        base = "";
    }
    if (path == NULL) {
        path = "";
    }
    bl = strlen(base);
    base_slash = (bl > 0 && base[bl - 1] == '/');
    path_slash = (path[0] == '/');

    if (base_slash && path_slash) {
        /* Drop one of the two slashes. */
        jc_snprintf(buf, cap, "%s%s", base, path + 1);
    } else if (!base_slash && !path_slash && bl > 0 && path[0] != '\0') {
        jc_snprintf(buf, cap, "%s/%s", base, path);
    } else {
        jc_snprintf(buf, cap, "%s%s", base, path);
    }
}

/* Join an OpenAI-style base + path, inserting "/v1" when the base lacks it (so
 * a base of either "https://host/v1" or "http://host:1234" works) -- matching
 * the chat endpoint's behaviour for embeddings/rerank/probes. */
static void url_join_v1(const char *base, const char *path, char *buf,
                        jc_size cap)
{
    if (base != NULL && strstr(base, "/v1") != NULL) {
        jc_url_join(base, path, buf, cap);
    } else {
        char tmp[900];
        jc_snprintf(tmp, sizeof(tmp), "%s/v1", base != NULL ? base : "");
        jc_url_join(tmp, path, buf, cap);
    }
}

void jc_net_url_v1(const struct jc_model_cfg *m, const char *path,
                   char *buf, jc_size cap)
{
    url_join_v1(m != NULL ? m->api_base : NULL, path, buf, cap);
}

jc_status jc_net_post_json(const struct jc_model_cfg *m, const char *path,
                           const char *body, long *http_status,
                           char **resp_out, volatile int *abort)
{
    char url[1024];
    char auth[1200];
    struct jc_http_headers headers;
    struct jc_http_request req;
    jc_status st;

    *resp_out = NULL;
    if (http_status != NULL) {
        *http_status = 0;
    }

    url_join_v1(m->api_base, path, url, sizeof(url));

    jc_http_headers_init(&headers);
    jc_http_headers_add(&headers, "Content-Type: application/json");
    if (m->api_key != NULL && m->api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", m->api_key);
        jc_http_headers_add(&headers, auth);
    }

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = url;
    req.headers = &headers;
    req.body = body;
    req.body_len = (body != NULL) ? strlen(body) : 0;
    req.timeout_secs = 60;
    req.abort_flag = abort;

    st = jc_http_perform(&req, http_status, resp_out, NULL);
    jc_http_headers_free(&headers);
    return st;
}

int jc_net_reachable(const char *api_base, const char *api_key,
                     long timeout_secs, volatile int *abort)
{
    char url[1024];
    char auth[1200];
    struct jc_http_headers headers;
    struct jc_http_request req;
    long http_status = 0;
    char *resp = NULL;
    jc_status st;

    if (api_base == NULL || api_base[0] == '\0') {
        return 0;
    }
    url_join_v1(api_base, "/models", url, sizeof(url));

    jc_http_headers_init(&headers);
    if (api_key != NULL && api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
        jc_http_headers_add(&headers, auth);
    }

    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.headers = &headers;
    req.timeout_secs = (timeout_secs > 0) ? timeout_secs : 2;
    req.abort_flag = abort;

    /* A connect failure here is the expected "unreachable" signal, not an
     * error worth printing -- mute logging just for the probe. */
    {
        int prev = jc_log_get_level();
        jc_log_set_level(JC_LOG_NONE);
        st = jc_http_perform(&req, &http_status, &resp, NULL);
        jc_log_set_level(prev);
    }
    free(resp);
    jc_http_headers_free(&headers);
    /* Any HTTP status (even 4xx) means the server answered => reachable. */
    return (st == JC_OK || http_status > 0) ? 1 : 0;
}

jc_status jc_net_parse_models(const char *json, struct jc_vec *out_ids,
                              struct jc_arena *a)
{
    cJSON *root;
    cJSON *data;
    cJSON *item;

    if (json == NULL) {
        return JC_ERR_PARSE;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    data = cJSON_IsArray(root) ? root : cJSON_GetObjectItem(root, "data");
    if (cJSON_IsArray(data)) {
        cJSON_ArrayForEach(item, data) {
            const char *id = jc_json_get_str(item, "id",
                                             jc_json_get_str(item, "name",
                                                             NULL));
            if (id != NULL && id[0] != '\0') {
                char *dup = jc_arena_strdup(a, id);
                jc_vec_push(out_ids, &dup);
            }
        }
    }
    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_net_list_models(const char *api_base, const char *api_key,
                             long timeout_secs, volatile int *abort,
                             struct jc_vec *out_ids, struct jc_arena *a)
{
    char url[1024];
    char auth[1200];
    struct jc_http_headers headers;
    struct jc_http_request req;
    long http_status = 0;
    char *resp = NULL;
    jc_status st;

    if (api_base == NULL || api_base[0] == '\0') {
        return JC_ERR_INVALID;
    }
    url_join_v1(api_base, "/models", url, sizeof(url));

    jc_http_headers_init(&headers);
    if (api_key != NULL && api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
        jc_http_headers_add(&headers, auth);
    }
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.headers = &headers;
    req.timeout_secs = (timeout_secs > 0) ? timeout_secs : 5;
    req.abort_flag = abort;

    st = jc_http_perform(&req, &http_status, &resp, NULL);
    jc_http_headers_free(&headers);
    if (st != JC_OK || http_status >= 400 || resp == NULL) {
        free(resp);
        return (st != JC_OK) ? st : JC_ERR_HTTP;
    }
    st = jc_net_parse_models(resp, out_ids, a);
    free(resp);
    return st;
}

/* The parse half of jc_net_model_limits, split out so it can be TESTED without
 * a server -- the same split jc_net_parse_models already has, and for the same
 * reason. The interesting inputs are not the happy path: they are the bodies
 * real servers send when they do not have this endpoint at all.
 *
 *   JC_OK           -- `model_id` is in the table with a positive input limit
 *   JC_ERR_NOTFOUND -- a well-formed model table that does not list it (or
 *                      lists it with a null limit, which this gateway does)
 *   JC_ERR_PARSE    -- not a model table: unparseable, or parseable but shaped
 *                      like something else. LM Studio answers this endpoint
 *                      with HTTP 200 and {"error":"Unexpected endpoint or
 *                      method."}, so "not the shape" is the ONLY way to
 *                      recognise a server that has no such endpoint.
 */
jc_status jc_net_parse_model_limits(const char *json, const char *model_id,
                                    long *out_max_input, long *out_max_output)
{
    cJSON *root;
    cJSON *data;
    cJSON *item;
    jc_status found = JC_ERR_NOTFOUND;

    if (out_max_input != NULL) {
        *out_max_input = 0;
    }
    if (out_max_output != NULL) {
        *out_max_output = 0;
    }
    if (json == NULL || model_id == NULL || model_id[0] == '\0') {
        return JC_ERR_INVALID;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    data = cJSON_IsArray(root) ? root : cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    cJSON_ArrayForEach(item, data) {
        const char *name = jc_json_get_str(item, "model_name", NULL);
        cJSON *info;
        long in;
        long outv;

        if (name == NULL || strcmp(name, model_id) != 0) {
            continue;
        }
        info = cJSON_GetObjectItem(item, "model_info");
        if (info == NULL) {
            continue;
        }
        /* Both fields are legitimately null on this gateway for models whose
         * limits nobody filled in, and null must read as "not published"
         * rather than as zero. */
        in = (long)jc_json_get_num(info, "max_input_tokens", 0.0);
        outv = (long)jc_json_get_num(info, "max_output_tokens", 0.0);
        if (in > 0) {
            if (out_max_input != NULL) {
                *out_max_input = in;
            }
            if (out_max_output != NULL && outv > 0) {
                *out_max_output = outv;
            }
            found = JC_OK;
        }
        break;
    }
    cJSON_Delete(root);
    return found;
}

/* The GATEWAY'S OWN account of a model's context window.
 *
 * WHY THIS EXISTS. Standard OpenAI `/v1/models` answers with id/object/
 * owned_by and says NOTHING about the context window, so jichi has always had
 * to be TOLD the window by hand -- and jc_agent.c's under-declared-window
 * warning exists precisely because nobody does: with no `contextLength` jichi
 * budgets against JC_COMPACT_DEFAULT_LIMIT (32000), and one measured workload
 * compacted seven times toward a target it never needed because 32000 stood in
 * for a real 256000. A LiteLLM proxy additionally serves `/v1/model/info`,
 * which carries `max_input_tokens` per model. Where a server publishes the
 * number, jichi should not have to guess it.
 *
 * `max_input_tokens` and not `max_output_tokens`: jichi's contextLength is the
 * budget it FILLS with prompt (jc_compact.c effective_limit, COMPACTION.md),
 * and compaction triggers at 0.8 of it, which is the headroom. Adding the
 * output allowance would over-declare the window and invite the HTTP 400 this
 * is meant to avoid.
 *
 * THIS ENDPOINT IS LITELLM-SPECIFIC, not part of the OpenAI surface. So the
 * three outcomes are kept apart, because a probe that cannot tell "absent"
 * from "I could not ask" is not a probe (M476):
 *
 *   JC_OK           -- a limit was published; *out_max_input > 0
 *   JC_ERR_NOTFOUND -- the endpoint answered, but this model has no limit in it
 *   JC_ERR_HTTP     -- the endpoint refused; *out_http_status says how (404 =
 *                      this server does not offer it at all, 401 = the key was
 *                      not accepted -- a caller must not report those alike)
 *   JC_ERR_PARSE    -- it answered with something that is not the shape
 *
 * Cost, stated rather than discovered later: the response is the WHOLE model
 * table (1.6 MB on the gateway this was measured against), parsed into cJSON. That
 * is acceptable for a one-shot diagnostic and is why no hot path calls this.
 */
jc_status jc_net_model_limits(const char *api_base, const char *api_key,
                              const char *model_id, long timeout_secs,
                              volatile int *abort, long *out_max_input,
                              long *out_max_output, long *out_http_status)
{
    char url[1024];
    char auth[1200];
    struct jc_http_headers headers;
    struct jc_http_request req;
    long http_status = 0;
    char *resp = NULL;
    jc_status st;

    if (out_max_input != NULL) {
        *out_max_input = 0;
    }
    if (out_max_output != NULL) {
        *out_max_output = 0;
    }
    if (out_http_status != NULL) {
        *out_http_status = 0;
    }
    if (api_base == NULL || api_base[0] == '\0' ||
        model_id == NULL || model_id[0] == '\0') {
        return JC_ERR_INVALID;
    }
    url_join_v1(api_base, "/model/info", url, sizeof(url));

    jc_http_headers_init(&headers);
    if (api_key != NULL && api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
        jc_http_headers_add(&headers, auth);
    }
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.headers = &headers;
    req.timeout_secs = (timeout_secs > 0) ? timeout_secs : 6;
    req.abort_flag = abort;

    /* A 404 here is the NORMAL answer from any non-LiteLLM server, so it must
     * not print as an error the way a real failure would. Muted for the same
     * reason jc_net_reachable mutes its probe. */
    {
        int prev = jc_log_get_level();
        jc_log_set_level(JC_LOG_NONE);
        st = jc_http_perform(&req, &http_status, &resp, NULL);
        jc_log_set_level(prev);
    }
    jc_http_headers_free(&headers);
    if (out_http_status != NULL) {
        *out_http_status = http_status;
    }
    if (st != JC_OK) {
        free(resp);
        return st;
    }
    if (http_status >= 400 || resp == NULL) {
        free(resp);
        return JC_ERR_HTTP;
    }

    st = jc_net_parse_model_limits(resp, model_id, out_max_input,
                                   out_max_output);
    free(resp);
    return st;
}
