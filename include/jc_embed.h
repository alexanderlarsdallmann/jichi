/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_embed.h - OpenAI-compatible embeddings client.
 *
 * Posts text to {api_base}/embeddings and returns dense float32 vectors. The
 * request side requires libcurl (compiled under src/net, HAVE_CURL-gated); the
 * pure response parser is always available so it can be unit-tested offline.
 */
#ifndef JC_EMBED_H
#define JC_EMBED_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_config.h"

/* Embed `n` texts with `m` (which must name an embeddings-capable model and
 * endpoint). On success returns JC_OK, sets *out_dim to the vector dimension,
 * and hands back a malloc'd contiguous array of n*(*out_dim) floats via
 * *out_vecs (row i is texts[i]); caller frees with free(). Requests are batched
 * internally. `abort` (may be NULL) cancels in-flight transfers. */
jc_status jc_embed_texts(const struct jc_model_cfg *m,
                         const char *const *texts, int n,
                         float **out_vecs, int *out_dim,
                         volatile int *abort);

/* Parse an OpenAI embeddings response body. Expects exactly `expected`
 * vectors; orders them by their "index" field. On success sets *out_dim and
 * returns a malloc'd array of expected*(*out_dim) floats via *out (caller
 * frees). Returns JC_ERR_PARSE on malformed/short input. Network-free. */
jc_status jc_embed_parse(const char *json, int expected,
                         float **out, int *out_dim);

#ifdef __cplusplus
}
#endif
#endif /* JC_EMBED_H */
