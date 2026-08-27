/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rerank.c - OpenAI/Cohere-compatible reranking client. */

#include "jc_rerank.h"
#include "jc_json.h"
#include "jc_log.h"
#include "net_util.h"

#include <stdlib.h>
#include <string.h>

static char *build_body(const char *model, const char *query,
                        const char *const *docs, int n)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    int i;
    char *text;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", model != NULL ? model : "");
    cJSON_AddStringToObject(root, "query", query != NULL ? query : "");
    arr = cJSON_AddArrayToObject(root, "documents");
    for (i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(docs[i] != NULL
                                                     ? docs[i] : ""));
    }
    cJSON_AddNumberToObject(root, "top_n", (double)n);
    text = jc_json_print(root);
    cJSON_Delete(root);
    return text;
}

jc_status jc_rerank_parse(const char *json, int n, double *out_scores)
{
    cJSON *root;
    cJSON *arr;
    cJSON *item;
    int i;

    for (i = 0; i < n; i++) {
        out_scores[i] = 0.0;
    }
    if (json == NULL) {
        return JC_ERR_PARSE;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    arr = jc_json_get_obj(root, "data");
    if (!cJSON_IsArray(arr)) {
        arr = jc_json_get_obj(root, "results");
    }
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }

    for (item = arr->child; item != NULL; item = item->next) {
        cJSON *idx_node = cJSON_GetObjectItem(item, "index");
        cJSON *score_node = cJSON_GetObjectItem(item, "relevance_score");
        int idx;
        if (!cJSON_IsNumber(score_node)) {
            score_node = cJSON_GetObjectItem(item, "score");
        }
        idx = cJSON_IsNumber(idx_node) ? (int)idx_node->valuedouble : -1;
        if (idx >= 0 && idx < n && cJSON_IsNumber(score_node)) {
            out_scores[idx] = score_node->valuedouble;
        }
    }

    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_rerank_score(const struct jc_model_cfg *m, const char *query,
                          const char *const *docs, int n, double *out_scores,
                          volatile int *abort)
{
    char *body;
    char *resp = NULL;
    long status = 0;
    jc_status st;

    if (m == NULL || n <= 0) {
        return JC_ERR_INVALID;
    }
    body = build_body(m->model, query, docs, n);
    if (body == NULL) {
        return JC_ERR_OOM;
    }
    st = jc_net_post_json(m, "/rerank", body, &status, &resp, abort);
    free(body);
    if (st != JC_OK) {
        free(resp);
        return st;
    }
    if (status >= 400 || resp == NULL) {
        jc_logf(JC_LOG_ERROR, "rerank: HTTP %ld%s%s", status,
                resp != NULL ? " " : "", resp != NULL ? resp : "");
        free(resp);
        return JC_ERR_PROVIDER;
    }
    st = jc_rerank_parse(resp, n, out_scores);
    free(resp);
    return st;
}
