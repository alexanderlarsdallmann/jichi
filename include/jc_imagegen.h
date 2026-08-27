/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_imagegen.h - OpenAI-compatible image-generation client (M32).
 *
 * Posts a prompt to {api_base}/v1/images/generations and returns the generated
 * image bytes (decoded from data[0].b64_json, or fetched from data[0].url). The
 * request side requires libcurl (compiled under src/net, HAVE_CURL-gated); the
 * pure body builder + response parser are always available for offline tests.
 */
#ifndef JC_IMAGEGEN_H
#define JC_IMAGEGEN_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_config.h"

/* Build the request body: {"model","prompt","n":1,["size"],
 * "response_format":"b64_json",["output_format"]}. `size`/`out_format` may be
 * NULL (omitted). Returns a malloc'd JSON string (caller frees) or NULL on OOM.
 * Pure. */
char *jc_imagegen_build_body(const char *model, const char *prompt,
                             const char *size, const char *out_format);

/* As jc_imagegen_build_body, plus an optional "ref_images" array of source
 * references (data: URIs or URLs) for editing models (e.g. FLUX Kontext): the
 * array is emitted only when `n_ref > 0`, so with `ref_images`==NULL/`n_ref`==0
 * the body is byte-identical to jc_imagegen_build_body. Pure (M-imageedit). */
char *jc_imagegen_build_body_ex(const char *model, const char *prompt,
                                const char *size, const char *out_format,
                                const char *const *ref_images, int n_ref);

/* Parse an images/generations response. On success: if data[0] has a
 * "b64_json", decodes it into *out_bytes (malloc'd, caller frees) + *out_len
 * and leaves *out_url NULL; otherwise if data[0] has a "url", leaves
 * *out_bytes NULL and sets *out_url (malloc'd, caller frees) for the caller to
 * fetch. Returns JC_ERR_PARSE on malformed/empty data, JC_ERR_OOM on
 * allocation failure. Network-free. */
jc_status jc_imagegen_parse(const char *json, unsigned char **out_bytes,
                            jc_size *out_len, char **out_url);

/* POST a generation request via `m` (its api_base/api_key/model) and return the
 * image bytes via *out_bytes (malloc'd, caller frees) + *out_len. `ref_images`
 * (NULL/`n_ref`==0 for plain text-to-image) carries source references for an
 * editing model. On a url-only response, fetch it with a GET when `fetch_url`
 * is nonzero. JC_ERR_PROVIDER on an API/HTTP error; `abort` (may be NULL)
 * cancels in-flight transfers. */
jc_status jc_imagegen_run(const struct jc_model_cfg *m, const char *prompt,
                          const char *size, const char *out_format,
                          const char *const *ref_images, int n_ref,
                          int fetch_url, unsigned char **out_bytes,
                          jc_size *out_len, volatile int *abort);

#ifdef __cplusplus
}
#endif
#endif /* JC_IMAGEGEN_H */
