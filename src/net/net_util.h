/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* net_util.h - shared helpers for the OpenAI-compatible REST clients
 * (embeddings and reranking). Internal to src/net; not a public API.
 *
 * Compiled only when libcurl is present (these live in src/net, which the
 * Makefile gates on HAVE_CURL).
 */
#ifndef JC_NET_UTIL_H
#define JC_NET_UTIL_H

#include "jc_platform.h"
#include "jc_config.h"

/* Join a base URL and a path into `buf` (capacity `cap`), collapsing a
 * duplicated '/' at the join. `base` may be NULL (treated as empty). */
void jc_url_join(const char *base, const char *path, char *buf, jc_size cap);

/* Build the full URL for `path` against a model's api_base, inserting "/v1"
 * when the base lacks it (the same rule jc_net_post_json applies). For callers
 * that need a non-JSON / binary response and so cannot use jc_net_post_json
 * (e.g. the TTS audio endpoint). */
void jc_net_url_v1(const struct jc_model_cfg *m, const char *path,
                   char *buf, jc_size cap);

/* POST `body` as application/json to `{model->api_base}{path}` with a Bearer
 * authorization header when the model carries an API key. On transport success
 * returns JC_OK, sets *http_status, and hands back a malloc'd NUL-terminated
 * response body via *resp_out (caller frees). On transport failure returns a
 * non-OK status and *resp_out is NULL. */
jc_status jc_net_post_json(const struct jc_model_cfg *m, const char *path,
                           const char *body, long *http_status,
                           char **resp_out, volatile int *abort);

#endif /* JC_NET_UTIL_H */
