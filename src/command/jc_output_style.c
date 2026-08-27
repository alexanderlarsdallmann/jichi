/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_output_style.c - custom output styles: loading + selection.
 *
 * Mirrors the flat-markdown loading of jc_command.c (global then project dir,
 * project overrides on a name collision); the body is injected into the system
 * prompt by jc_sysmsg_build when the style is active.
 */

#include "jc_output_style.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_platform.h"

#include <string.h>

void jc_output_style_set_init(struct jc_output_style_set *s)
{
    jc_vec_init(&s->styles, sizeof(struct jc_output_style));
    s->active = NULL;
}

void jc_output_style_set_free(struct jc_output_style_set *s)
{
    jc_vec_free(&s->styles);
    s->active = NULL;
}

const struct jc_output_style *jc_output_style_find(
    const struct jc_output_style_set *s, const char *name)
{
    jc_size i;
    if (s == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < s->styles.len; i++) {
        const struct jc_output_style *o =
            (const struct jc_output_style *)
                jc_vec_at((struct jc_vec *)&s->styles, i);
        if (strcmp(o->name, name) == 0) {
            return o;
        }
    }
    return NULL;
}

const struct jc_output_style *jc_output_style_active(
    const struct jc_output_style_set *s)
{
    if (s == NULL || s->active == NULL) {
        return NULL;
    }
    return jc_output_style_find(s, s->active);
}

int jc_output_style_set_active(struct jc_output_style_set *s, const char *name)
{
    const struct jc_output_style *o;
    if (name == NULL || name[0] == '\0') {
        s->active = NULL;
        return 1;
    }
    o = jc_output_style_find(s, name);
    if (o == NULL) {
        return 0;
    }
    s->active = o->name; /* stable arena-owned pointer */
    return 1;
}

void jc_output_style_parse(const char *text, const char *name,
                           struct jc_arena *a, struct jc_output_style *out)
{
    struct jc_md_doc doc;
    const char *str;

    memset(out, 0, sizeof(*out));
    out->name = jc_arena_strdup(a, name != NULL ? name : "");
    jc_md_parse(text, a, &doc);
    if (doc.front != NULL) {
        str = jc_yaml_get_str(doc.front, "description", NULL);
        out->description = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
    }
    out->body = jc_arena_strdup(a, doc.body != NULL ? doc.body : "");
    jc_md_free(&doc);
}

static void add_or_replace(struct jc_output_style_set *s,
                           const struct jc_output_style *st)
{
    jc_size i;
    for (i = 0; i < s->styles.len; i++) {
        struct jc_output_style *o =
            (struct jc_output_style *)jc_vec_at(&s->styles, i);
        if (strcmp(o->name, st->name) == 0) {
            *o = *st;
            return;
        }
    }
    jc_vec_push(&s->styles, st);
}

static void load_dir(struct jc_output_style_set *s, const char *dir,
                     struct jc_arena *a)
{
    struct jc_vec names;
    jc_size i;

    if (!jc_is_dir(dir)) {
        return;
    }
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, a) != JC_OK) {
        jc_vec_free(&names);
        return;
    }
    for (i = 0; i < names.len; i++) {
        const char *fn = *(char **)jc_vec_at(&names, i);
        jc_size len = strlen(fn);
        char path[1100];
        char base[256];
        char *text;
        struct jc_output_style st;

        if (len < 4 || strcmp(fn + len - 3, ".md") != 0) {
            continue;
        }
        if (len - 3 >= sizeof(base)) {
            continue;
        }
        memcpy(base, fn, len - 3);
        base[len - 3] = '\0';
        jc_snprintf(path, sizeof(path), "%s/%s", dir, fn);
        if (jc_is_regular_file(path) && /* M198: skip FIFO/socket/device */
            jc_read_file(path, &text, NULL, a) == JC_OK) {
            jc_output_style_parse(text, base, a, &st);
            add_or_replace(s, &st);
        }
    }
    jc_vec_free(&names);
}

jc_status jc_output_style_load(struct jc_output_style_set *s, const char *cwd,
                               struct jc_arena *a)
{
    char dir[1100];
    jc_snprintf(dir, sizeof(dir), "%s/.config/jichi/output-styles",
                jc_home_dir());
    load_dir(s, dir, a);
    jc_snprintf(dir, sizeof(dir), "%s/.jichi/output-styles", cwd);
    load_dir(s, dir, a);
    return JC_OK;
}

void jc_output_style_render_list(const struct jc_output_style_set *s,
                                 struct jc_sb *out)
{
    jc_size i;
    if (s == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < s->styles.len; i++) {
        const struct jc_output_style *o =
            (const struct jc_output_style *)
                jc_vec_at((struct jc_vec *)&s->styles, i);
        int active = (s->active != NULL && strcmp(s->active, o->name) == 0);
        jc_sb_append(out, active ? "  * " : "    ");
        jc_sb_append(out, o->name);
        if (o->description != NULL && o->description[0] != '\0') {
            jc_sb_append(out, " - ");
            jc_sb_append(out, o->description);
        }
        jc_sb_append_char(out, '\n');
    }
}
