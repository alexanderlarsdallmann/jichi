/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_calib.c - persistent per-model token-estimate calibration (see
 * jc_calib.h). The clamp/blend math is pure; load/save is JSON I/O. */

#include "jc_calib.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_json.h"
#include "jc_platform.h"
#include "jc_log.h"

#include <stdlib.h>
#include <string.h>

double jc_calib_clamp(double r)
{
    if (r < JC_CALIB_MIN) {
        return JC_CALIB_MIN;
    }
    if (r > JC_CALIB_MAX) {
        return JC_CALIB_MAX;
    }
    return r;
}

double jc_calib_blend(double ratio, long samples, double sample)
{
    long n;
    double w;

    if (samples <= 0) {
        return jc_calib_clamp(sample);
    }
    n = (samples < JC_CALIB_WINDOW) ? samples : JC_CALIB_WINDOW;
    w = 1.0 / (double)(n + 1);
    return jc_calib_clamp(ratio + (sample - ratio) * w);
}

void jc_calib_init(struct jc_calib *c, struct jc_arena *a)
{
    if (c == NULL) {
        return;
    }
    jc_vec_init(&c->entries, sizeof(struct jc_calib_entry));
    c->arena = a;
    c->path = NULL;
    c->dirty = 0;
}

void jc_calib_free(struct jc_calib *c)
{
    if (c == NULL) {
        return;
    }
    jc_vec_free(&c->entries);
    /* model_id/path are arena-owned; the arena frees them. */
}

/* Locate an entry by model id, or NULL. */
static struct jc_calib_entry *find(const struct jc_calib *c, const char *id)
{
    jc_size i;
    if (c == NULL || id == NULL) {
        return NULL;
    }
    for (i = 0; i < c->entries.len; i++) {
        struct jc_calib_entry *e =
            (struct jc_calib_entry *)jc_vec_at((struct jc_vec *)&c->entries, i);
        if (e->model_id != NULL && strcmp(e->model_id, id) == 0) {
            return e;
        }
    }
    return NULL;
}

double jc_calib_get(const struct jc_calib *c, const char *model_id)
{
    struct jc_calib_entry *e = find(c, model_id);
    if (e != NULL && e->samples > 0 && e->ratio > 0.0) {
        return e->ratio;
    }
    return 1.0;
}

void jc_calib_observe(struct jc_calib *c, const char *model_id,
                      long real, long est)
{
    struct jc_calib_entry *e;
    double sample;

    if (c == NULL || model_id == NULL || model_id[0] == '\0' ||
        real <= 0 || est <= 0) {
        return;
    }
    sample = (double)real / (double)est;
    /* Reject a wildly out-of-band sample outright (a truncated/failed request)
     * rather than clamp-and-record it, so one bad turn can't drag the average. */
    if (sample < JC_CALIB_MIN || sample > JC_CALIB_MAX) {
        return;
    }
    e = find(c, model_id);
    if (e == NULL) {
        struct jc_calib_entry ne;
        if (c->entries.len >= (jc_size)JC_CALIB_MAX_ENTRIES) {
            return; /* table full: don't grow a shared file without bound */
        }
        ne.model_id = jc_arena_strdup(c->arena, model_id);
        ne.ratio = 0.0;
        ne.samples = 0;
        if (ne.model_id == NULL || jc_vec_push(&c->entries, &ne) != JC_OK) {
            return;
        }
        e = (struct jc_calib_entry *)jc_vec_at(&c->entries,
                                               c->entries.len - 1);
    }
    e->ratio = jc_calib_blend(e->ratio, e->samples, sample);
    e->samples++;
    c->dirty = 1;
}

