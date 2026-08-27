/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_base64.c - standard base64 encoding (see jc_base64.h). */

#include "jc_base64.h"

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

jc_size jc_base64_encoded_len(jc_size n)
{
    return ((n + 2) / 3) * 4;
}

jc_status jc_base64_encode(const unsigned char *in, jc_size n, char *out,
                           jc_size cap)
{
    jc_size need = jc_base64_encoded_len(n);
    jc_size i = 0;
    jc_size o = 0;

    if (out == NULL || cap == 0) {
        return JC_ERR_TOOBIG;
    }
    if (need + 1 > cap) {
        out[0] = '\0';
        return JC_ERR_TOOBIG;
    }
    /* Full 3-byte groups -> 4 chars. */
    while (i + 3 <= n) {
        unsigned long v = ((unsigned long)in[i] << 16) |
                          ((unsigned long)in[i + 1] << 8) |
                          (unsigned long)in[i + 2];
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = B64[(v >> 6) & 0x3F];
        out[o++] = B64[v & 0x3F];
        i += 3;
    }
    /* Trailing 1 or 2 bytes -> 2 or 3 chars + '=' padding. */
    if (n - i == 1) {
        unsigned long v = (unsigned long)in[i] << 16;
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (n - i == 2) {
        unsigned long v = ((unsigned long)in[i] << 16) |
                          ((unsigned long)in[i + 1] << 8);
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = B64[(v >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
    return JC_OK;
}

/* Map a base64 character to its 6-bit value, or -1 if not in the alphabet. */
static int b64_val(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

jc_size jc_base64_decoded_len(jc_size enc_len)
{
    return (enc_len / 4 + 1) * 3;
}

jc_status jc_base64_decode(const char *in, unsigned char *out, jc_size cap,
                           jc_size *out_len)
{
    unsigned long acc = 0;
    int nsyms = 0;            /* 6-bit symbols accumulated in `acc` (0..4) */
    jc_size o = 0;
    const unsigned char *p;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (in == NULL) {
        return JC_ERR_INVALID;
    }
    for (p = (const unsigned char *)in; *p != '\0'; p++) {
        int c = (int)*p;
        int v;
        if (c == '=') {
            break;           /* padding marks the end of the data */
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;        /* tolerate line-wrapped base64 */
        }
        v = b64_val(c);
        if (v < 0) {
            return JC_ERR_INVALID;
        }
        acc = (acc << 6) | (unsigned long)v;
        nsyms++;
        if (nsyms == 4) {
            if (o + 3 > cap) {
                return JC_ERR_TOOBIG;
            }
            out[o++] = (unsigned char)((acc >> 16) & 0xFF);
            out[o++] = (unsigned char)((acc >> 8) & 0xFF);
            out[o++] = (unsigned char)(acc & 0xFF);
            acc = 0;
            nsyms = 0;
        }
    }
    /* A trailing partial group: 2 symbols => 1 byte, 3 => 2 bytes. A lone
     * symbol (6 bits) cannot form a byte and is malformed. */
    if (nsyms == 1) {
        return JC_ERR_INVALID;
    }
    if (nsyms == 2) {
        if (o + 1 > cap) {
            return JC_ERR_TOOBIG;
        }
        out[o++] = (unsigned char)((acc >> 4) & 0xFF);
    } else if (nsyms == 3) {
        if (o + 2 > cap) {
            return JC_ERR_TOOBIG;
        }
        out[o++] = (unsigned char)((acc >> 10) & 0xFF);
        out[o++] = (unsigned char)((acc >> 2) & 0xFF);
    }
    if (out_len != NULL) {
        *out_len = o;
    }
    return JC_OK;
}
