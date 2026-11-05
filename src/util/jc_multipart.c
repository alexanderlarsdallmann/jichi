/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_multipart.c - multipart/form-data body builder (see header). */

#include "jc_multipart.h"
#include "jc_uuid.h"
#include "jc_snprintf.h"

#include <string.h>

void jc_multipart_init(struct jc_multipart *mp)
{
    char uuid[40];
    jc_sb_init(&mp->body);
    jc_uuid_v4(uuid);
    jc_snprintf(mp->boundary, sizeof(mp->boundary), "jichiBoundary%s", uuid);
}

/* Append `s` as a quoted-string header parameter value, dropping the bytes that
 * would end the quoting or the header (M472).
 *
 * jc_multipart_file interpolated `filename` straight into
 * `Content-Disposition: ...; filename="<here>"`, with no escaping of '"', CR or
 * LF. A filename containing CRLF therefore injects arbitrary headers -- or an
 * entire extra part -- into the request body, and the filename reaching this
 * function is model-chosen (jc_transcribe passes the path the model asked to
 * upload).
 *
 * STRIPPING rather than backslash-escaping, deliberately: RFC 2616's quoted-pair
 * is not reliably implemented by the servers on the other end (RFC 6266 §4.3 and
 * RFC 7578 §2 both discourage relying on it), so an escaped quote is a
 * compatibility gamble where a removed one is not. A filename is a label here --
 * the bytes that matter are in the payload -- so losing a quote from it costs
 * nothing a caller can observe. Same reasoning as M363's decision to strip
 * control bytes rather than visualise them. */
static void append_quoted(struct jc_multipart *mp, const char *s)
{
    jc_size i;
    if (s == NULL) {
        return;
    }
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\r' || c == '\n' || c == '\\' || c < 32) {
            continue;
        }
        jc_sb_append_n(&mp->body, (const char *)&c, 1);
    }
}

static void part_header(struct jc_multipart *mp)
{
    jc_sb_append(&mp->body, "--");
    jc_sb_append(&mp->body, mp->boundary);
    jc_sb_append(&mp->body, "\r\n");
}

void jc_multipart_field(struct jc_multipart *mp, const char *name,
                        const char *value)
{
    part_header(mp);
    jc_sb_append(&mp->body, "Content-Disposition: form-data; name=\"");
    append_quoted(mp, name != NULL ? name : "");
    jc_sb_append(&mp->body, "\"\r\n\r\n");
    jc_sb_append(&mp->body, value != NULL ? value : "");
    jc_sb_append(&mp->body, "\r\n");
}

jc_status jc_multipart_file(struct jc_multipart *mp, const char *name,
                            const char *filename, const char *content_type,
                            const unsigned char *data, jc_size len)
{
    /* C-10 (M1121): a part's data unit must not contain the delimiter
     * "--<boundary>" (the boundary's two leading dashes + the boundary, the
     * sequence that terminates a part; RFC 7578). A file part's payload is
     * MODEL-CHOSEN bytes, so scan it for the delimiter BEFORE appending; on a
     * match regenerate the boundary (fresh jc_uuid_v4, the same
     * "jichiBoundary%s" format jc_multipart_init uses) and re-scan, capped at
     * 5 regenerations -- a fresh OS-entropy boundary will not appear in the
     * unchanged payload, so the loop converges on the first pass. The payload
     * is NEVER modified. On the common path (no regeneration) every part
     * header and the closing boundary render under the one mp->boundary, so
     * the body is built once and is self-consistent; no re-render is needed
     * (the only bytes that can introduce the delimiter are these file payload
     * bytes). If a regeneration DID occur after some field parts were already
     * rendered (only the jc_transcribe order: model, response_format, the
     * optional language, then the file part), those earlier parts keep the
     * OLD boundary and the closing boundary the NEW one -- a mixed-boundary
     * body. That is still safe and fail-closed: the server expects its first
     * delimiter to be the NEW boundary and rejects the body, so the upload
     * fails rather than leaks; and the planted part carries the old
     * boundary, not the new one, so it is not an injection. The path
     * converges on the first pass under OS entropy, so the mixed case is
     * near-impossible.
     *
     * Scope: the field path (jc_multipart_field) is deliberately NOT checked
     * -- a field VALUE is a text parameter, not a byte-exact model payload,
     * and its collision is negligible under OS entropy. And the caller reads
     * the Content-Type after finish, so the regenerated boundary is what the
     * request carries. */
    if (data != NULL && len > 0) {
        char delim[64];
        int tries;
        for (tries = 0; tries <= 5; tries++) {
            jc_size dl = (jc_size)strlen(mp->boundary);
            jc_size i;
            if (dl > sizeof(delim) - 3) {
                return JC_ERR_INVALID;   /* cannot even name the delimiter */
            }
            delim[0] = '-';
            delim[1] = '-';
            memcpy(delim + 2, mp->boundary, dl + 1);
            for (i = 0; i + 2 + dl <= len; i++) {
                if (memcmp(data + i, delim, 2 + dl) == 0) {
                    break;
                }
            }
            if (i + 2 + dl > len) {
                break;                   /* payload is clean under this boundary */
            }
            if (tries == 5) {
                return JC_ERR_INVALID;   /* impossible under OS entropy */
            }
            {
                char uuid[40];
                jc_uuid_v4(uuid);
                jc_snprintf(mp->boundary, sizeof(mp->boundary),
                            "jichiBoundary%s", uuid);
            }
        }
    }
    part_header(mp);
    jc_sb_append(&mp->body, "Content-Disposition: form-data; name=\"");
    append_quoted(mp, name != NULL ? name : "");
    jc_sb_append(&mp->body, "\"; filename=\"");
    append_quoted(mp, filename != NULL ? filename : "file");
    jc_sb_append(&mp->body, "\"\r\n");
    jc_sb_append(&mp->body, "Content-Type: ");
    append_quoted(mp, content_type != NULL ? content_type
                                           : "application/octet-stream");
    jc_sb_append(&mp->body, "\r\n\r\n");
    if (data != NULL && len > 0) {
        jc_sb_append_n(&mp->body, (const char *)data, len);
    }
    jc_sb_append(&mp->body, "\r\n");
    return JC_OK;
}

char *jc_multipart_finish(struct jc_multipart *mp, jc_size *out_len)
{
    char *body;
    jc_sb_append(&mp->body, "--");
    jc_sb_append(&mp->body, mp->boundary);
    jc_sb_append(&mp->body, "--\r\n");
    if (out_len != NULL) {
        *out_len = mp->body.len;
    }
    body = jc_sb_finish(&mp->body);
    return body;
}

void jc_multipart_content_type(const struct jc_multipart *mp, char *buf,
                               jc_size cap)
{
    jc_snprintf(buf, cap, "multipart/form-data; boundary=%s", mp->boundary);
}

void jc_multipart_free(struct jc_multipart *mp)
{
    jc_sb_free(&mp->body);
}
