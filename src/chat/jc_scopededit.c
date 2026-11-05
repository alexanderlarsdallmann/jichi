/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_scopededit.c - the D6 scoped edit check (M975).
 *
 * See jc_scopededit.h for the API and design notes.
 */

#include "jc_scopededit.h"
#include "jc_app.h"
#include "jc_mem.h"    /* for arena functions */
#include "jc_platform.h" /* for jc_read_file */
#include "jc_snprintf.h" /* for jc_snprintf */
#include "jc_str.h"
#include "jc_log.h"
#include "jc_eventlog.h"
#include "jc_jsonc.h"
#include "jc_path.h"   /* for jc_path_normalize */
#include <stdlib.h>
#include <string.h>

/* Grow the entries array when needed. */
static int scopededit_grow(struct jc_scopededit *se, struct jc_arena *a)
{
    int cap;
    struct jc_scopededit_entry *new_entries;
    cap = (se->capacity == 0) ? 8 : se->capacity * 2;
    new_entries = (struct jc_scopededit_entry *)jc_arena_alloc(a, cap * sizeof(struct jc_scopededit_entry));
    if (!new_entries)
        return -1;
    if (se->entries && se->count > 0) {
        memcpy(new_entries, se->entries, se->count * sizeof(struct jc_scopededit_entry));
    }
    se->entries = new_entries;
    se->capacity = cap;
    return 0;
}

/* Normalize a path to canonical form using jc_path_normalize against app->root.
 * Returns the normalized path (stack buffer), or falls back to raw path on
 * normalization failure. The canonical form is root-relative with no '.', no
 * '..', and no redundant slashes. We use a 512-byte buffer which is sufficient
 * for typical paths; on normalization failure (e.g., ".." segment in path),
 * we fall back to the raw string because the dedup benefit is worth less than
 * the risk of silently missing a match. */
static const char *normalize_path(const char *cwd, const char *root, const char *path,
                                  char *buf, size_t cap)
{
    char norm[512];
    jc_status st;
    size_t len;
    size_t rlen;
    size_t rlen2;
    const char *rel;
    /* Declarations at top (C89) */

    /* Try normalization first. jc_path_normalize refuses ".." segments and
     * returns JC_ERR_INVALID; on any failure we fall back to raw path. */
    st = jc_path_normalize(cwd, path, norm, sizeof(norm));
    if (st != JC_OK) {
        /* Fall back to raw path on failure. This keeps the check working for
         * unusual paths at the cost of potential double-matching (e.g., "a/./b"
         * vs "a/b" would both match but be stored differently). */
        len = strlen(path);
        if (len >= cap) len = cap - 1;
        memcpy(buf, path, len);
        buf[len] = '\0';
        return buf;
    }

    /* Normalize norm against root: if path is absolute, join it with root
     * first; if relative, it's already relative to cwd. Then strip the root
     * prefix to get root-relative form for comparison. */
    if (norm[0] == '/') {
        /* Absolute path: strip root prefix to get root-relative */
        rlen = strlen(root);
        while (rlen > 1 && root[rlen - 1] == '/') rlen--;
        if (strncmp(norm, root, rlen) == 0) {
            rel = norm + rlen;
            if (*rel == '/') rel++;
            /* Copy relative portion */
            rlen2 = strlen(rel);
            if (rlen2 >= cap) rlen2 = cap - 1;
            memcpy(buf, rel, rlen2);
            buf[rlen2] = '\0';
            return buf;
        } else {
            /* Absolute path not under root: use raw path (should not happen
             * in normal operation, but keep check working) */
            len = strlen(path);
            if (len >= cap) len = cap - 1;
            memcpy(buf, path, len);
            buf[len] = '\0';
            return buf;
        }
    } else {
        /* Relative path (already normalized relative to cwd) */
        len = strlen(norm);
        if (len >= cap) len = cap - 1;
        memcpy(buf, norm, len);
        buf[len] = '\0';
        return buf;
    }
}

/* Find an existing entry by path, or -1 if not found.
 * Both the stored entry path and the lookup path are normalized before comparison
 * to handle different path spellings (e.g., "reg.txt" vs "./reg.txt"). */
static int scopededit_find(struct jc_scopededit *se, const char *path,
                           const char *cwd, const char *root)
{
    char norm_path[512];
    int i;
    const char *p = normalize_path(cwd, root, path, norm_path, sizeof(norm_path));

    for (i = 0; i < se->count; i++) {
        if (strcmp(se->entries[i].path, p) == 0)
            return i;
    }
    return -1;
}

