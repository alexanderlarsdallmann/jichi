/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_transcribe.c - OpenAI-compatible transcription client (see header). */

#include "jc_transcribe.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_http.h"
#include "jc_snprintf.h"
#include "jc_multipart.h"
#include "net_util.h"

#include <stdlib.h>
#include <string.h>

jc_status jc_transcribe_parse(const char *json, char **out)
{
    cJSON *root;
    const char *text;
    jc_size n;
    char *copy;

    *out = NULL;
    if (json == NULL) {
        return JC_ERR_PARSE;
    }
    root = jc_json_parse(json);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    text = jc_json_get_str(root, "text", NULL);
    if (text == NULL) {
        cJSON_Delete(root);
        return JC_ERR_PARSE;
    }
    n = (jc_size)strlen(text);
    copy = (char *)malloc(n + 1);
    if (copy == NULL) {
        cJSON_Delete(root);
        return JC_ERR_OOM;
    }
    memcpy(copy, text, n + 1);
    cJSON_Delete(root);
    *out = copy;
    return JC_OK;
}

jc_status jc_transcribe_run(const struct jc_model_cfg *m,
                            const unsigned char *audio, jc_size len,
                            const char *filename, const char *content_type,
                            const char *language, char **out,
                            volatile int *abort, long *out_http)
{
    struct jc_multipart mp;
    char *body;
    jc_size body_len = 0;
    char ctype[160];
    char url[1024];
    char auth[1200];
    struct jc_http_headers headers;
    struct jc_http_request req;
    char *resp = NULL;
    long status = 0;
    jc_status st;

    *out = NULL;
    if (m == NULL || audio == NULL) {
        return JC_ERR_INVALID;
    }

    jc_multipart_init(&mp);
    jc_multipart_field(&mp, "model", m->model != NULL ? m->model : "");
    jc_multipart_field(&mp, "response_format", "json");
    if (language != NULL && language[0] != '\0') {
        jc_multipart_field(&mp, "language", language);
    }
    jc_multipart_file(&mp, "file", filename != NULL ? filename : "audio",
                      content_type, audio, len);
    jc_multipart_content_type(&mp, ctype, sizeof(ctype));
    body = jc_multipart_finish(&mp, &body_len);
    jc_multipart_free(&mp);
    if (body == NULL) {
        return JC_ERR_OOM;
    }

    jc_net_url_v1(m, "/audio/transcriptions", url, sizeof(url));
    jc_http_headers_init(&headers);
    {
        char ch[200];
        jc_snprintf(ch, sizeof(ch), "Content-Type: %s", ctype);
        jc_http_headers_add(&headers, ch);
    }
    if (m->api_key != NULL && m->api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", m->api_key);
        jc_http_headers_add(&headers, auth);
    }

    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = url;
    req.headers = &headers;
    req.body = body;
    req.body_len = body_len;
    req.timeout_secs = 120;
    req.abort_flag = abort;

    st = jc_http_perform(&req, &status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);

    if (out_http != NULL) {
        *out_http = (st == JC_OK) ? status : 0;   /* M500 */
    }
    if (st != JC_OK) {
        free(resp);
        return st;
    }
    if (status >= 400 || resp == NULL) {
        jc_logf(JC_LOG_ERROR, "transcription: HTTP %ld%s%s", status,
                resp != NULL ? " " : "", resp != NULL ? resp : "");
        free(resp);
        return JC_ERR_PROVIDER;
    }
    st = jc_transcribe_parse(resp, out);
    free(resp);
    return st;
}
