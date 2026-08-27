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
        for (p = cmd; *p != '\0'; ) {
            if (p[0] == '{' && p[1] == '}') {
                if (n + qlen + 1 >= cap) {
                    return -1;
                }
                memcpy(out + n, q, qlen);
                n += qlen;
                p += 2;
            } else {
                if (n + 1 >= cap) {
                    return -1;
                }
                out[n++] = *p++;
            }
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