void jc_scopededit_turn_start(struct jc_app *app)
{
    struct jc_scopededit *se;
    /* Only at top level and only if N > 0 */
    if (app->agent_depth != 0)
        return;
    if (app->config.scoped_edit_max <= 0)
        return;

    /* Reset on arena (scratch) */
    if (app->scratch == NULL)
        return;

    /* F1: Every turn must allocate fresh; discard any previous struct/entries.
     * The scratch arena is reset between turns, so the old block may be freed
     * and reused. Reusing it causes multi-turn memory corruption. */
    se = (struct jc_scopededit *)jc_arena_alloc(app->scratch, sizeof(struct jc_scopededit));
    if (!se)
        return;
    se->n = app->config.scoped_edit_max;
    se->count = 0;
    se->capacity = 0;
    se->entries = NULL;
    app->scopededit = se;
}

void jc_scopededit_owner(struct jc_app *app, const char *path, int task,
                         const char *content, size_t len)
{
    int idx;
    size_t path_len;
    char norm_path[512];
    struct jc_scopededit_entry *e;
    const char *np;
    /* Only at top level */
    if (app->agent_depth != 0)
        return;
    if (app->scopededit == NULL || app->scopededit->n <= 0)
        return;

    /* Normalize the path to canonical root-relative form before storing */
    np = normalize_path(app->cwd, app->root, path, norm_path, sizeof(norm_path));
    path_len = strlen(np);

    /* Check if already owned (first owner wins) */
    idx = scopededit_find(app->scopededit, np, app->cwd, app->root);
    if (idx >= 0)
        return; /* Already owned, do not duplicate */

    /* Ensure capacity */
    if (app->scopededit->count >= app->scopededit->capacity) {
        if (scopededit_grow(app->scopededit, app->scratch) < 0)
            return;
    }

    /* Create new entry with arena-owned copies */
    e = &app->scopededit->entries[app->scopededit->count];
    e->path = (char *)jc_arena_alloc(app->scratch, path_len + 1);
    if (!e->path)
        return;
    memcpy(e->path, np, path_len + 1);

    e->task = task;
    e->edit_count = 0;

    e->snapshot = (char *)jc_arena_alloc(app->scratch, len);
    if (!e->snapshot)
        return;
    memcpy(e->snapshot, content, len);
    e->snapshot_len = len;

    app->scopededit->count++;
}

int jc_scopededit_path(struct jc_app *app, const char *path, int *task_out)
{
    int idx;
    if (app->scopededit == NULL || app->scopededit->n <= 0)
        return 0;

    idx = scopededit_find(app->scopededit, path, app->cwd, app->root);
    if (idx < 0)
        return 0;

    *task_out = app->scopededit->entries[idx].task;
    return 1;
}

const char *jc_scopededit_pre_write(struct jc_app *app, const char *path)
{
    int task;
    int idx;
    char norm_path[512];
    struct jc_scopededit_entry *e;
    const char *np;
    struct jc_sb msg;
    const char *result;
    cJSON *o;
    /* Self-gates: only at top level, only if N > 0 */
    if (app->agent_depth != 0)
        return NULL;
    if (app->scopededit == NULL || app->scopededit->n <= 0)
        return NULL;

    /* Normalize path for lookup */
    np = normalize_path(app->cwd, app->root, path, norm_path, sizeof(norm_path));

    if (!jc_scopededit_path(app, np, &task))
        return NULL; /* Not owned, allow write */

    /* Trip: allow first (N-1) edits, refuse from the Nth */
    idx = scopededit_find(app->scopededit, np, app->cwd, app->root);
    if (idx >= 0)
        e = &app->scopededit->entries[idx];
    else
        e = NULL;

    if (e && e->edit_count >= app->scopededit->n - 1) {
        /* Trip: the Nth or later edit is refused */
        /* F5a: make the message tool-agnostic (no hard-coded tool name). */
        jc_sb_init(&msg);
        jc_sb_append_fmt(&msg,
            "'%s' refused: worker-owned path (task %d) limit reached; "
            "a follow-up task for that file, or `jichi attempts show` / `recover` for dropped work.",
            path, task);
        result = jc_arena_strdup(app->scratch, msg.data);
        jc_sb_free(&msg);

        /* Emit one telemetry event */
        if (app->telemetry) {
            o = jc_app_telem_begin(app, "scoped_edit");
            if (o) {
                cJSON_AddStringToObject(o, "path", path);
                cJSON_AddNumberToObject(o, "task", (double)task);
                cJSON_AddStringToObject(o, "kind", "trip");
                jc_eventlog_end(app->telemetry, o);
            }
        }

        return result;
    }

    /* Allow and increment counter */
    if (e)
        e->edit_count++;

    return NULL;
}

