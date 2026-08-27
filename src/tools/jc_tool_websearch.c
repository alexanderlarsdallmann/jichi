/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_websearch.c - the web_search tool (M27).
 *
 * POSTs {"query","max_results"[,"api_key"]} to the configured search endpoint
 * (config "search".url) with a Bearer header and renders the returned results.
 * The result parser jc_websearch_format is pure and unit-tested (it accepts the
 * common Tavily/SerpAPI-style shapes). Registered only when search.url is set.
 */

#include "tool_util.h"
#include "jc_untrusted.h"
#include "jc_app.h"
#include "jc_http.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#define WEB_SEARCH_MAX_BYTES (16 * 1024)
#define WEB_SEARCH_DEF_RESULTS 5
#define WEB_SNIPPET_MAX 320

/* Append `s` to `out`, collapsing runs of whitespace/newlines into single
 * spaces and stopping at `max` source bytes (keeps a snippet compact). */
static void append_snippet(struct jc_sb *out, const char *s, jc_size max)
{
    jc_size i;
    int prev_space = 0;
    if (s == NULL) {
        return;
    }
    for (i = 0; s[i] != '\0' && i < max; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (!prev_space) {
                jc_sb_append_char(out, ' ');
                prev_space = 1;
            }
        } else {
            jc_sb_append_char(out, c);
            prev_space = 0;
        }
    }
    if (s[i] != '\0') {
        jc_sb_append(out, "...");
    }
}

/* Pure: render up to `max` results from a search response `json` into `out`.
 * Accepts the result array under "results" (Tavily), "data", or "web_results";
 * each item's title is title|name, url is url|link, snippet is
 * content|snippet|description|text. Returns the number rendered, or -1 if the
 * JSON did not parse / had no results array. */
int jc_websearch_format(const char *json, int max, struct jc_sb *out)
{
    cJSON *root;
    cJSON *arr;
    cJSON *item;
    int count = 0;

    if (json == NULL) {
        return -1;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return -1;
    }
    arr = jc_json_get_obj(root, "results");
    if (!cJSON_IsArray(arr)) {
        arr = jc_json_get_obj(root, "data");
    }
    if (!cJSON_IsArray(arr)) {
        arr = jc_json_get_obj(root, "web_results");
    }
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return -1;
    }
    if (max <= 0) {
        max = WEB_SEARCH_DEF_RESULTS;
    }
    for (item = arr->child; item != NULL && count < max; item = item->next) {
        const char *title = jc_json_get_str(item, "title", NULL);
        const char *url;
        const char *snip;
        if (title == NULL) {
            title = jc_json_get_str(item, "name", NULL);
        }
        url = jc_json_get_str(item, "url", NULL);
        if (url == NULL) {
            url = jc_json_get_str(item, "link", NULL);
        }
        snip = jc_json_get_str(item, "content", NULL);
        if (snip == NULL) snip = jc_json_get_str(item, "snippet", NULL);
        if (snip == NULL) snip = jc_json_get_str(item, "description", NULL);
        if (snip == NULL) snip = jc_json_get_str(item, "text", NULL);

        count++;
        jc_sb_append_fmt(out, "%d. %s\n", count,
                         title != NULL ? title : "(untitled)");
        if (url != NULL) {
            jc_sb_append_fmt(out, "   %s\n", url);
        }
        if (snip != NULL && snip[0] != '\0') {
            jc_sb_append(out, "   ");
            append_snippet(out, snip, WEB_SNIPPET_MAX);
            jc_sb_append_char(out, '\n');
        }
        jc_sb_append_char(out, '\n');
    }
    cJSON_Delete(root);
    if (count == 0) {
        jc_sb_append(out, "(no results)");
    }
    return count;
}

static cJSON *web_search_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "query", "The search query", 1);
    tu_schema_int(s, "max_results", "Maximum results to return (optional)", 0);
    return s;
}

