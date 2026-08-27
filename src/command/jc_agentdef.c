/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_agentdef.c - load named agent profiles from markdown (see jc_agentdef.h). */

#include "jc_agentdef.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_platform.h"

#include <string.h>

void jc_agentdef_set_init(struct jc_agentdef_set *s)
{
    jc_vec_init(&s->defs, sizeof(struct jc_agentdef));
}

void jc_agentdef_render_list(const struct jc_agentdef_set *s, struct jc_sb *out)
{
    jc_size i;
    if (s == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < s->defs.len; i++) {
        const struct jc_agentdef *d =
            (const struct jc_agentdef *)jc_vec_at((struct jc_vec *)&s->defs, i);
        int wrote_attr = 0;
        jc_size j;
        jc_sb_append(out, "  ");
        jc_sb_append(out, d->name);
        if (d->description != NULL && d->description[0] != '\0') {
            jc_sb_append(out, " - ");
            jc_sb_append(out, d->description);
        }
        jc_sb_append_char(out, '\n');
        /* Second line: attributes, only when present. */
        if (d->model != NULL && d->model[0] != '\0') {
            jc_sb_append(out, "      model: ");
            jc_sb_append(out, d->model);
            wrote_attr = 1;
        }
        if (d->has_readonly && d->readonly) {
            jc_sb_append(out, wrote_attr ? " \xc2\xb7 readonly" : "      readonly");
            wrote_attr = 1;
        }
        if (d->tools.len > 0) {
            jc_sb_append(out, wrote_attr ? " \xc2\xb7 tools: " : "      tools: ");
            for (j = 0; j < d->tools.len; j++) {
                const char *t =
                    *(char **)jc_vec_at((struct jc_vec *)&d->tools, j);
                if (j > 0) {
                    jc_sb_append(out, ", ");
                }
                jc_sb_append(out, t);
            }
            wrote_attr = 1;
        }
        if (wrote_attr) {
            jc_sb_append_char(out, '\n');
        }
    }
}

void jc_agentdef_set_free(struct jc_agentdef_set *s)
{
    jc_size i;
    for (i = 0; i < s->defs.len; i++) {
        struct jc_agentdef *d = (struct jc_agentdef *)jc_vec_at(&s->defs, i);
        jc_vec_free(&d->tools);
    }
    jc_vec_free(&s->defs);
}

const struct jc_agentdef *jc_agentdef_find(const struct jc_agentdef_set *s,
                                           const char *name)
{
    jc_size i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < s->defs.len; i++) {
        struct jc_agentdef *d =
            (struct jc_agentdef *)jc_vec_at((struct jc_vec *)&s->defs, i);
        if (strcmp(d->name, name) == 0) {
            return d;
        }
    }
    return NULL;
}

void jc_agentdef_merge(const struct jc_agentdef *def, const char *arg_model,
                       int has_arg_ro, int arg_ro,
                       const char **model_out, int *ro_out)
{
    if (arg_model != NULL && arg_model[0] != '\0') {
        *model_out = arg_model;
    } else {
        *model_out = (def != NULL) ? def->model : NULL;
    }
    if (has_arg_ro) {
        *ro_out = arg_ro;
    } else if (def != NULL && def->has_readonly) {
        *ro_out = def->readonly;
    } else {
        *ro_out = 0;
    }
}

/* Insert def, replacing any existing entry of the same name (project wins). */
static void add_or_replace(struct jc_agentdef_set *s,
                           const struct jc_agentdef *def)
{
    jc_size i;
    for (i = 0; i < s->defs.len; i++) {
        struct jc_agentdef *d = (struct jc_agentdef *)jc_vec_at(&s->defs, i);
        if (strcmp(d->name, def->name) == 0) {
            jc_vec_free(&d->tools);
            *d = *def;
            return;
        }
    }
    jc_vec_push(&s->defs, def);
}

/* Build a profile from one markdown file's text. name is the basename. */
static void parse_one(struct jc_agentdef_set *s, const char *name,
                      const char *text, struct jc_arena *a)
{
    struct jc_md_doc doc;
    struct jc_agentdef def;
    const char *str;

    memset(&def, 0, sizeof(def));
    jc_vec_init(&def.tools, sizeof(char *));
    def.name = jc_arena_strdup(a, name);

    jc_md_parse(text, a, &doc);
    if (doc.front != NULL) {
        str = jc_yaml_get_str(doc.front, "description", NULL);
        def.description = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        str = jc_yaml_get_str(doc.front, "model", NULL);
        def.model = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        /* M302: a named output style, not prose -- reusing M28 rather than adding
         * a second persona mechanism to drift from the first. */
        str = jc_yaml_get_str(doc.front, "style", NULL);
        def.style = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        str = jc_yaml_get_str(doc.front, "readonly", NULL);
        if (str != NULL) {
            /* M534: the shared dialect. This was `strcmp(str, "true")`, so a
             * profile declaring `readonly: yes`, `readonly: True` or
             * `readonly: 1` set has_readonly=1 with readonly=0 -- read downstream
             * as an EXPLICIT "writable". Measured: four profiles each declaring
             * read-only, and only the one spelling `true` was fenced. The same
             * presence-check-plus-fallback shape as M519's `"pathFence": 1` and
             * M530's `{"readonly": 1}`, now in the YAML path.
             *
             * A value that is not a boolean word at all leaves has_readonly set
             * but the value untouched-and-false, which is the conservative
             * reading: the key was written, we could not understand it, and a
             * fence is not something to grant by guessing. */
            int b = 0;
            def.has_readonly = 1;
            def.readonly = jc_bool_from_word(str, &b) ? b : 0;
        }
        {
            struct jc_yaml *tools = jc_yaml_get(doc.front, "tools");
            jc_size n = jc_yaml_seq_len(tools);
            jc_size k;
            for (k = 0; k < n; k++) {
                struct jc_yaml *t = jc_yaml_seq_at(tools, k);
                if (t != NULL && t->scalar != NULL) {
                    char *c = jc_arena_strdup(a, t->scalar);
                    jc_vec_push(&def.tools, &c);
                }
            }
        }
    }
    def.system_prompt = jc_arena_strdup(a, doc.body != NULL ? doc.body : "");
    jc_md_free(&doc);

    add_or_replace(s, &def);
}

/* Scan a directory for .md files and parse each into the set. */
static void load_dir(struct jc_agentdef_set *s, const char *dir,
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
            parse_one(s, base, text, a);
        }
    }
    jc_vec_free(&names);
}

jc_status jc_agentdef_load(struct jc_agentdef_set *s, const char *cwd,
                           struct jc_arena *a)
{
    char dir[1100];

    jc_snprintf(dir, sizeof(dir), "%s/.config/jichi/agents",
                jc_home_dir());
    load_dir(s, dir, a); /* global first */
    jc_snprintf(dir, sizeof(dir), "%s/.jichi/agents", cwd);
    load_dir(s, dir, a); /* project overrides */
    return JC_OK;
}
