/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_transcribe.h - OpenAI-compatible speech-to-text (transcription) client (M33).
 *
 * Uploads audio to {api_base}/v1/audio/transcriptions as multipart/form-data and
 * returns the transcript text. The request side requires libcurl (compiled under
 * src/net, HAVE_CURL-gated); the pure response parser is always available so it
 * can be unit-tested offline.
 */
#ifndef JC_TRANSCRIBE_H
#define JC_TRANSCRIBE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_config.h"

/* Built-in input cap for transcription (OpenAI's 25 MB limit); 0 in config
 * resolves to this via jc_config_cap. */
#define JC_TRANSCRIBE_MAX_BYTES (25L * 1024L * 1024L)

/* Parse a transcription response, extracting the "text" field into *out (a
 * malloc'd NUL-terminated string; caller frees). Accepts the plain
 * {"text":"..."} shape (also the common verbose-json shape, which still carries
 * a top-level "text"). Returns JC_ERR_PARSE on malformed input or a missing
 * text field. Network-free. */
jc_status jc_transcribe_parse(const char *json, char **out);

/* POST `audio` (`len` bytes, labelled `filename` + `content_type`) to the
 * transcription endpoint of `m`, with the model id and an optional `language`
 * hint (ISO-639-1; NULL to omit). On success returns JC_OK and the transcript
 * via *out (malloc'd, caller frees). JC_ERR_PROVIDER on an API/HTTP error;
 * `abort` (may be NULL) cancels the transfer.
 *
 * `out_http` (may be NULL) receives the HTTP status when a response arrived and
 * 0 when none did, so the caller can say WHICH failure this was (M500). */
jc_status jc_transcribe_run(const struct jc_model_cfg *m,
                            const unsigned char *audio, jc_size len,
                            const char *filename, const char *content_type,
                            const char *language, char **out,
                            volatile int *abort, long *out_http);

#ifdef __cplusplus
}
#endif
#endif /* JC_TRANSCRIBE_H */
