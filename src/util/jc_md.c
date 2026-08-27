/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_md.c - markdown frontmatter splitter (see jc_md.h). */

#include "jc_md.h"

#include <stdlib.h>
#include <string.h>

/* True if the line [s, e) equals "---" after trimming a trailing '\r'. */
static int is_fence(const char *s, const char *e)
{
    if (e > s && e[-1] == '\r') {
        e--;
    }
    return (e - s == 3) && s[0] == '-' && s[1] == '-' && s[2] == '-';
}

jc_status jc_md_parse(const char *text, struct jc_arena *a,
                      struct jc_md_doc *out)
{
    const char *first_nl;
    const char *line;
    const char *fm_start;

    out->front = NULL;
    out->body = text;
    if (text == NULL) {
        return JC_OK;
    }

    /* Must open with a "---" line. */
    first_nl = strchr(text, '\n');
    if (first_nl == NULL || !is_fence(text, first_nl)) {
        return JC_OK;
    }
    fm_start = first_nl + 1;

    /* Find the closing "---" line. */
    line = fm_start;
    for (;;) {
        const char *nl = strchr(line, '\n');
        const char *end = (nl != NULL) ? nl : (line + strlen(line));
        if (is_fence(line, end)) {
            /* Frontmatter is [fm_start, line); body follows the closing line. */
            jc_size n = (jc_size)(line - fm_start);
            char *buf = (char *)jc_arena_alloc(a, n + 1);
            if (buf != NULL) {
                memcpy(buf, fm_start, n);
                buf[n] = '\0';
                out->front = jc_yaml_parse(buf, a);
            }
            out->body = (nl != NULL) ? nl + 1 : end;
            return JC_OK;
        }
        if (nl == NULL) {
            return JC_OK; /* unterminated fence: treat as no frontmatter */
        }
        line = nl + 1;
    }
}

/* Line [s,e) is a fence after trimming trailing whitespace (space/tab/CR). */
static int is_fence_loose(const char *s, const char *e)
{
    while (e > s && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) {
        e--;
    }
    return (e - s == 3) && s[0] == '-' && s[1] == '-' && s[2] == '-';
}

int jc_md_frontmatter_unterminated(const char *text)
{
    const char *line;

    if (text == NULL) {
        return 0;
    }
    /* Skip leading blank lines (whitespace-only), landing on the first content
     * line -- which must be an opening fence for frontmatter to be intended. */
    line = text;
    for (;;) {
        const char *nl = strchr(line, '\n');
        const char *end = (nl != NULL) ? nl : (line + strlen(line));
        const char *p = line;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
            p++;
        }
        if (p != end) {
            break; /* first non-blank line */
        }
        if (nl == NULL) {
            return 0; /* only blank lines: no frontmatter intended */
        }
        line = nl + 1;
    }
    /* The first content line must be an opening fence. */
    {
        const char *nl = strchr(line, '\n');
        const char *end = (nl != NULL) ? nl : (line + strlen(line));
        if (!is_fence_loose(line, end)) {
            return 0;
        }
        if (nl == NULL) {
            return 1; /* "---" is the only line: opened, never closed */
        }
        line = nl + 1;
    }
    /* Scan for a closing fence. */
    for (;;) {
        const char *nl = strchr(line, '\n');
        const char *end = (nl != NULL) ? nl : (line + strlen(line));
        if (is_fence_loose(line, end)) {
            return 0; /* closed */
        }
        if (nl == NULL) {
            return 1; /* reached EOF without a closing fence */
        }
        line = nl + 1;
    }
}

void jc_md_free(struct jc_md_doc *doc)
{
    if (doc != NULL && doc->front != NULL) {
        jc_yaml_free(doc->front);
        doc->front = NULL;
    }
}
