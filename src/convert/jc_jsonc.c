/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_jsonc.c - strip JSONC comments and trailing commas (see jc_jsonc.h). */

#include "jc_jsonc.h"

#include <string.h>

/* Remove // and block comments from `s` into `out` (which must hold at least
 * strlen(s)+1 bytes). Only double-quoted strings are recognised, matching
 * JSON; a comment marker inside a string is preserved. A block comment is
 * replaced by one space so adjacent tokens do not merge. */
static void strip_comments(const char *s, char *out)
{
    const char *p = s;
    char *o = out;
    int in_str = 0;

    while (*p != '\0') {
        char c = *p;
        if (in_str) {
            *o++ = c;
            if (c == '\\' && p[1] != '\0') {
                p++;
                *o++ = *p;
            } else if (c == '"') {
                in_str = 0;
            }
            p++;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            *o++ = c;
            p++;
            continue;
        }
        if (c == '/' && p[1] == '/') {
            p += 2;
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue; /* keep the newline for the next iteration */
        }
        if (c == '/' && p[1] == '*') {
            p += 2;
            while (*p != '\0' && !(*p == '*' && p[1] == '/')) {
                p++;
            }
            if (*p != '\0') {
                p += 2; /* skip the closing */
            }
            *o++ = ' ';
            continue;
        }
        *o++ = c;
        p++;
    }
    *o = '\0';
}

/* Remove a comma that is followed (past whitespace) by } or ], in place. */
static void strip_trailing_commas(char *s)
{
    char *p = s;
    char *o = s;
    int in_str = 0;

    while (*p != '\0') {
        char c = *p;
        if (in_str) {
            *o++ = c;
            if (c == '\\' && p[1] != '\0') {
                p++;
                *o++ = *p;
            } else if (c == '"') {
                in_str = 0;
            }
            p++;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            *o++ = c;
            p++;
            continue;
        }
        if (c == ',') {
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') {
                q++;
            }
            if (*q == '}' || *q == ']') {
                p++; /* drop the comma */
                continue;
            }
        }
        *o++ = c;
        p++;
    }
    *o = '\0';
}

char *jc_jsonc_strip(const char *src, struct jc_arena *a)
{
    jc_size n;
    char *buf;

    if (src == NULL) {
        return NULL;
    }
    n = (jc_size)strlen(src);
    buf = (char *)jc_arena_alloc(a, n + 1);
    if (buf == NULL) {
        return NULL;
    }
    strip_comments(src, buf);
    strip_trailing_commas(buf);
    return buf;
}
