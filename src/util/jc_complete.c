/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_complete.c - pure Tab-completion helpers (see jc_complete.h). */

#include "jc_complete.h"
#include "jc_utf8.h"   /* jc_ctrl_display_safe (M472) */

#include <stdlib.h>
#include <string.h>

static int csp(int c) { return c == ' ' || c == '\t'; }

const char *jc_complete_token(const char *buf, jc_size cursor, jc_size *start)
{
    jc_size s = cursor;
    if (buf == NULL) {
        *start = 0;
        return "";
    }
    while (s > 0 && !csp((unsigned char)buf[s - 1])) {
        s--;
    }
    *start = s;
    return buf + s;
}

jc_size jc_complete_common_prefix(const char *const *cands, int n,
                                  char *out, jc_size cap)
{
    jc_size len;
    int i;

    if (cap == 0) return 0;
    out[0] = '\0';
    if (cands == NULL || n <= 0 || cands[0] == NULL) return 0;

    /* Start from the first candidate, shrink to agree with the rest. */
    len = (jc_size)strlen(cands[0]);
    for (i = 1; i < n; i++) {
        jc_size j = 0;
        const char *c = cands[i];
        if (c == NULL) { len = 0; break; }
        while (j < len && c[j] != '\0' && c[j] == cands[0][j]) {
            j++;
        }
        len = j;
        if (len == 0) break;
    }
    if (len > cap - 1) len = cap - 1;
    memcpy(out, cands[0], len);
    out[len] = '\0';
    return len;
}

static int line_is_word(int c)
{
    /* ASCII alphanumerics, plus any UTF-8 byte (high bit set) so a CJK / non-Latin
     * run counts as a "word" for Alt-B/F and Ctrl-W (M127). */
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || (c & 0x80) != 0;
}

jc_size jc_line_word_left(const char *buf, jc_size len, jc_size cursor)
{
    jc_size p;
    (void)len;
    if (buf == NULL) return 0;
    p = cursor;
    while (p > 0 && !line_is_word((unsigned char)buf[p - 1])) p--;
    while (p > 0 && line_is_word((unsigned char)buf[p - 1])) p--;
    return p;
}

jc_size jc_line_word_right(const char *buf, jc_size len, jc_size cursor)
{
    jc_size p;
    if (buf == NULL) return len;
    p = cursor;
    while (p < len && !line_is_word((unsigned char)buf[p])) p++;
    while (p < len && line_is_word((unsigned char)buf[p])) p++;
    return p;
}

char *jc_paste_splice(const char *before, const char *after,
                      const char *pasted, jc_size *out_cursor)
{
    jc_size bl = (before != NULL) ? (jc_size)strlen(before) : 0;
    jc_size al = (after != NULL) ? (jc_size)strlen(after) : 0;
    jc_size pl = (pasted != NULL) ? (jc_size)strlen(pasted) : 0;
    char *out = (char *)malloc(bl + pl + al + 1); /* normalize never grows */
    jc_size o = 0;
    jc_size i;
    jc_size cursor_at;

    if (out == NULL) {
        if (out_cursor != NULL) *out_cursor = 0;
        return NULL;
    }
    if (bl > 0) {
        memcpy(out, before, bl);
        o = bl;
    }
    /* Normalize the pasted bytes: CRLF -> LF, lone CR -> LF -- and strip the
     * C0 control characters (except newline and tab) plus DEL (M363). A
     * bracketed paste delivers content verbatim, and the editor re-EMITS the
     * buffer on every redraw: a pasted ESC would replay pasted escape
     * sequences into the terminal on each keystroke (paste injection, the
     * output-side twin of the attack bracketed paste exists to stop), and a
     * pasted BEL would ring per redraw. Tab is kept -- it is real content
     * (Makefiles) and the column math accounts for it (jc_term_str_cols_from);
     * newline is the row separator. UTF-8 bytes (>= 0x80) pass untouched. */
    for (i = 0; i < pl; i++) {
        char c = pasted[i];
        unsigned char u = (unsigned char)c;
        if (c == '\r') {
            out[o++] = '\n';
            if (i + 1 < pl && pasted[i + 1] == '\n') {
                i++; /* consume the LF of a CRLF pair */
            }
        } else if (!jc_ctrl_display_safe(u)) {
            /* M472: the rule itself now lives in jc_ctrl_display_safe, so the
             * paste (input) side and the terminal-write (output) side cannot
             * drift apart. The reasoning above is unchanged; only the copy of
             * the condition is gone. */
            continue; /* stripped: ESC, BEL, NUL, ... and DEL */
        } else {
            out[o++] = c;
        }
    }
    cursor_at = o; /* just past the inserted (normalized) paste */
    if (al > 0) {
        memcpy(out + o, after, al);
        o += al;
    }
    out[o] = '\0';
    if (out_cursor != NULL) {
        *out_cursor = cursor_at;
    }
    return out;
}
