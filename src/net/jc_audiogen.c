/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_audiogen.c - OpenAI-compatible text-to-speech client (see header). */

#include "jc_audiogen.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_http.h"
#include "jc_snprintf.h"
#include "net_util.h"

#include <stdlib.h>
#include <string.h>

char *jc_audiogen_build_body(const char *model, const char *input,
                             const char *voice, const char *fmt)
{
    cJSON *root = cJSON_CreateObject();
    char *text;

    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "model", model != NULL ? model : "");
    cJSON_AddStringToObject(root, "input", input != NULL ? input : "");
    if (voice != NULL && voice[0] != '\0') {
        cJSON_AddStringToObject(root, "voice", voice);
    }
    if (fmt != NULL && fmt[0] != '\0') {
        cJSON_AddStringToObject(root, "response_format", fmt);
    }
    text = jc_json_print(root);
    cJSON_Delete(root);
    return text;
}

/* Case-insensitive compare of `a` against lowercase literal `b`. */
static int ext_eq(const char *a, const char *b)
{
    jc_size i;
    for (i = 0; b[i] != '\0'; i++) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != b[i]) {
            return 0;
        }
    }
    return a[i] == '\0';
}

const char *jc_audiogen_format(const char *path)
{
    const char *base;
    const char *slash;
    const char *dot;
    const char *ext;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    base = (slash != NULL) ? slash + 1 : path;
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base || dot[1] == '\0') {
        return NULL;
    }
    ext = dot + 1;
    if (ext_eq(ext, "mp3"))  return "mp3";
    if (ext_eq(ext, "wav"))  return "wav";
    if (ext_eq(ext, "opus")) return "opus";
    if (ext_eq(ext, "aac"))  return "aac";
    if (ext_eq(ext, "flac")) return "flac";
    if (ext_eq(ext, "pcm"))  return "pcm";
    return NULL;
}

jc_status jc_audiogen_run(const struct jc_model_cfg *m, const char *input,
                          const char *voice, const char *fmt,
                          unsigned char **out_bytes, jc_size *out_len,
                          volatile int *abort, long *out_http)
{
    char *body;
    char *resp = NULL;
    jc_size resp_len = 0;
    long status = 0;
    char url[1024];
    char auth[1200];
    struct jc_http_request req;
    struct jc_http_headers headers;
    jc_status st;

    *out_bytes = NULL;
    *out_len = 0;
    if (m == NULL || input == NULL) {
        return JC_ERR_INVALID;
    }
    body = jc_audiogen_build_body(m->model, input, voice, fmt);
    if (body == NULL) {
        return JC_ERR_OOM;
    }
    jc_net_url_v1(m, "/audio/speech", url, sizeof(url));

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
    req.body_len = strlen(body);
    req.timeout_secs = 60;
    req.abort_flag = abort;

    st = jc_http_perform(&req, &status, &resp, &resp_len);
    jc_http_headers_free(&headers);
    free(body);

    if (out_http != NULL) {
        /* 0 when the transport never got a reply -- a distinction the caller's
         * message depends on (M500). */
        *out_http = (st == JC_OK) ? status : 0;
    }
    if (st != JC_OK) {
        free(resp);
        return st;
    }
    if (status >= 400 || resp == NULL) {
        /* The error body (when present) is JSON. */
        jc_logf(JC_LOG_ERROR, "audio generation: HTTP %ld%s%s", status,
                resp != NULL ? " " : "", resp != NULL ? resp : "");
        free(resp);
        return JC_ERR_PROVIDER;
    }
    *out_bytes = (unsigned char *)resp;
    *out_len = resp_len;
    return JC_OK;
}
