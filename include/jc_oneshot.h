/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_oneshot.h - a single non-streaming prompt->text model call.
 *
 * The provider streaming machinery is overkill for one-shot helper calls
 * (query rewrite, the setup advisor, small classifications). This factors the
 * build_request -> jc_http_perform -> parse_full sequence into one call that
 * returns the assistant's text.
 */
#ifndef JC_ONESHOT_H
#define JC_ONESHOT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_provider;

/* Perform one non-streaming call against `prov`: a single user message
 * (`user_msg`), optional `system_msg` (may be NULL), no tools. Returns the
 * assistant's text (malloc'd; caller frees) or NULL on any failure. A
 * `timeout_secs` <= 0 uses a 60s default; `abort_flag` may be NULL. */
char *jc_oneshot(struct jc_provider *prov, const char *system_msg,
                 const char *user_msg, long timeout_secs,
                 volatile int *abort_flag);

/* Like jc_oneshot, but when the call fails and `timed_out` is non-NULL, sets
 * *timed_out to 1 iff the failure was a stall/timeout (vs. an HTTP error or an
 * empty completion) -- so a caller can tell a cold model-load timeout apart
 * from a model that returned nothing. *timed_out is set to 0 on success or a
 * non-timeout failure. jc_oneshot() is this with timed_out = NULL. */
char *jc_oneshot_ex(struct jc_provider *prov, const char *system_msg,
                    const char *user_msg, long timeout_secs,
                    volatile int *abort_flag, int *timed_out);

/* What one probe call observed. Strings are malloc'd; free with
 * jc_oneshot_result_free. */
struct jc_oneshot_result {
    char  *text;        /* assistant text, or NULL                          */
    char  *call_name;   /* name of the FIRST native tool call, or NULL      */
    int    ncalls;      /* number of native tool calls parsed               */
    double in_tokens;   /* the server's own prompt-token count (0 if absent)*/
    long   http_status; /* 0 when the request never completed               */
};

void jc_oneshot_result_free(struct jc_oneshot_result *r);

/* One non-streaming call that ADVERTISES tools and reports what came back
 * (M167). `tools` is a neutral tool array as built by jc_tool_build_neutral, or
 * NULL. Unlike jc_oneshot, the caller can see native tool calls -- which is the
 * whole point for the `doctor --live` probe, since a tool call is the signal
 * being measured. `in_tokens` comes from the provider's usage, giving the real
 * tokenizer count for free (useful evidence: the byte/4 estimate is only
 * approximate, and its error is model-specific -- see docs/COMPACTION.md).
 *
 * Returns JC_OK when the HTTP call completed and parsed, whatever the model
 * said; a non-answer is data, not an error. `out` must be non-NULL and is fully
 * overwritten. */
jc_status jc_oneshot_probe(struct jc_provider *prov, const char *system_msg,
                           const char *user_msg, const void *tools,
                           long timeout_secs, volatile int *abort_flag,
                           struct jc_oneshot_result *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_ONESHOT_H */
