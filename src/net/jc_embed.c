/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_embed.c - OpenAI-compatible embeddings client. */

#include "jc_embed.h"
#include "jc_json.h"
#include "jc_log.h"
#include "net_util.h"

#include <stdlib.h>
#include <string.h>

#define JC_EMBED_BATCH 64

/* Build the request body {"model":<id>,"input":[texts...]}. Returns a malloc'd
 * string (caller frees) or NULL on failure. */
static char *build_body(const char *model, const char *const *texts, int n)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    int i;
    char *text;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", model != NULL ? model : "");
    arr = cJSON_AddArrayToObject(root, "input");
    for (i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(texts[i] != NULL
                                                     ? texts[i] : ""));
    }
    text = jc_json_print(root);
    cJSON_Delete(root);
    return text;
}

jc_status jc_embed_parse(const char *json, int expected,
                         float **out, int *out_dim)
{
    cJSON *root;
    cJSON *data;
    cJSON *item;
    float *vecs = NULL;
    int dim = -1;
    int count;

    *out = NULL;
    *out_dim = 0;
    if (json == NULL) {
        return JC_ERR_PARSE;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    data = jc_json_get_obj(root, "data");
    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) != expected) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }

    /* First pass establishes the dimension from item 0's embedding. */
    item = cJSON_GetArrayItem(data, 0);
    {
        cJSON *emb = (item != NULL) ? cJSON_GetObjectItem(item, "embedding")
                                    : NULL;
        if (!cJSON_IsArray(emb) || cJSON_GetArraySize(emb) <= 0) {
            cJSON_Delete(root);
            return JC_ERR_PARSE;
        }
        dim = cJSON_GetArraySize(emb);
    }

    vecs = (float *)malloc((jc_size)expected * (jc_size)dim * sizeof(float));
    if (vecs == NULL) {
        cJSON_Delete(root);
        return JC_ERR_OOM;
    }

    /* Place each row at its "index" (default to encounter order). */
    count = 0;
    for (item = data->child; item != NULL; item = item->next) {
        cJSON *emb = cJSON_GetObjectItem(item, "embedding");
        cJSON *idx_node = cJSON_GetObjectItem(item, "index");
        int idx = cJSON_IsNumber(idx_node) ? (int)idx_node->valuedouble : count;
        cJSON *v;
        int j;

        if (!cJSON_IsArray(emb) || cJSON_GetArraySize(emb) != dim ||
            idx < 0 || idx >= expected) {
            free(vecs);
            cJSON_Delete(root);
            return JC_ERR_PARSE;
        }
        j = 0;
        for (v = emb->child; v != NULL && j < dim; v = v->next) {
            vecs[(jc_size)idx * (jc_size)dim + (jc_size)j] =
                (float)v->valuedouble;
            j++;
        }
        count++;
    }

    cJSON_Delete(root);
    *out = vecs;
    *out_dim = dim;
    return JC_OK;
}

jc_status jc_embed_texts(const struct jc_model_cfg *m,
                         const char *const *texts, int n,
                         float **out_vecs, int *out_dim,
                         volatile int *abort)
{
    float *big = NULL;
    int dim = 0;
    int done = 0;
    jc_status st = JC_OK;

    *out_vecs = NULL;
    *out_dim = 0;
    if (m == NULL || n <= 0) {
        return JC_ERR_INVALID;
    }

    while (done < n) {
        int bn = n - done;
        char *body;
        char *resp = NULL;
        long status = 0;
        float *batch = NULL;
        int bdim = 0;

        if (bn > JC_EMBED_BATCH) {
            bn = JC_EMBED_BATCH;
        }
        body = build_body(m->model, texts + done, bn);
        if (body == NULL) {
            st = JC_ERR_OOM;
            break;
        }
        st = jc_net_post_json(m, "/embeddings", body, &status, &resp, abort);
        free(body);
        if (st != JC_OK) {
            free(resp);
            break;
        }
        if (status >= 400 || resp == NULL) {
            jc_logf(JC_LOG_ERROR, "embeddings: HTTP %ld%s%s", status,
                    resp != NULL ? " " : "", resp != NULL ? resp : "");
            free(resp);
            st = JC_ERR_PROVIDER;
            break;
        }
        st = jc_embed_parse(resp, bn, &batch, &bdim);
        free(resp);
        if (st != JC_OK) {
            break;
        }
        if (big == NULL) {
            dim = bdim;
            big = (float *)malloc((jc_size)n * (jc_size)dim * sizeof(float));
            if (big == NULL) {
                free(batch);
                st = JC_ERR_OOM;
                break;
            }
        } else if (bdim != dim) {
            free(batch);
            st = JC_ERR_PROVIDER;
            break;
        }
        memcpy(big + (jc_size)done * (jc_size)dim, batch,
               (jc_size)bn * (jc_size)dim * sizeof(float));
        free(batch);
        done += bn;
    }

    if (st != JC_OK) {
        free(big);
        return st;
    }
    *out_vecs = big;
    *out_dim = dim;
    return JC_OK;
}
