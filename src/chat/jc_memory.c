/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_memory.c - persistent agent memory (see jc_memory.h). */

#include "jc_memory.h"
#include "jc_app.h"
#include "jc_log.h"
#include "jc_str.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"
#include <stdlib.h> /* free: app->memory is malloc-owned (M199) */

#include <string.h>

jc_size jc_memory_clean_note(const char *note, char *out, jc_size cap)
{
    jc_size o = 0;
    int prev_space = 1; /* leading => skip leading whitespace */
    const char *p;

    if (out == NULL || cap == 0) {
        return 0;
    }
    if (note != NULL) {
        for (p = note; *p != '\0'; p++) {
            unsigned char ch = (unsigned char)*p;
            int is_space = (ch == ' ' || ch == '\t' || ch == '\n' ||
                            ch == '\r' || ch == '\f' || ch == '\v');
            if (is_space) {
                prev_space = 1;
                continue;
            }
            if (prev_space && o > 0 && o + 1 < cap) {
                out[o++] = ' ';
            }
            prev_space = 0;
            if (o + 1 < cap) {
                out[o++] = (char)ch;
            }
        }
    }
    out[o] = '\0';
    return o;
}

int jc_memory_has_line(const char *content, const char *note)
{
    jc_size nlen;
    const char *p;

    if (content == NULL || note == NULL) {
        return 0;
    }
    nlen = (jc_size)strlen(note);
    for (p = content; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        jc_size linelen = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (linelen == nlen + 2 && p[0] == '-' && p[1] == ' ' &&
            memcmp(p + 2, note, nlen) == 0) {
            return 1;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

/* Build <cwd>/.jichi/memory.md into buf. */
static void memory_path(struct jc_app *app, char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi/memory.md", app->cwd);
}

char *jc_memory_load(struct jc_app *app)
{
    char path[1200];
    char *data;
    jc_size len = 0;
    char *result;

    memory_path(app, path, sizeof(path));
    /* M197: scratch for the raw file; the tail we KEEP is strdup'd onto
     * app->arena below, so only the bounded note text becomes session state. */
    if (jc_read_file(path, &data, &len, jc_app_tool_scratch(app)) != JC_OK ||
        len == 0) {
        return NULL;
    }
    if (len > JC_MEMORY_MAX) {
        /* Keep the most recent notes (the tail). The file is untouched on
         * disk, but not loading part of durable memory must not be silent
         * (M143): say what is being skipped and how to fix it. */
        /* M292: this used to say "consolidate with /learn corrections" -- a
         * command that had never existed, in either the TUI or the CLI, so it
         * printed a usage error and exited 2. M292 pointed it at `learn apply`
         * instead; M294 MADE the named operation real, because retracting stale
         * notes (not adding more) is exactly what this situation calls for, so
         * the message can once again name the narrow command -- this time one
         * that runs. */
        jc_logf(JC_LOG_WARN, "memory: .jichi/memory.md is %lu KB; only the most "
                "recent %d KB is loaded into the prompt -- older notes are "
                "skipped. Prune the file, or supersede stale notes: add a "
                "'## Corrections' section to .jichi/lessons.draft.md (via "
                "/learn) and run `learn corrections`",
                (unsigned long)(len / 1024), JC_MEMORY_MAX / 1024);
        data = data + jc_utf8_resync(data, len, len - JC_MEMORY_MAX);
    }
    /* Skip leading whitespace so an all-blank file reads as empty. */
    while (*data == ' ' || *data == '\n' || *data == '\r' || *data == '\t') {
        data++;
    }
    if (data[0] == '\0') {
        return NULL;
    }
    /* M199: malloc, not the session arena. This function is called on EVERY
     * `remember` tool call and every correction, and each call used to leave a
     * full copy of the notes on app->arena (freed only at exit) -- N updates
     * retained N copies, bounded by JC_MEMORY_MAX but growing without reason.
     * The caller owns the result; jc_memory_refresh does the swap so no caller
     * has to remember to free the previous one. */
    result = jc_strdup(data);
    return result;
}

void jc_memory_refresh(struct jc_app *app)
{
    char *fresh;
    if (app == NULL) {
        return;
    }
    fresh = jc_memory_load(app);
    free(app->memory);      /* M199: exactly one live copy at a time */
    app->memory = fresh;
}

long jc_memory_file_size(struct jc_app *app)
{
    char path[1200];
    long sz;
    memory_path(app, path, sizeof(path));
    sz = jc_file_size(path);
    return sz > 0 ? sz : 0;
}

jc_status jc_memory_add(struct jc_app *app, const char *note, int *was_new)
{
    char path[1200];
    char dir[1100];
    char clean[2048];
    char *existing = NULL;
    jc_size elen = 0;
    struct jc_sb sb;
    jc_status st;

    if (was_new != NULL) {
        *was_new = 0;
    }
    if (jc_memory_clean_note(note, clean, sizeof(clean)) == 0) {
        return JC_ERR_INVALID;
    }

    memory_path(app, path, sizeof(path));
    /* Best-effort: load existing content (missing file is fine). */
    if (jc_read_file(path, &existing, &elen,
                     jc_app_tool_scratch(app)) != JC_OK) { /* M197: scratch */
        existing = NULL;
    }
    if (existing != NULL && jc_memory_has_line(existing, clean)) {
        return JC_OK; /* already remembered; was_new stays 0 */
    }

    jc_snprintf(dir, sizeof(dir), "%s/.jichi", app->cwd);
    jc_mkdir_p(dir);

    jc_sb_init(&sb);
    if (existing != NULL && elen > 0) {
        jc_sb_append_n(&sb, existing, elen);
        if (existing[elen - 1] != '\n') {
            jc_sb_append_char(&sb, '\n');
        }
    }
    jc_sb_append(&sb, "- ");
    jc_sb_append(&sb, clean);
    jc_sb_append_char(&sb, '\n');

    st = jc_write_file(path, sb.data != NULL ? sb.data : "", sb.len);
    jc_sb_free(&sb);
    if (st == JC_OK && was_new != NULL) {
        *was_new = 1;
    }
    return st;
}

/* Does the byte range [s, s+len) contain `needle` as a substring? */
static int range_contains(const char *s, jc_size len, const char *needle)
{
    jc_size nn = (jc_size)strlen(needle);
    jc_size i, j;
    if (nn == 0) {
        return 0;
    }
    if (nn > len) {
        return 0;
    }
    for (i = 0; i + nn <= len; i++) {
        for (j = 0; j < nn; j++) {
            if (s[i + j] != needle[j]) {
                break;
            }
        }
        if (j == nn) {
            return 1;
        }
    }
    return 0;
}

int jc_memory_apply_correction(const char *content, const char *match,
                               const char *replacement, struct jc_sb *out)
{
    const char *p;
    int removed = 0;
    int added = 0;
    char clean[2048];
    jc_size clen = 0;

    if (content == NULL || match == NULL || match[0] == '\0' || out == NULL) {
        return 0;
    }
    if (replacement != NULL && replacement[0] != '\0') {
        clen = jc_memory_clean_note(replacement, clean, sizeof(clean));
    } else {
        clean[0] = '\0';
    }
    for (p = content; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        jc_size linelen = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);
        int is_bullet = (linelen >= 2 && p[0] == '-' && p[1] == ' ');
        if (is_bullet && range_contains(p + 2, linelen - 2, match)) {
            removed++; /* drop this stale note */
        } else {
            jc_sb_append_n(out, p, linelen);
            if (nl != NULL) {
                jc_sb_append_char(out, '\n');
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    /* Append the corrected note (only when we actually superseded something and
     * it isn't already present). */
    if (clen > 0 && removed > 0 &&
        !jc_memory_has_line(out->data != NULL ? out->data : "", clean)) {
        if (out->len > 0 && out->data != NULL && out->data[out->len - 1] != '\n') {
            jc_sb_append_char(out, '\n');
        }
        jc_sb_append(out, "- ");
        jc_sb_append(out, clean);
        jc_sb_append_char(out, '\n');
        added = 1;
    }
    return removed + added;
}

jc_status jc_memory_correct(struct jc_app *app, const char *match,
                            const char *replacement, int *changed)
{
    char path[1200];
    char *existing = NULL;
    jc_size elen = 0;
    struct jc_sb sb;
    int n;
    jc_status st;

    if (changed != NULL) {
        *changed = 0;
    }
    if (match == NULL || match[0] == '\0') {
        return JC_ERR_INVALID;
    }
    memory_path(app, path, sizeof(path));
    if (jc_read_file(path, &existing, &elen, /* M197: scratch */
                     jc_app_tool_scratch(app)) != JC_OK || existing == NULL) {
        return JC_OK; /* no memory file: nothing to correct */
    }
    jc_sb_init(&sb);
    n = jc_memory_apply_correction(existing, match, replacement, &sb);
    if (n == 0) {
        jc_sb_free(&sb); /* match not found: leave the file untouched */
        return JC_OK;
    }
    st = jc_write_file(path, sb.data != NULL ? sb.data : "", sb.len);
    jc_sb_free(&sb);
    if (st == JC_OK) {
        jc_memory_refresh(app); /* refresh for the rest of the run */
        if (changed != NULL) {
            *changed = n;
        }
    }
    return st;
}
