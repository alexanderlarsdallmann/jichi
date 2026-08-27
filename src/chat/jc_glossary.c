/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_glossary.c - glossary loading (see jc_glossary.h). */

#include "jc_glossary.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"

#include <string.h>

/* Append the file at `path` (if present and non-empty) to `sb`, separated from
 * any prior content by a blank line. Returns 1 if anything was appended. */
static int append_file(struct jc_app *app, const char *path, struct jc_sb *sb)
{
    char *data = NULL;
    jc_size len = 0;

    if (jc_read_file(path, &data, &len, app->arena) != JC_OK || len == 0) {
        return 0;
    }
    if (sb->len > 0) {
        jc_sb_append(sb, "\n\n");
    }
    jc_sb_append_n(sb, data, len);
    return 1;
}

char *jc_glossary_load(struct jc_app *app)
{
    char path[1200];
    struct jc_sb sb;
    char *result = NULL;
    const char *text;

    jc_sb_init(&sb);

    /* Global first (house-wide terms), then project (which may override/extend
     * in prose). */
    jc_snprintf(path, sizeof(path), "%s/.config/jichi/glossary.md",
                jc_home_dir());
    append_file(app, path, &sb);
    jc_snprintf(path, sizeof(path), "%s/.jichi/glossary.md", app->cwd);
    append_file(app, path, &sb);

    text = (sb.data != NULL) ? sb.data : "";
    /* Skip leading whitespace so an all-blank file reads as empty. */
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') {
        text++;
    }
    if (text[0] != '\0') {
        jc_size len = (jc_size)strlen(text);
        if (len > JC_GLOSSARY_MAX) {
            /* Keep the tail, starting on a character boundary (M191). */
            text = text + jc_utf8_resync(text, len, len - JC_GLOSSARY_MAX);
        }
        result = jc_arena_strdup(app->arena, text);
    }
    jc_sb_free(&sb);
    return result;
}