void jc_scopededit_post_write(struct jc_app *app, const char *path)
{
    int idx;
    char full_path[4096];
    char norm_path[512];
    char *content;
    jc_size len;
    struct jc_scopededit_entry *e;
    const char *np;
    struct jc_arena *tool_arena;
    /* Only at top level and only if N > 0 */
    if (app->agent_depth != 0)
        return;
    if (app->scopededit == NULL || app->scopededit->n <= 0)
        return;

    /* tool_arena: per-call transient arena (M199) for comparison reads */
    tool_arena = jc_app_tool_scratch(app);

    /* Normalize path for lookup */
    np = normalize_path(app->cwd, app->root, path, norm_path, sizeof(norm_path));

    idx = scopededit_find(app->scopededit, np, app->cwd, app->root);
    if (idx < 0)
        return; /* Not owned */

    /* Re-read the path from disk using per-call arena, copy baseline to turn arena */
    /* Use the original path for building the full path (model's form) */
    if (strlen(app->root) + 1 + strlen(path) >= sizeof(full_path))
        return;
    jc_snprintf(full_path, sizeof(full_path), "%s/%s", app->root, path);

    if (jc_read_file(full_path, &content, &len, tool_arena) != 0)
        return;

    e = &app->scopededit->entries[idx];

    /* Arena doesn't have realloc, so we alloc fresh and copy to turn arena */
    e->snapshot = (char *)jc_arena_alloc(app->scratch, len);
    if (!e->snapshot)
        return;
    memcpy(e->snapshot, content, len);
    e->snapshot_len = len;
}

const char *jc_scopededit_post_tool(struct jc_app *app)
{
    const char *notice;
    int i;
    char full_path[4096];
    char *content;
    jc_size len;
    struct jc_scopededit_entry *e;
    struct jc_sb sb;
    struct jc_arena *tool_arena;
    cJSON *o;
    /* Self-gates: only at top level, only if N > 0 */
    if (app->agent_depth != 0)
        return NULL;
    if (app->scopededit == NULL || app->scopededit->n <= 0)
        return NULL;

    notice = NULL;

    /* tool_arena: per-call transient arena (M199) for comparison reads */
    tool_arena = jc_app_tool_scratch(app);

    /* Build the entire notice on a single jc_sb across the entire loop.
     * Only allocate the final notice string ONCE at the end. */
    jc_sb_init(&sb);

    for (i = 0; i < app->scopededit->count; i++) {
        e = &app->scopededit->entries[i];

        /* Re-read from disk using the per-call arena (M199) */
        if (strlen(app->root) + 1 + strlen(e->path) >= sizeof(full_path))
            continue;
        jc_snprintf(full_path, sizeof(full_path), "%s/%s", app->root, e->path);

        if (jc_read_file(full_path, &content, &len, tool_arena) != 0) {
            /* F5c: a deleted owned path is a change.
             * Do NOT report at the instant owner() recorded a 'D' merge (the path was
             * deleted by jichi itself as part of a merge). For now, we consider it a
             * change only if the file is missing AND the snapshot is non-empty. */
            if (e->snapshot_len > 0) {
                /* Emit one telemetry event */
                if (app->telemetry) {
                    o = jc_app_telem_begin(app, "scoped_edit");
                    if (o) {
                        cJSON_AddStringToObject(o, "path", e->path);
                        cJSON_AddStringToObject(o, "kind", "detected");
                        jc_eventlog_end(app->telemetry, o);
                    }
                }

                /* Re-baseline (the file is gone, snapshot stays for next comparison) */
                e->snapshot = NULL;
                e->snapshot_len = 0;

                /* Append this changed path to the notice sb */
                if (sb.len == 0) {
                    jc_sb_append_fmt(&sb, "paths changed outside the file tools: %s", e->path);
                } else {
                    jc_sb_append_fmt(&sb, ", %s", e->path);
                }
            }
            continue;
        }

        /* Compare to snapshot */
        if (len != e->snapshot_len || memcmp(content, e->snapshot, len) != 0) {
            /* Emit one telemetry event */
            if (app->telemetry) {
                o = jc_app_telem_begin(app, "scoped_edit");
                if (o) {
                    cJSON_AddStringToObject(o, "path", e->path);
                    cJSON_AddStringToObject(o, "kind", "detected");
                    jc_eventlog_end(app->telemetry, o);
                }
            }

            /* Re-baseline so same out-of-band change is not re-reported */
            e->snapshot = (char *)jc_arena_alloc(app->scratch, len);
            if (e->snapshot) {
                memcpy(e->snapshot, content, len);
                e->snapshot_len = len;
            }

            /* Append this changed path to the notice sb */
            if (sb.len == 0) {
                jc_sb_append_fmt(&sb, "paths changed outside the file tools: %s", e->path);
            } else {
                jc_sb_append_fmt(&sb, ", %s", e->path);
            }
        }
    }

    /* Allocate the final notice string ONCE (if any changes were detected) */
    if (sb.len > 0) {
        notice = jc_arena_strdup(app->scratch, sb.data);
    }
    jc_sb_free(&sb);

    return notice;
}
