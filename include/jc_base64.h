/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_base64.h - standard base64 encoding (RFC 4648), for image data URIs and
 * the provider image content blocks (M29).
 *
 * Pure and unit-tested. Uses the standard alphabet ('+' / '/') with '=' padding,
 * which is what data: URIs and the Anthropic/OpenAI image blocks expect.
 */
#ifndef JC_BASE64_H
#define JC_BASE64_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Exact number of base64 characters produced for `n` input bytes (excluding the
 * NUL terminator): 4 * ceil(n / 3). */
jc_size jc_base64_encoded_len(jc_size n);

/* Encode `n` bytes of `in` into `out` (capacity `cap`), NUL-terminated. `in` may
 * be NULL only when `n` is 0. Returns JC_OK when it fit (needs
 * jc_base64_encoded_len(n) + 1 bytes), else JC_ERR_TOOBIG (and `out`, if cap>0,
 * is set to an empty string). */
jc_status jc_base64_encode(const unsigned char *in, jc_size n, char *out,
                           jc_size cap);

/* Safe upper bound on the byte count produced by decoding a base64 string of
 * `enc_len` characters (whitespace and '=' padding only reduce it). Use this to
 * size the output buffer for jc_base64_decode. */
jc_size jc_base64_decoded_len(jc_size enc_len);

/* Decode the NUL-terminated base64 string `in` into `out` (capacity `cap`),
 * writing the decoded byte count to *out_len (may be NULL). The standard
 * alphabet ('+' / '/') is accepted; ASCII whitespace is skipped (servers may
 * line-wrap), and decoding stops at the first '=' padding. Binary-safe (the
 * output may contain NUL bytes, hence *out_len). Returns JC_OK; JC_ERR_TOOBIG
 * if the decoded data does not fit `cap`; JC_ERR_INVALID on a non-alphabet
 * character or a truncated final group. */
jc_status jc_base64_decode(const char *in, unsigned char *out, jc_size cap,
                           jc_size *out_len);

#ifdef __cplusplus
}
#endif
#endif /* JC_BASE64_H */
