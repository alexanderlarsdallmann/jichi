/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fmtcmd.c - see jc_fmtcmd.h. Pure; no I/O, no allocation. */
#include "jc_fmtcmd.h"

#include <string.h>

int jc_shell_quote(const char *s, char *out, jc_size cap)
{
    jc_size n = 0;
    const char *p;

    if (s == NULL || out == NULL || cap == 0) {
        return -1;
    }
    /* Opening quote. */
    if (n + 1 >= cap) {
        return -1;
    }
    out[n++] = '\'';
    for (p = s; *p != '\0'; p++) {
        if (*p == '\'') {
            /* Close the quote, emit an escaped quote, reopen: '\'' */
            if (n + 4 >= cap) {
                return -1;
            }
            out[n++] = '\'';
            out[n++] = '\\';
            out[n++] = '\'';
            out[n++] = '\'';
        } else {
            if (n + 1 >= cap) {
                return -1;
            }
            out[n++] = *p;
        }
    }
    if (n + 1 >= cap) {
        return -1;
    }
    out[n++] = '\'';
    out[n] = '\0';
    return 0;
}

int jc_fmtcmd_build(const char *cmd, const char *path, char *out, jc_size cap)
{
    char q[2048];
    jc_size n = 0;
    jc_size qlen;
    const char *p;

    if (cmd == NULL || cmd[0] == '\0' || path == NULL || path[0] == '\0' ||
        out == NULL || cap == 0) {
        return -1;
    }
    if (jc_shell_quote(path, q, sizeof(q)) != 0) {
        return -1;
    }
    qlen = (jc_size)strlen(q);

    if (strstr(cmd, "{}") != NULL) {
        /* M847 (S1): the path is substituted ALREADY SINGLE-QUOTED, so it is
         * safe only OUTSIDE the author's own quotes. If the author wrote `{}`
         * inside a quoted span -- `'...style_file("{}")'`, as the R and elisp
         * packs and FORMATTING.md once did -- the path's own opening quote
         * closes the author's, and the path's content (a space, a `;`, a `$`)
         * falls out into the shell unquoted. There is no safe substitution
         * there without re-implementing the author's quoting context, so refuse
         * it and say where to move `{}`. Track single/double quote state while
         * scanning; a backslash in an unquoted or double-quoted span escapes the
         * next byte (single quotes have no escapes in sh). */
        int in_sq = 0, in_dq = 0;
        for (p = cmd; *p != '\0'; ) {
            if (p[0] == '{' && p[1] == '}') {
                if (in_sq || in_dq) {
                    return -2;   /* placeholder inside quotes: unsafe */
                }
                if (n + qlen + 1 >= cap) {
                    return -1;
                }
                memcpy(out + n, q, qlen);
                n += qlen;
                p += 2;
                continue;
            }
            if (in_sq) {
                if (*p == '\'') {
                    in_sq = 0;
                }
            } else if (in_dq) {
                if (*p == '\\' && p[1] != '\0') {
                    if (n + 1 >= cap) {
                        return -1;
                    }
                    out[n++] = *p++;
                } else if (*p == '"') {
                    in_dq = 0;
                }
            } else {
                if (*p == '\'') {
                    in_sq = 1;
                } else if (*p == '"') {
                    in_dq = 1;
                } else if (*p == '\\' && p[1] != '\0') {
                    if (n + 1 >= cap) {
                        return -1;
                    }
                    out[n++] = *p++;
                }
            }
            if (n + 1 >= cap) {
                return -1;
            }
            out[n++] = *p++;
        }
        out[n] = '\0';
        return 0;
    }

    /* No placeholder: append the quoted path as the final argument. */
    {
        jc_size clen = (jc_size)strlen(cmd);
        if (clen + 1 + qlen + 1 > cap) {
            return -1;
        }
        memcpy(out, cmd, clen);
        n = clen;
        out[n++] = ' ';
        memcpy(out + n, q, qlen);
        n += qlen;
        out[n] = '\0';
    }
    return 0;
}
