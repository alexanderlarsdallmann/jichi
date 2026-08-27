/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_skill.c - agent skills loaded from SKILL.md folders (see jc_skill.h). */

#include "jc_skill.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_platform.h"

#include <string.h>

void jc_skill_set_init(struct jc_skill_set *s)
{
    jc_vec_init(&s->skills, sizeof(struct jc_skill));
}

void jc_skill_set_free(struct jc_skill_set *s)
{
    jc_size i;
    for (i = 0; i < s->skills.len; i++) {
        struct jc_skill *sk = (struct jc_skill *)jc_vec_at(&s->skills, i);
        jc_vec_free(&sk->tools); /* the vec handle is heap-owned */
    }
    jc_vec_free(&s->skills); /* string fields are arena-owned */
}

const struct jc_skill *jc_skill_find(const struct jc_skill_set *s,
                                     const char *name)
{
    jc_size i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < s->skills.len; i++) {
        struct jc_skill *sk =
            (struct jc_skill *)jc_vec_at((struct jc_vec *)&s->skills, i);
        if (strcmp(sk->name, name) == 0) {
            return sk;
        }
    }
    return NULL;
}

int jc_skill_count(const struct jc_skill_set *s)
{
    return (s != NULL) ? (int)s->skills.len : 0;
}

const struct jc_skill *jc_skill_at(const struct jc_skill_set *s, int i)
{
    if (s == NULL || i < 0 || (jc_size)i >= s->skills.len) {
        return NULL;
    }
    return (struct jc_skill *)jc_vec_at((struct jc_vec *)&s->skills,
                                        (jc_size)i);
}

void jc_skill_parse(const char *text, const char *default_name,
                    const char *dir, struct jc_arena *a, struct jc_skill *out)
{
    struct jc_md_doc doc;
    const char *str;
    int sb_ok = 0;   /* M534: shared boolean dialect */

    memset(out, 0, sizeof(*out));
    jc_vec_init(&out->tools, sizeof(char *));
    out->dir = jc_arena_strdup(a, dir != NULL ? dir : "");

    jc_md_parse(text != NULL ? text : "", a, &doc);
    if (doc.front != NULL) {
        struct jc_yaml *tools;
        str = jc_yaml_get_str(doc.front, "name", NULL);
        if (str != NULL && str[0] != '\0') {
            out->name = jc_arena_strdup(a, str);
        }
        str = jc_yaml_get_str(doc.front, "description", NULL);
        out->description = jc_arena_strdup(a, str != NULL ? str : "");

        /* allowed-tools (Claude-Skills spelling), or `tools` as an alias. */
        tools = jc_yaml_get(doc.front, "allowed-tools");
        if (tools == NULL) {
            tools = jc_yaml_get(doc.front, "tools");
        }
        {
            jc_size n = jc_yaml_seq_len(tools);
            jc_size k;
            for (k = 0; k < n; k++) {
                struct jc_yaml *t = jc_yaml_seq_at(tools, k);
                if (t != NULL && t->scalar != NULL && t->scalar[0] != '\0') {
                    char *c = jc_arena_strdup(a, t->scalar);
                    jc_vec_push(&out->tools, &c);
                }
            }
        }
        /* restrict-tools: true turns the advisory `tools` list into an enforced
         * fence when the skill is loaded inside a subagent (bounded lifetime). */
        str = jc_yaml_get_str(doc.front, "restrict-tools", NULL);
        /* M534: the shared dialect -- `restrict-tools: yes` fences a subagent
         * off write_file exactly as `true` does. Measured before: it did not,
         * and the child wrote. */
        if (str != NULL && jc_bool_from_word(str, &sb_ok) && sb_ok) {
            out->restrict_tools = 1;
        }
        /* M302: a named output style for this skill's tone. Same advisory/enforced
         * split as `tools` above -- see jc_skill.h. */
        str = jc_yaml_get_str(doc.front, "style", NULL);
        out->style = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
    } else {
        out->description = jc_arena_strdup(a, "");
    }
    if (out->name == NULL) {
        out->name = jc_arena_strdup(a, default_name != NULL ? default_name : "");
    }
    out->body = jc_arena_strdup(a, doc.body != NULL ? doc.body : "");
    jc_md_free(&doc);
}

/* Insert skill, replacing any existing entry of the same name (project wins). */
static void add_or_replace(struct jc_skill_set *s, const struct jc_skill *sk)
{
    jc_size i;
    for (i = 0; i < s->skills.len; i++) {
        struct jc_skill *e = (struct jc_skill *)jc_vec_at(&s->skills, i);
        if (strcmp(e->name, sk->name) == 0) {
            jc_vec_free(&e->tools); /* drop the replaced skill's fence vec */
            *e = *sk;
            return;
        }
    }
    jc_vec_push(&s->skills, sk);
}

/* Scan a skills directory: each subdirectory holding a SKILL.md is one skill. */
static void load_dir(struct jc_skill_set *s, const char *dir, struct jc_arena *a)
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
        const char *nm = *(char **)jc_vec_at(&names, i);
        char subdir[1100];
        char path[1200];
        char *text;

        jc_snprintf(subdir, sizeof(subdir), "%s/%s", dir, nm);
        if (!jc_is_dir(subdir)) {
            continue;
        }
        jc_snprintf(path, sizeof(path), "%s/SKILL.md", subdir);
        if (jc_is_regular_file(path) && /* M198: skip FIFO/socket/device */
            jc_read_file(path, &text, NULL, a) == JC_OK) {
            struct jc_skill sk;
            jc_skill_parse(text, nm, subdir, a, &sk);
            if (sk.name[0] != '\0') {
                add_or_replace(s, &sk);
            }
        }
    }
    jc_vec_free(&names);
}

jc_status jc_skill_load(struct jc_skill_set *s, const char *cwd,
                        struct jc_arena *a)
{
    char dir[1100];

    jc_snprintf(dir, sizeof(dir), "%s/.config/jichi/skills",
                jc_home_dir());
    load_dir(s, dir, a); /* global first */
    jc_snprintf(dir, sizeof(dir), "%s/.jichi/skills", cwd);
    load_dir(s, dir, a); /* project overrides */
    return JC_OK;
}

void jc_skill_render_catalog(const struct jc_skill_set *s, struct jc_sb *out)
{
    jc_size i;

    if (s == NULL || s->skills.len == 0) {
        return;
    }
    jc_sb_append(out,
        "\n\n## Available skills\n"
        "Each skill below provides detailed, step-by-step instructions for a "
        "specific kind of task. When the user's request matches a skill, call "
        "the load_skill tool with its name to read the full instructions, then "
        "follow them. Do not guess a skill's contents from its description.\n");
    for (i = 0; i < s->skills.len; i++) {
        struct jc_skill *sk =
            (struct jc_skill *)jc_vec_at((struct jc_vec *)&s->skills, i);
        jc_sb_append_fmt(out, "- %s: %s\n", sk->name,
                         (sk->description != NULL) ? sk->description : "");
    }
}
