/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_checkinred.c - the C89 port of `sed -E 's/[A-Za-z0-9_-]{40,}/<r>/g'` (M1224). */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <string.h>
#include <stddef.h>
#include "jc_hp_checkinred.h"

/* A character class helper: returns 1 if c is in [A-Za-z0-9_-]. */
static int
checkinred_class(int c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

/* The redactor: ONE left-to-right pass over in; runs of >=40 chars from
 * the class become <r>; runs <40 pass through unchanged; non-class chars
 * split runs; <r> is never re-scanned. */
int
jc_checkinred(const char *in, char *out, size_t cap)
{
    size_t out_pos = 0;
    const char *p;
    size_t run_len;

    if (in == NULL) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    p = in;
    while (*p != '\0') {
        /* Start a new run: scan forward while class chars. */
        run_len = 0;
        while (p[run_len] != '\0' && checkinred_class((unsigned char)p[run_len])) {
            run_len++;
        }

        if (run_len == 0) {            /* a non-class char: copy it and advance */
            if (out_pos + 1 > cap) {
                return -1;
            }
            out[out_pos++] = *p++;
            continue;
        }
        if (run_len >= 40) {
            /* Redact: replace with <r> */
            if (out_pos + 3 > cap) {
                return -1;
            }
            out[out_pos++] = '<';
            out[out_pos++] = 'r';
            out[out_pos++] = '>';
            p += run_len;
        } else {
            /* Copy verbatim (run_len < 40) */
            size_t i;
            if (out_pos + run_len > cap) {
                return -1;
            }
            for (i = 0; i < run_len; i++) {
                out[out_pos++] = p[i];
            }
            p += run_len;
        }
    }

    /* NUL-terminate */
    if (out_pos + 1 > cap) {
        return -1;
    }
    out[out_pos] = '\0';
    return 0;
}
