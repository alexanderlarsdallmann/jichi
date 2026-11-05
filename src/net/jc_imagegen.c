/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_imagegen.c - OpenAI-compatible image-generation client (see header). */

#include "jc_imagegen.h"
#include "jc_json.h"
#include "jc_log.h"
#include "jc_base64.h"
#include "jc_http.h"
#include "jc_snprintf.h"
#include "jc_net.h"
#include "net_util.h"

#include <stdlib.h>
#include <string.h>

/* M1093 (C-09): the image side call's named TOTAL cap (a CAP, not a fence),
 * shared by both legs (the POST and the URL fetch). The connect + dead-stream
 * FENCES come from jc_net_side_fences. */
#define JC_NET_IMAGEGEN_TIMEOUT_SECS 60

/* M1092-b (SV:0219): slack for the non-b64 JSON framing of the b64_json
 * envelope, {"created":N,"data":[{"b64_json":"..."}]} -- a few hundred bytes.
 * 2048 is generous framing slack, well below one page. */
#define ENVELOPE_JSON_SLACK 2048L

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

/* GET a (public/presigned) image URL into the out_bytes/out_len pair.
 * `max_bytes` (C-06) is the transfer cap (0 => the library default ceiling);
 * `out_overflow` (may be NULL) receives 1 when the transfer was cut at it. */
static jc_status fetch_image_url(const char *url, unsigned char **out_bytes,
                                 jc_size *out_len, volatile int *abort,
                                 long max_bytes, int *out_overflow)
{
    struct jc_http_request req;
    char *body = NULL;
    jc_size body_len = 0;
    long status = 0;
    jc_status st;

    if (out_overflow != NULL) {
        *out_overflow = 0;
    }
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.timeout_secs = JC_NET_IMAGEGEN_TIMEOUT_SECS;
    req.abort_flag = abort;
    req.max_bytes = max_bytes; /* C-06: 0 => the library's default ceiling */
    /* C-07 (M1119): the reply URL is SERVER- or MITM-named, so it is an
     * untrusted URL and gets the connect-time private-address guard (the
     * fetch_url house standard, jc_tool_fetch.c). The POST leg above stays
     * unguarded: it is the operator-configured, trusted api_base. */
    req.block_private_addrs = 1;
    jc_net_side_fences(&req, NULL); /* M1093: no jc_config in scope: built-in */
    st = jc_http_perform(&req, &status, &body, &body_len);
    if (st != JC_OK) {
        if (out_overflow != NULL) {
            *out_overflow = req.overflow;
        }
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
                          jc_size *out_len, volatile int *abort,
                          long max_bytes, int *out_overflow)
{
    char *body;
    char *resp = NULL;
    char *url = NULL;
    long status = 0;
    jc_status st;
    int overflow = 0;
    char post_url[1024];
    char auth[1200];
    struct jc_http_request req;
    struct jc_http_headers headers;

    *out_bytes = NULL;
    *out_len = 0;
    if (out_overflow != NULL) {
        *out_overflow = 0;
    }
    if (m == NULL || prompt == NULL) {
        return JC_ERR_INVALID;
    }
    body = jc_imagegen_build_body_ex(m->model, prompt, size, out_format,
                                     ref_images, n_ref);
    if (body == NULL) {
        return JC_ERR_OOM;
    }
    /* C-06: the POST leg goes through jc_net_post_json's exact request shape,
     * inlined here so the transfer carries max_bytes (that helper is shared
     * with the embeddings/rerank clients and cannot grow the parameter). The
     * reply is the b64_json JSON envelope, so a huge envelope aborts at the
     * cap before it fills memory.
     *
     * M1092-b (SV:0219): the transfer cap is the ENVELOPE's size bound, not
     * the decoded cap: max_bytes is the decoded image cap (the argument), and
     * a b64 image of that size encodes to jc_base64_encoded_len(max_bytes)
     * plus the small JSON framing slack -- so every LEGAL image's envelope
     * clears the transfer bound, and only an image over the cap (or a hostile
     * oversized envelope) trips it. (Capping the transfer at the decoded cap
     * itself cut in transit any image above ~73% of the cap.) */
    jc_net_url_v1(m, "/images/generations", post_url, sizeof(post_url));
    jc_http_headers_init(&headers);
    jc_http_headers_add(&headers, "Content-Type: application/json");
    if (m->api_key != NULL && m->api_key[0] != '\0') {
        jc_snprintf(auth, sizeof(auth), "Authorization: Bearer %s", m->api_key);
        jc_http_headers_add(&headers, auth);
    }
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = post_url;
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = JC_NET_IMAGEGEN_TIMEOUT_SECS;
    req.abort_flag = abort;
    jc_net_side_fences(&req, NULL); /* M1093: no jc_config in scope: built-in */
    if (max_bytes > 0) {
        /* M1092-b (SV:0219): cap the TRANSFER at the b64_json envelope's
         * size bound (jc_base64_encoded_len(cap) + the small framing slack),
         * not at the decoded image cap -- the reply is the envelope, ~1.37x
         * the image. 0 keeps the library's default ceiling. The url-only GET
         * leg (fetch_image_url) returns RAW bytes and keeps max_bytes as is. */
        req.max_bytes = jc_base64_encoded_len((jc_size)max_bytes) +
                       ENVELOPE_JSON_SLACK;
    } else {
        req.max_bytes = 0; /* C-06: the library's default ceiling */
    }
    st = jc_http_perform(&req, &status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);
    if (st != JC_OK) {
        overflow = req.overflow; /* C-06: was the POST cut at the cap */
        free(resp);
        if (out_overflow != NULL) {
            *out_overflow = overflow;
        }
        return st;
    }
    if (status >= 400 || resp == NULL) {
        /* M1131 (C-13): cap the error body to 2048 before logging (see
         * jc_log_err_body_cap); resp NULL -> cap NULL -> just the status. */
        char *cap = jc_log_err_body_cap(resp, 2048);
        jc_logf(JC_LOG_ERROR, "image generation: HTTP %ld%s%s", status,
                cap != NULL ? " " : "", cap != NULL ? cap : "");
        free(cap);
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
        st = fetch_image_url(url, out_bytes, out_len, abort, max_bytes,
                             &overflow);
        free(url);
        if (out_overflow != NULL) {
            *out_overflow = overflow;
        }
        return st;
    }
    free(url);
    jc_logf(JC_LOG_ERROR, "image generation: response had no b64_json");
    return JC_ERR_PROVIDER;
}
