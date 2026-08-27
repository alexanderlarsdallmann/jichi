/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lineno.c - line-number gutter formatting (see jc_lineno.h). */

#include "jc_lineno.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>

int jc_format_numbered(const char *text, int start_line, int max_lines,
                       struct jc_sb *out, int *total_lines)
{
    const char *p;
    int lineno = 1;
    int emitted = 0;
    char gutter[16];

    if (total_lines != NULL) {
        *total_lines = 0;
    }
    if (text == NULL) {
        return 0;
    }
    if (start_line < 1) {
        start_line = 1;
    }
    p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size linelen = (nl != NULL) ? (jc_size)(nl - p)
                                       : (jc_size)strlen(p);
        int want = (lineno >= start_line) &&
                   (max_lines <= 0 || emitted < max_lines);
        if (want) {
            jc_snprintf(gutter, sizeof(gutter), "%6d\t", lineno);
            jc_sb_append(out, gutter);
            jc_sb_append_n(out, p, linelen);
            jc_sb_append_char(out, '\n');
            emitted++;
        }
        lineno++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
        if (*p == '\0') {
            break; /* trailing newline: no phantom final line */
        }
    }
    if (total_lines != NULL) {
        *total_lines = lineno - 1;
    }
    return emitted;
}

int jc_count_lines(const char *text, jc_size len)
{
    jc_size i;
    int lines = 0;
    int in_line = 0;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (text[i] == '\0') {
            break; /* the formatter stops here too */
        }
        if (text[i] == '\n') {
            lines++;
            in_line = 0;
        } else {
            in_line = 1;
        }
    }
    /* A final segment with no newline is a line; a trailing newline is not a
     * new empty one. Same rule as jc_format_numbered. */
    return lines + (in_line ? 1 : 0);
}

jc_size jc_escape_ctrl(const char *name, char *out, jc_size cap)
{
    static const char HEX[] = "0123456789abcdef";
    jc_size n = 0;
    jc_size i;
    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (name == NULL) {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        char esc[5];
        jc_size elen;
        if (c == '\n')      { esc[0] = '\\'; esc[1] = 'n'; elen = 2; }
        else if (c == '\r') { esc[0] = '\\'; esc[1] = 'r'; elen = 2; }
        else if (c == '\t') { esc[0] = '\\'; esc[1] = 't'; elen = 2; }
        else if (c == '\\') { esc[0] = '\\'; esc[1] = '\\'; elen = 2; }
        else if (c < 0x20 || c == 0x7f) {
            esc[0] = '\\'; esc[1] = 'x';
            esc[2] = HEX[(c >> 4) & 0xf];
            esc[3] = HEX[c & 0xf];
            elen = 4;
        } else {
            esc[0] = (char)c; elen = 1;
        }
        if (n + elen + 1 > cap) {
            break; /* no room: truncate rather than emit a partial escape */
        }
        {
            jc_size k;
            for (k = 0; k < elen; k++) {
                out[n++] = esc[k];
            }
        }
    }
    out[n] = '\0';
    return n;
}
