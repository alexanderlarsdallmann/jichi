/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_net.h - small networking helpers usable outside src/net.
 *
 * jc_net_reachable backs the model-fallback feature: a quick liveness probe of
 * an OpenAI-compatible server. Implemented in src/net/net_util.c (compiled with
 * libcurl); callers guard use with JC_HAVE_CURL.
 */
#ifndef JC_NET_H
#define JC_NET_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_mem.h"

/* Parse an OpenAI-style model list -- {"data":[{"id":"..."}...]} (or a bare
 * array of such objects) -- pushing each id as an arena-owned char* into
 * `out_ids`. Pure and unit-tested. Returns JC_OK (even for zero ids) or
 * JC_ERR_PARSE if `json` is not valid JSON. */
jc_status jc_net_parse_models(const char *json, struct jc_vec *out_ids,
                              struct jc_arena *a);

/* Fetch `{api_base}/models` (Bearer `api_key` when non-empty) and parse the
 * ids into `out_ids` (via jc_net_parse_models). Returns JC_OK on a successful
 * fetch + parse, else an error. `timeout_secs` <= 0 uses a small default. */
jc_status jc_net_list_models(const char *api_base, const char *api_key,
                             long timeout_secs, volatile int *abort,
                             struct jc_vec *out_ids, struct jc_arena *a);

/* Probe whether the server at `api_base` is reachable: issue a short-timeout
 * GET to `{api_base}/models` (Bearer `api_key` when non-empty). Returns 1 when
 * the server answers with ANY HTTP status (even 401/404 — it's up), 0 on a
 * transport/connect/timeout failure. `timeout_secs` <= 0 uses a small default;
 * `abort` (nullable) cancels the probe. */

/* The gateway's own account of a model's context window, where it publishes
 * one. `/v1/model/info` is a LiteLLM extension, NOT part of the OpenAI
 * surface, so the outcomes are deliberately distinct: JC_OK (published;
 * *out_max_input > 0), JC_ERR_NOTFOUND (endpoint answered, no limit for this
 * model), JC_ERR_HTTP (refused -- *out_http_status distinguishes a 404 "this
 * server has no such endpoint" from a 401 "key rejected"), JC_ERR_PARSE.
 * `out_max_output` and `out_http_status` may be NULL. Fetches the whole model
 * table, so it belongs in diagnostics, never a hot path. */
/* The parse half of jc_net_model_limits, callable without a server.
 * JC_OK (found, *out_max_input > 0), JC_ERR_NOTFOUND (a model table
 * that does not list this model, or lists it with a null limit),
 * JC_ERR_PARSE (not a model table at all -- which is how a server
 * WITHOUT this endpoint is recognised: LM Studio answers HTTP 200
 * with an error object). Both out-params may be NULL. */
jc_status jc_net_parse_model_limits(const char *json,
                                    const char *model_id,
                                    long *out_max_input,
                                    long *out_max_output);

jc_status jc_net_model_limits(const char *api_base, const char *api_key,
                              const char *model_id, long timeout_secs,
                              volatile int *abort, long *out_max_input,
                              long *out_max_output, long *out_http_status);

int jc_net_reachable(const char *api_base, const char *api_key,
                     long timeout_secs, volatile int *abort);

#ifdef __cplusplus
}
#endif
#endif /* JC_NET_H */
