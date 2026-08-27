/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sessmeta.c - see jc_sessmeta.h. One forward pass, no allocation. */

#include "jc_sessmeta.h"

#include <string.h>

/* Append one decoded byte if it fits (always NUL-terminating). */
static void put_byte(char *out, jc_size cap, jc_size *n, char c)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (*n + 1 < cap) {
        out[*n] = c;
        (*n)++;
        out[*n] = '\0';
    }
}

/* Encode a code point as UTF-8 into `out`. Only used for \uXXXX. */
static void put_cp(char *out, jc_size cap, jc_size *n, unsigned long cp)
{
    if (cp < 0x80UL) {
        put_byte(out, cap, n, (char)cp);
    } else if (cp < 0x800UL) {
        put_byte(out, cap, n, (char)(0xC0UL | (cp >> 6)));
        put_byte(out, cap, n, (char)(0x80UL | (cp & 0x3FUL)));
    } else if (cp < 0x10000UL) {
        put_byte(out, cap, n, (char)(0xE0UL | (cp >> 12)));
        put_byte(out, cap, n, (char)(0x80UL | ((cp >> 6) & 0x3FUL)));
        put_byte(out, cap, n, (char)(0x80UL | (cp & 0x3FUL)));
    } else {
        put_byte(out, cap, n, '?');
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Read a JSON string starting at text[i] == '"'. Decodes into out (may be NULL
 * to skip). Returns the index just past the closing quote, or 0 on malformed
 * input (unterminated). */
static jc_size read_string(const char *text, jc_size len, jc_size i,
                           char *out, jc_size cap)
{
    jc_size n = 0;
    if (out != NULL && cap > 0) {
        out[0] = '\0';
    }
    if (i >= len || text[i] != '"') {
        return 0;
    }
    i++;
    while (i < len) {
        char c = text[i];
        if (c == '"') {
            return i + 1;
        }
        if (c == '\\') {
            i++;
            if (i >= len) {
                return 0;
            }
            switch (text[i]) {
                case '"':  put_byte(out, cap, &n, '"');  break;
                case '\\': put_byte(out, cap, &n, '\\'); break;
                case '/':  put_byte(out, cap, &n, '/');  break;
                case 'n':  put_byte(out, cap, &n, '\n'); break;
                case 'r':  put_byte(out, cap, &n, '\r'); break;
                case 't':  put_byte(out, cap, &n, '\t'); break;
                case 'b':  put_byte(out, cap, &n, '\b'); break;
                case 'f':  put_byte(out, cap, &n, '\f'); break;
                case 'u': {
                    unsigned long cp = 0;
                    int k;
                    if (i + 4 >= len) {
                        return 0;
                    }
                    for (k = 1; k <= 4; k++) {
                        int h = hexval(text[i + k]);
                        if (h < 0) {
                            return 0;
                        }
                        cp = (cp << 4) | (unsigned long)h;
                    }
                    i += 4;
                    put_cp(out, cap, &n, cp);
                    break;
                }
                default:
                    /* Unknown escape: JSON forbids it; be strict so the caller
                     * falls back rather than silently mangling a value. */
                    return 0;
            }
            i++;
            continue;
        }
        put_byte(out, cap, &n, c);
        i++;
    }
    return 0; /* unterminated */
}

/* Skip whitespace. */
static jc_size skip_ws(const char *text, jc_size len, jc_size i)
{
    while (i < len && (text[i] == ' ' || text[i] == '\t' ||
                       text[i] == '\n' || text[i] == '\r')) {
        i++;
    }
    return i;
}

int jc_sessmeta_scan(const char *text, jc_size len, struct jc_sessmeta *out)
{
    jc_size i;
    int depth = 0;          /* nesting of { and [ combined                   */
    int hist_depth = -1;    /* the [ depth of the history array, once entered */
    int expect_key = 0;     /* at depth 1 inside the root object             */

    if (text == NULL || out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));

    i = skip_ws(text, len, 0);
    if (i >= len || text[i] != '{') {
        return 0; /* not a top-level object */
    }
    depth = 1;
    i++;
    expect_key = 1;

    while (i < len) {
        char c;
        i = skip_ws(text, len, i);
        if (i >= len) {
            break;
        }
        c = text[i];

        if (c == '}' || c == ']') {
            if (hist_depth >= 0 && depth == hist_depth && c == ']') {
                hist_depth = -1; /* left the history array */
            }
            depth--;
            i++;
            if (depth == 0) {
                /* End of the root object: trustworthy only if we saw history.
                 * A file without it is not one of ours -- fall back. */
                return out->has_history ? 1 : 0;
            }
            expect_key = (depth == 1);
            continue;
        }
        if (c == ',' || c == ':') {
            i++;
            expect_key = (depth == 1 && c == ',');
            continue;
        }
        if (c == '{') {
            /* An object directly inside the history array is one message. This
             * is the count, and it is why string state is tracked: a message
             * whose content contains `"role":` must not inflate it. */
            if (hist_depth >= 0 && depth == hist_depth) {
                out->nmsgs++;
            }
            depth++;
            i++;
            expect_key = 0;
            continue;
        }
        if (c == '[') {
            depth++;
            i++;
            expect_key = 0;
            continue;
        }
        if (c == '"') {
            if (expect_key && depth == 1) {
                char key[32];
                jc_size after = read_string(text, len, i, key, sizeof(key));
                if (after == 0) {
                    return 0;
                }
                i = skip_ws(text, len, after);
                if (i >= len || text[i] != ':') {
                    return 0;
                }
                i = skip_ws(text, len, i + 1);
                if (i >= len) {
                    return 0;
                }
                if (text[i] == '"') {
                    char *dst = NULL;
                    jc_size cap = 0;
                    int *flag = NULL;
                    if (strcmp(key, "sessionId") == 0) {
                        dst = out->id; cap = sizeof(out->id); flag = &out->has_id;
                    } else if (strcmp(key, "title") == 0) {
                        dst = out->title; cap = sizeof(out->title);
                        flag = &out->has_title;
                    } else if (strcmp(key, "alias") == 0) {
                        dst = out->alias; cap = sizeof(out->alias);
                        flag = &out->has_alias;
                    } else if (strcmp(key, "workspaceDirectory") == 0) {
                        dst = out->workspace; cap = sizeof(out->workspace);
                        flag = &out->has_workspace;
                    }
                    after = read_string(text, len, i, dst, cap);
                    if (after == 0) {
                        return 0;
                    }
                    if (flag != NULL) {
                        *flag = 1;
                    }
                    i = after;
                    expect_key = 0;
                    continue;
                }
                if (strcmp(key, "history") == 0 && text[i] == '[') {
                    out->has_history = 1;
                    depth++;
                    hist_depth = depth; /* messages are objects at this depth */
                    i++;
                    expect_key = 0;
                    continue;
                }
                /* Some other key with a non-string value: fall through and let
                 * the main loop walk it. */
                expect_key = 0;
                continue;
            }
            /* A string in any other position (a value, or nested): skip it
             * wholesale so its contents can never be mistaken for structure. */
            {
                jc_size after = read_string(text, len, i, NULL, 0);
                if (after == 0) {
                    return 0;
                }
                i = after;
            }
            expect_key = 0;
            continue;
        }
        /* A number, true/false/null, or stray byte: step over it. */
        i++;
        expect_key = 0;
    }
    return 0; /* ran out of input before the root object closed */
}
