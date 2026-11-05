/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sha256.h - SHA-256 (FIPS 180-4), written from the standard (M851).
 *
 * jichi had no cryptographic hash. The project-config trust step binds a
 * user's trust to the CONTENT of the file they trusted, so a `git pull` that
 * adds an MCP server does not inherit it -- and that binding needs a hash an
 * attacker cannot collide on purpose. A non-cryptographic fingerprint (FNV,
 * the prompt-cache prefix hash) can be collided by whoever writes the next
 * commit, which is exactly the party the trust step guards against.
 *
 * C89: no <stdint.h>, so a 32-bit word is an `unsigned long` masked to 32 bits
 * after every operation that can carry above them. Pure; no allocation.
 * Unit-tested against the NIST vectors (tests/test_sha256.c). */
#ifndef JC_SHA256_H
#define JC_SHA256_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

#define JC_SHA256_DIGEST_LEN 32
#define JC_SHA256_HEX_LEN    64   /* plus a NUL: a 65-byte buffer */

struct jc_sha256 {
    unsigned long h[8];        /* the running state, each masked to 32 bits */
    unsigned char buf[64];     /* the partial block */
    unsigned long buflen;      /* bytes in buf, 0..63 */
    unsigned long bytes_lo;    /* total message length in bytes, low 32 bits */
    unsigned long bytes_hi;    /* ...and the next 32 */
};

void jc_sha256_init(struct jc_sha256 *c);
void jc_sha256_update(struct jc_sha256 *c, const void *data, jc_size len);
void jc_sha256_final(struct jc_sha256 *c,
                     unsigned char out[JC_SHA256_DIGEST_LEN]);

/* One-shot: the lower-case hex digest of `data` into `out` (at least 65 bytes,
 * NUL-terminated). */
void jc_sha256_hex(const void *data, jc_size len, char *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_SHA256_H */
