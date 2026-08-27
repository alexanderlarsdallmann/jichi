/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_audiogen.h - OpenAI-compatible text-to-speech client (M32).
 *
 * Posts text to {api_base}/v1/audio/speech and returns the synthesized audio.
 * Unlike the JSON endpoints, the success body is RAW BINARY audio (an HTTP
 * error body is JSON), so the I/O path uses jc_http_perform with an explicit
 * length rather than jc_net_post_json (which is JSON/NUL-terminated). The pure
 * body builder + format helper are always available for offline tests; the I/O
 * requires libcurl (compiled under src/net, HAVE_CURL-gated).
 */
#ifndef JC_AUDIOGEN_H
#define JC_AUDIOGEN_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_config.h"

/* Build the request body: {"model","input",["voice"],"response_format":fmt}.
 * `voice`/`fmt` may be NULL (omitted). Returns a malloc'd JSON string (caller
 * frees) or NULL on OOM. Pure. */
char *jc_audiogen_build_body(const char *model, const char *input,
                             const char *voice, const char *fmt);

/* Map `path`'s extension to a TTS response_format token ("mp3", "wav", "opus",
 * "aac", "flac", "pcm"), or NULL when the extension is not supported.
 * Case-insensitive. Pure. */
const char *jc_audiogen_format(const char *path);

/* POST a TTS request via `m` (its api_base/api_key/model). On success the body
 * is raw audio bytes, returned via *out_bytes (malloc'd, caller frees) +
 * *out_len. JC_ERR_PROVIDER on an API/HTTP error; `abort` (may be NULL) cancels
 * in-flight transfers.
 *
 * `out_http` (may be NULL) receives the HTTP status when a response arrived, and
 * 0 when none did. M500: it exists because the caller has to be able to tell a
 * server failure from a bad argument -- without it the tool reported the same
 * sentence for both and a model burned 428 tokens retrying variations of a call
 * the server was answering 500 to. */
jc_status jc_audiogen_run(const struct jc_model_cfg *m, const char *input,
                          const char *voice, const char *fmt,
                          unsigned char **out_bytes, jc_size *out_len,
                          volatile int *abort, long *out_http);

#ifdef __cplusplus
}
#endif
#endif /* JC_AUDIOGEN_H */