static jc_status web_search_run(const cJSON *args, struct jc_tool_result *out,
                                struct jc_app *app)
{
    const char *query = tu_arg_str(args, "query");
    const struct jc_search_cfg *sc = &app->config.search;
    int want;
    char *body;
    cJSON *reqj;
    struct jc_http_request req;
    struct jc_http_headers headers;
    char auth[1200];
    long status = 0;
    char *resp = NULL;
    jc_status st;
    struct jc_sb sb;

    if (query == NULL || query[0] == '\0') {
        tu_err(out, "error: 'query' argument is required");
        return JC_OK;
    }
    if (sc->url == NULL || sc->url[0] == '\0') {
        tu_err(out, "error: web search is not configured (set search.url)");
        return JC_OK;
    }

    want = tu_arg_int(args, "max_results", 0);
    if (want <= 0) {
        want = (sc->max_results > 0) ? (int)sc->max_results
                                     : WEB_SEARCH_DEF_RESULTS;
    }

    /* Body: {"query","max_results"[,"api_key"]} -- the api_key field covers
     * backends (e.g. Tavily) that take the key in the body as well as / instead
     * of the Bearer header. */
    reqj = cJSON_CreateObject();
    if (reqj == NULL) {
        tu_err(out, "error: out of memory");
        return JC_OK;
    }
    cJSON_AddStringToObject(reqj, "query", query);
    cJSON_AddNumberToObject(reqj, "max_results", (double)want);
    if (sc->api_key != NULL && sc->api_key[0] != '\0') {
        cJSON_AddStringToObject(reqj, "api_key", sc->api_key);
    }
    body = jc_json_print(reqj);
    cJSON_Delete(reqj);
    if (body == NULL) {
        tu_err(out, "error: out of memory");
        return JC_OK;
    }

    jc_http_headers_init(&headers);
    jc_http_headers_add(&headers, "Content-Type: application/json");
    if (sc->api_key != NULL && sc->api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s",
                    sc->api_key);
        jc_http_headers_add(&headers, auth);
    }

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = sc->url;
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 30;
    req.abort_flag = &app->abort_flag;

    st = jc_http_perform(&req, &status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);

    if (st != JC_OK) {
        free(resp);
        tu_err(out, "error: web search request failed");
        return JC_OK;
    }
    if (status >= 400) {
        char msg[128];
        jc_snprintf(msg, sizeof(msg),
                    "error: web search endpoint returned HTTP %ld", status);
        free(resp);
        tu_err(out, msg);
        return JC_OK;
    }

    jc_sb_init(&sb);
    if (jc_websearch_format(resp, want, &sb) < 0) {
        jc_sb_free(&sb);
        free(resp);
        tu_err(out, "error: could not parse the search response");
        return JC_OK;
    }
    free(resp);
    {
        jc_size cap = jc_config_cap(app->config.search_max_bytes,
                                    WEB_SEARCH_MAX_BYTES);
        if (sb.len > cap) {
            sb.len = cap;
            sb.data[cap] = '\0';
            jc_sb_append(&sb, "\n... [truncated]");
        }
    }
    /* M300: search results are attacker-influenceable -- the snippets come from
     * whatever pages rank for the model's query. Fenced as data. */
    {
        struct jc_sb fenced;
        jc_sb_init(&fenced);
        jc_untrusted_wrap("search results", NULL,
                          sb.data != NULL ? sb.data : "", &fenced);
        jc_sb_free(&sb);
        tu_ok_owned(out, jc_sb_finish(&fenced));
        jc_sb_free(&fenced);
    }
    return JC_OK;
}

static const struct jc_tool WEB_SEARCH_TOOL = {
    "web_search",
    "Search the web and return ranked results (title, URL, snippet) for a "
    "query. Use for current information beyond the workspace.",
    web_search_schema,
    1, /* read-only */
    web_search_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_web_search(void)
{
    return &WEB_SEARCH_TOOL;
}
