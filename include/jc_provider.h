/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_provider.h - pluggable LLM provider abstraction.
 *
 * One interface, two implementations: the Anthropic Messages API and
 * OpenAI-compatible chat completions. Both support streaming (SSE) and native
 * tool calling. The agent loop never branches on provider; it talks only to
 * this vtable.
 *
 * Streaming model: the agent feeds SSE events to vt->on_event, which appends
 * the assistant's text and tool calls into a caller-owned jc_message and
 * streams text deltas to a jc_stream_sink for live display. When the message
 * is complete, on_event sets *done.
 *
 * Tools are described in a NEUTRAL JSON shape that each provider maps to its
 * own wire format:
 *   [ { "name": ..., "description": ..., "parameters": <JSON schema object> } ]
 */
#ifndef JC_PROVIDER_H
#define JC_PROVIDER_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_config.h"
#include "jc_message.h"
#include "jc_http.h"
#include "jc_sse.h"
#include "cJSON.h"

/* Live-display sink for streamed assistant text. */
struct jc_stream_sink {
    void (*on_text)(void *user, const char *delta, jc_size n);
    void *user;
};

struct jc_provider;

struct jc_provider_vtable {
    /* Serialise a request body. *body_out is malloc'd (caller frees). */
    jc_status (*build_request)(struct jc_provider *self,
                               const struct jc_history *hist,
                               const char *system_msg,
                               const cJSON *tools,
                               int streaming,
                               char **body_out);

    /* Add auth/content headers for this provider. */
    void (*add_headers)(struct jc_provider *self, struct jc_http_headers *h);

    /* Full endpoint URL for the chat/messages call. */
    const char *(*endpoint)(struct jc_provider *self);

    /* Reset per-turn streaming scratch before a new stream. */
    void (*stream_reset)(struct jc_provider *self);

    /* Parse one SSE event: append text/tool-calls into `out`, stream text via
     * `sink`, set *done when the assistant message is complete. */
    void (*on_event)(struct jc_provider *self,
                     const struct jc_sse_event *ev,
                     struct jc_message *out,
                     struct jc_stream_sink *sink,
                     int *done);

    /* The HTTP stream ENDED without on_event ever setting *done: a cut
     * connection, a server that omits the terminal event, or a final event that
     * arrived without its terminating blank line (SSE frames end on one, so an
     * unterminated last frame is never dispatched at all).
     *
     * Flush whatever was accumulated into `out`. Without this the text was
     * streamed to the SINK -- the user watched it appear -- and then dropped,
     * because the only flush was on the terminal event: measured on the stable
     * `--output json` contract, a run that printed HELLO reported
     * `"text": "", "stop_reason": "done"`. An empty answer with a success
     * verdict is worse than an error, since nothing downstream can tell.
     *
     * Optional: NULL means the provider has nothing to finish. M521. */
    void (*stream_end)(struct jc_provider *self, struct jc_message *out);

    /* Parse a non-streaming full response body into `out`. */
    jc_status (*parse_full)(struct jc_provider *self, const char *body,
                            struct jc_message *out);

    /* Token usage from the most recent stream (0 if unknown). */
    void (*get_usage)(struct jc_provider *self, double *in_tok,
                      double *out_tok);

    /* Prompt-cache token usage from the most recent stream (0 if unknown or
     * unsupported). read_in: input tokens served from a cache hit; write_in:
     * input tokens written to the cache. Anthropic reports both;
     * OpenAI-compatible servers report read_in only (write_in stays 0). */
    void (*get_cache_usage)(struct jc_provider *self, double *read_in,
                            double *write_in);

    void (*free)(struct jc_provider *self);
};

struct jc_provider {
    const struct jc_provider_vtable *vt;
    const struct jc_model_cfg       *model;
    void                            *state; /* provider-private */
};

/* Factory: chooses the implementation from model->provider. The provider
 * holds a reference to `model` (which must outlive it). Returns NULL on
 * unknown provider or OOM. The returned provider is malloc-backed; release
 * with provider->vt->free(provider). */
struct jc_provider *jc_provider_create(const struct jc_model_cfg *model);

/* Implementations (used by the factory). */
struct jc_provider *jc_provider_openai_create(const struct jc_model_cfg *model);
struct jc_provider *jc_provider_anthropic_create(const struct jc_model_cfg *model);

#ifdef __cplusplus
}
#endif
#endif /* JC_PROVIDER_H */
