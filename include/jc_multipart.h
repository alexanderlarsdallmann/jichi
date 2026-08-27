/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_multipart.h - build a multipart/form-data request body (M33).
 *
 * The HTTP layer (jc_http) takes an opaque body blob + a Content-Type header,
 * so a multipart upload is just a body the caller assembles here. Pure (no
 * libcurl), binary-safe (file parts carry an explicit length), and
 * unit-tested. Used by the audio-transcription client.
 */
#ifndef JC_MULTIPART_H
#define JC_MULTIPART_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

struct jc_multipart {
    struct jc_sb body;
    char         boundary[48];
};

/* Start a multipart body with a fresh (collision-resistant) boundary. */
void jc_multipart_init(struct jc_multipart *mp);

/* Append a simple text field: name="<name>" with `value`. */
void jc_multipart_field(struct jc_multipart *mp, const char *name,
                        const char *value);

/* Append a file field: name="<name>"; filename="<filename>" with the given
 * Content-Type and `len` bytes of `data` (binary-safe; may contain NULs). */
void jc_multipart_file(struct jc_multipart *mp, const char *name,
                       const char *filename, const char *content_type,
                       const unsigned char *data, jc_size len);

/* Finish the body (appends the closing boundary) and detach it: returns the
 * malloc'd bytes (caller frees) and writes the length to *out_len. The builder
 * is reset to empty afterwards. */
char *jc_multipart_finish(struct jc_multipart *mp, jc_size *out_len);

/* Write the "multipart/form-data; boundary=<boundary>" Content-Type value
 * (sans the "Content-Type: " prefix) into `buf`. */
void jc_multipart_content_type(const struct jc_multipart *mp, char *buf,
                               jc_size cap);

/* Release the builder's buffer (safe after finish, which already detached it). */
void jc_multipart_free(struct jc_multipart *mp);

#ifdef __cplusplus
}
#endif
#endif /* JC_MULTIPART_H */
