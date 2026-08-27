/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fim.c - pure fill-in-the-middle prompt assembly (see jc_fim.h). */

#include "jc_fim.h"
#include "jc_str.h"

#include <stddef.h>
#include <string.h>

jc_size jc_fim_bound(jc_size len, jc_size budget, int keep_tail,
                     jc_size *out_len)
{
    if (budget == 0 || len <= budget) {
        if (out_len != NULL) {
            *out_len = len;
        }
        return 0;
    }
    if (out_len != NULL) {
        *out_len = budget;
    }
    return keep_tail ? (len - budget) : 0;
}

void jc_fim_build_user(const char *prefix, const char *suffix,
                       struct jc_sb *sb)
{
    jc_sb_append(sb, "<BEFORE>");
    jc_sb_append(sb, prefix != NULL ? prefix : "");
    jc_sb_append(sb, "</BEFORE>\n<AFTER>");
    jc_sb_append(sb, suffix != NULL ? suffix : "");
    jc_sb_append(sb, "</AFTER>");
}

void jc_fim_strip_fences(const char *in, struct jc_sb *out)
{
    const char *start;
    const char *p;
    const char *last;
    const char *end;

    if (in == NULL) {
        return;
    }
    if (!(in[0] == '`' && in[1] == '`' && in[2] == '`')) {
        jc_sb_append(out, in);  /* no leading fence: verbatim */
        return;
    }

    /* Skip the opening fence line: ```[language]\n */
    p = in + 3;
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    if (*p == '\n') {
        p++;
    }
    start = p;

    /* Find the last closing fence after the opening line. */
    last = NULL;
    p = start;
    while ((p = strstr(p, "```")) != NULL) {
        last = p;
        p += 3;
    }
    if (last == NULL) {
        jc_sb_append(out, start);  /* unterminated: drop only the opener */
        return;
    }

    /* Trim one newline immediately before the closing fence. */
    end = last;
    if (end > start && end[-1] == '\n') {
        end--;
    }
    jc_sb_append_n(out, start, (jc_size)(end - start));
}
