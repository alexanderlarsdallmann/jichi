/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_imagegen.c - OpenAI-compatible image-generation client (see header). */

#include "jc_imagegen.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_base64.h"
#include "jc_http.h"
#include "net_util.h"

#include <stdlib.h>
#include <string.h>

char *jc_imagegen_build_body_ex(const char *model, const char *prompt,
                                const char *size, const char *out_format,
                                const char *const *ref_images, int n_ref)
{
    cJSON *root = cJSON_CreateObject();
    char *text;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", model != NULL ? model : "");
    cJSON_AddStringToObject(root, "prompt", prompt != NULL ? prompt : "");
    cJSON_AddNumberToObject(root, "n", 1.0);
    cJSON_AddStringToObject(root, "response_format", "b64_json");
    if (size != NULL && size[0] != '\0') {
        cJSON_AddStringToObject(root, "size", size);
    }
    if (out_format != NULL && out_format[0] != '\0') {
        cJSON_AddStringToObject(root, "output_format", out_format);
    }
    if (ref_images != NULL && n_ref > 0) {
        cJSON *arr = cJSON_CreateArray();
        int i;
        if (arr == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
        for (i = 0; i < n_ref; i++) {
            if (ref_images[i] != NULL) {
                cJSON_AddItemToArray(arr, cJSON_CreateString(ref_images[i]));
            }
        }
        cJSON_AddItemToObject(root, "ref_images", arr);
    }
    text = jc_json_print(root);
    cJSON_Delete(root);
    return text;
}

char *jc_imagegen_build_body(const char *model, const char *prompt,
                             const char *size, const char *out_format)
{
    return jc_imagegen_build_body_ex(model, prompt, size, out_format, NULL, 0);
}

/* malloc a NUL-terminated copy of `s` (NULL-safe). */
static char *dup_str(const char *s)
{
    jc_size n;
    char *out;
    if (s == NULL) {
        return NULL;
    }
    n = (jc_size)strlen(s);
    out = (char *)malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n + 1);
    }
    return out;
}

jc_status jc_imagegen_parse(const char *json, unsigned char **out_bytes,
                            jc_size *out_len, char **out_url)
{
    cJSON *root;
    cJSON *data;
    cJSON *item0;
    const char *b64;
    const char *url;

    *out_bytes = NULL;
    *out_len = 0;
    *out_url = NULL;
    if (json == NULL) {
        return JC_ERR_PARSE;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    data = jc_json_get_obj(root, "data");
    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) < 1) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    item0 = cJSON_GetArrayItem(data, 0);
    b64 = jc_json_get_str(item0, "b64_json", NULL);
    if (b64 != NULL && b64[0] != '\0') {
        jc_size cap = jc_base64_decoded_len((jc_size)strlen(b64));
        unsigned char *buf = (unsigned char *)malloc(cap > 0 ? cap : 1);
        jc_size len = 0;
        if (buf == NULL) {
            cJSON_Delete(root);
            return JC_ERR_OOM;
        }
        if (jc_base64_decode(b64, buf, cap, &len) != JC_OK) {
            free(buf);
            cJSON_Delete(root);
            return JC_ERR_PARSE;
        }
        cJSON_Delete(root);
        *out_bytes = buf;
        *out_len = len;
        return JC_OK;
    }
    url = jc_json_get_str(item0, "url", NULL);
    if (url != NULL && url[0] != '\0') {
        char *u = dup_str(url);
        cJSON_Delete(root);
        if (u == NULL) {
            return JC_ERR_OOM;
        }
        *out_url = u;
        return JC_OK;
    }
    cJSON_Delete(root);
    return JC_ERR_PARSE;
}

/* GET a (public/presigned) image URL into the out_bytes/out_len pair. */
static jc_status fetch_image_url(const char *url, unsigned char **out_bytes,
                                 jc_size *out_len, volatile int *abort)
{
    struct jc_http_request req;
    char *body = NULL;
    jc_size body_len = 0;
    long status = 0;
    jc_status st;

    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.timeout_secs = 60;
    req.abort_flag = abort;
    st = jc_http_perform(&req, &status, &body, &body_len);
    if (st != JC_OK) {
        free(body);
        return st;
    }
    if (status >= 400 || body == NULL) {
        jc_logf(JC_LOG_ERROR, "image url fetch: HTTP %ld", status);
        free(body);
        return JC_ERR_PROVIDER;
    }
    *out_bytes = (unsigned char *)body;
    *out_len = body_len;
    return JC_OK;
}

jc_status jc_imagegen_run(const struct jc_model_cfg *m, const char *prompt,
                          const char *size, const char *out_format,
                          const char *const *ref_images, int n_ref,
                          int fetch_url, unsigned char **out_bytes,
                          jc_size *out_len, volatile int *abort)
{
    char *body;
    char *resp = NULL;
    char *url = NULL;
    long status = 0;
    jc_status st;

    *out_bytes = NULL;
    *out_len = 0;
    if (m == NULL || prompt == NULL) {
        return JC_ERR_INVALID;
    }
    body = jc_imagegen_build_body_ex(m->model, prompt, size, out_format,
                                     ref_images, n_ref);
    if (body == NULL) {
        return JC_ERR_OOM;
    }
    st = jc_net_post_json(m, "/images/generations", body, &status, &resp, abort);
    free(body);
    if (st != JC_OK) {
        free(resp);
        return st;
    }
    if (status >= 400 || resp == NULL) {
        jc_logf(JC_LOG_ERROR, "image generation: HTTP %ld%s%s", status,
                resp != NULL ? " " : "", resp != NULL ? resp : "");
        free(resp);
        return JC_ERR_PROVIDER;
    }
    st = jc_imagegen_parse(resp, out_bytes, out_len, &url);
    free(resp);
    if (st != JC_OK) {
        return st;
    }
    if (*out_bytes != NULL) {
        free(url);
        return JC_OK;
    }
    /* url-only response. */
    if (url != NULL && fetch_url) {
        st = fetch_image_url(url, out_bytes, out_len, abort);
        free(url);
        return st;
    }
    free(url);
    jc_logf(JC_LOG_ERROR, "image generation: response had no b64_json");
    return JC_ERR_PROVIDER;
}