jc_status jc_calib_load(struct jc_calib *c, const char *path)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root;
    cJSON *item;

    if (c == NULL) {
        return JC_ERR_INVALID;
    }
    if (path != NULL) {
        c->path = jc_arena_strdup(c->arena, path);
    }
    c->dirty = 0;
    /* jc_read_file allocates from the arena, so `text` is not free()d here. */
    if (path == NULL ||
        jc_read_file(path, &text, &len, c->arena) != JC_OK || text == NULL) {
        return JC_OK; /* no file yet: an empty table is fine */
    }
    root = jc_json_parse(text);
    if (root == NULL) {
        jc_logf(JC_LOG_WARN, "calibration: ignoring malformed %s", path);
        return JC_OK;
    }
    /* A ratio measured against a different estimate basis is not a ratio for
     * this one, so an older table is dropped rather than trusted or averaged
     * down (M286). Absent "v" means version 1, the pre-M286 `history + 2000`
     * basis. The entries re-populate from the first model call onward. */
    if ((int)jc_json_get_num(root, "v", 1.0) < JC_CALIB_SCHEMA) {
        jc_logf(JC_LOG_INFO, "calibration: %s was written against an older "
                "token-estimate basis -- discarding it; each model recalibrates "
                "from its next model call", path);
        cJSON_Delete(root);
        return JC_OK;
    }
    for (item = root->child; item != NULL; item = item->next) {
        struct jc_calib_entry ne;
        double ratio;
        double samples;
        if (item->string == NULL || !cJSON_IsObject(item)) {
            continue;
        }
        if (c->entries.len >= (jc_size)JC_CALIB_MAX_ENTRIES) {
            break;
        }
        ratio = jc_json_get_num(item, "ratio", 0.0);
        samples = jc_json_get_num(item, "samples", 0.0);
        if (ratio <= 0.0 || samples <= 0.0) {
            continue;
        }
        ne.model_id = jc_arena_strdup(c->arena, item->string);
        ne.ratio = jc_calib_clamp(ratio);
        ne.samples = (long)samples;
        if (ne.model_id != NULL) {
            jc_vec_push(&c->entries, &ne);
        }
    }
    cJSON_Delete(root);
    return JC_OK;
}

/* Copy the parent directory of `path` into `dir` (bounded). */
static void parent_dir(const char *path, char *dir, jc_size cap)
{
    const char *slash = strrchr(path, '/');
    jc_size n;
    if (slash == NULL || cap == 0) {
        if (cap > 0) {
            dir[0] = '\0';
        }
        return;
    }
    n = (jc_size)(slash - path);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dir, path, n);
    dir[n] = '\0';
}

jc_status jc_calib_save(struct jc_calib *c)
{
    cJSON *root;
    char *text;
    char dir[1024];
    jc_size i;
    jc_status st;

    if (c == NULL || !c->dirty || c->path == NULL) {
        return JC_OK;
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return JC_ERR_OOM;
    }
    /* Stamp the estimate basis these ratios were measured against (M286), so a
     * future basis change can discard them instead of averaging across two
     * incompatible definitions. A model id can never collide with this key: ids
     * are model names, and the load loop skips any non-object member anyway. */
    cJSON_AddNumberToObject(root, "v", (double)JC_CALIB_SCHEMA);
    for (i = 0; i < c->entries.len; i++) {
        struct jc_calib_entry *e =
            (struct jc_calib_entry *)jc_vec_at(&c->entries, i);
        cJSON *o;
        if (e->model_id == NULL || e->samples <= 0) {
            continue;
        }
        o = cJSON_CreateObject();
        if (o == NULL) {
            continue;
        }
        cJSON_AddNumberToObject(o, "ratio", e->ratio);
        cJSON_AddNumberToObject(o, "samples", (double)e->samples);
        cJSON_AddItemToObject(root, e->model_id, o);
    }
    text = jc_json_print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return JC_ERR_OOM;
    }
    parent_dir(c->path, dir, sizeof(dir));
    if (dir[0] != '\0') {
        jc_mkdir_p(dir);
    }
    /* M146: atomic -- the calibration file is global and last-writer-wins
     * across all instances; atomicity at least keeps every version whole. */
    st = jc_write_file_atomic(c->path, text, (jc_size)strlen(text));
    free(text);
    if (st == JC_OK) {
        jc_make_private(c->path); /* owner-only (M132) */
        c->dirty = 0;
    }
    return st;
}
