/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_command.c - custom slash commands: loading + template expansion. */

#include "jc_command.h"
#include "jc_proc.h"
#include "jc_md.h"
#include "jc_yaml.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define JC_CMD_SHELL_MAX (16 * 1024)
#define JC_CMD_FILE_MAX  (32 * 1024)

void jc_command_set_init(struct jc_command_set *s)
{
    jc_vec_init(&s->commands, sizeof(struct jc_command));
}

void jc_command_render_list(const struct jc_command_set *s, struct jc_sb *out)
{
    jc_size i;
    if (s == NULL || out == NULL) {
        return;
    }
    for (i = 0; i < s->commands.len; i++) {
        const struct jc_command *c =
            (const struct jc_command *)jc_vec_at((struct jc_vec *)&s->commands,
                                                 i);
        jc_sb_append(out, "  /");
        jc_sb_append(out, c->name);
        if (c->description != NULL && c->description[0] != '\0') {
            jc_sb_append(out, " - ");
            jc_sb_append(out, c->description);
        }
        jc_sb_append_char(out, '\n');
    }
}

void jc_command_set_free(struct jc_command_set *s)
{
    jc_vec_free(&s->commands);
}

const struct jc_command *jc_command_find(const struct jc_command_set *s,
                                         const char *name)
{
    jc_size i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < s->commands.len; i++) {
        struct jc_command *c =
            (struct jc_command *)jc_vec_at((struct jc_vec *)&s->commands, i);
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

static void add_or_replace(struct jc_command_set *s,
                           const struct jc_command *cmd)
{
    jc_size i;
    for (i = 0; i < s->commands.len; i++) {
        struct jc_command *c =
            (struct jc_command *)jc_vec_at(&s->commands, i);
        if (strcmp(c->name, cmd->name) == 0) {
            *c = *cmd;
            return;
        }
    }
    jc_vec_push(&s->commands, cmd);
}

static void parse_one(struct jc_command_set *s, const char *name,
                      const char *text, struct jc_arena *a)
{
    struct jc_md_doc doc;
    struct jc_command cmd;
    const char *str;

    memset(&cmd, 0, sizeof(cmd));
    cmd.name = jc_arena_strdup(a, name);

    jc_md_parse(text, a, &doc);
    if (doc.front != NULL) {
        str = jc_yaml_get_str(doc.front, "description", NULL);
        cmd.description = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        str = jc_yaml_get_str(doc.front, "model", NULL);
        cmd.model = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        str = jc_yaml_get_str(doc.front, "agent", NULL);
        cmd.agent = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        str = jc_yaml_get_str(doc.front, "subtask", NULL);
        /* M534: the shared dialect, for the same reason as the two fences. */
        cmd.subtask = 0;
        if (str != NULL) {
            int cb = 0;
            if (jc_bool_from_word(str, &cb)) { cmd.subtask = cb; }
        }
        str = jc_yaml_get_str(doc.front, "output", NULL);
        cmd.output = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
        str = jc_yaml_get_str(doc.front, "language", NULL);
        cmd.language = (str != NULL) ? jc_arena_strdup(a, str) : NULL;
    }
    cmd.body = jc_arena_strdup(a, doc.body != NULL ? doc.body : "");
    jc_md_free(&doc);

    add_or_replace(s, &cmd);
}

static void load_dir(struct jc_command_set *s, const char *dir,
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

jc_status jc_command_load(struct jc_command_set *s, const char *cwd,
                          struct jc_arena *a)
{
    char dir[1100];
    jc_snprintf(dir, sizeof(dir), "%s/.config/jichi/commands",
                jc_home_dir());
    load_dir(s, dir, a);
    jc_snprintf(dir, sizeof(dir), "%s/.jichi/commands", cwd);
    load_dir(s, dir, a);
    return JC_OK;
}

/* --- template expansion --- */

/* Append `cmd`'s stdout (combined with stderr), bounded. */
static void append_shell(struct jc_sb *sb, const char *cmd)
{
    char *full;
    FILE *pipe;
    char chunk[4096];
    size_t n;
    jc_size emitted = 0;
    jc_size cmdlen = strlen(cmd);

    full = (char *)malloc(cmdlen + 8);
    if (full == NULL) {
        return;
    }
    jc_snprintf(full, cmdlen + 8, "%s 2>&1", cmd);
    pipe = jc_proc_popen(full, "r");
    free(full);
    if (pipe == NULL) {
        jc_sb_append(sb, "[command failed]");
        return;
    }
    while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        if (emitted + n > JC_CMD_SHELL_MAX) {
            jc_sb_append_n(sb, chunk, JC_CMD_SHELL_MAX - emitted);
            jc_sb_append(sb, "...[truncated]");
            emitted = JC_CMD_SHELL_MAX;
            break;
        }
        jc_sb_append_n(sb, chunk, (jc_size)n);
        emitted += (jc_size)n;
    }
    pclose(pipe);
}

/* Append the contents of `path` (relative to cwd unless absolute), bounded. */
static void append_file(struct jc_sb *sb, const char *path, const char *cwd,
                        struct jc_arena *a)
{
    char full[1100];
    char *data;
    jc_size len = 0;

    if (path[0] == '/') {
        jc_snprintf(full, sizeof(full), "%s", path);
    } else {
        jc_snprintf(full, sizeof(full), "%s/%s", cwd, path);
    }
    if (jc_read_file(full, &data, &len, a) != JC_OK) {
        jc_sb_append_fmt(sb, "[could not read %s]", path);
        return;
    }
    if (len > JC_CMD_FILE_MAX) {
        len = JC_CMD_FILE_MAX;
    }
    jc_sb_append_n(sb, data, len);
}

/* Split `args_raw` into whitespace tokens (arena copies) pushed onto `tokens`. */
static void split_args(const char *args_raw, struct jc_vec *tokens,
                       struct jc_arena *a)
{
    const char *p = args_raw;
    if (p == NULL) {
        return;
    }
    while (*p != '\0') {
        const char *start;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        {
            char *tok = jc_arena_strndup(a, start, (jc_size)(p - start));
            jc_vec_push(tokens, &tok);
        }
    }
}

jc_status jc_command_expand(const struct jc_command *c, const char *args_raw,
                            const char *cwd, struct jc_arena *a, char **out)
{
    struct jc_sb sb;
    struct jc_vec tokens;
    const char *p;

    jc_vec_init(&tokens, sizeof(char *));
    split_args(args_raw, &tokens, a);
    jc_sb_init(&sb);

    for (p = c->body; *p != '\0'; ) {
        if (*p == '$' && strncmp(p + 1, "ARGUMENTS", 9) == 0 &&
            !isalnum((unsigned char)p[10])) {
            jc_sb_append(&sb, args_raw != NULL ? args_raw : "");
            p += 10;
        } else if (*p == '$' && isdigit((unsigned char)p[1])) {
            jc_size idx = 0;
            p++;
            while (isdigit((unsigned char)*p)) {
                idx = idx * 10 + (jc_size)(*p - '0');
                p++;
            }
            if (idx >= 1 && idx <= tokens.len) {
                jc_sb_append(&sb, *(char **)jc_vec_at(&tokens, idx - 1));
            }
        } else if (*p == '!' && p[1] == '`') {
            const char *end = strchr(p + 2, '`');
            if (end != NULL) {
                char *cmd = jc_arena_strndup(a, p + 2, (jc_size)(end - (p + 2)));
                append_shell(&sb, cmd);
                p = end + 1;
            } else {
                jc_sb_append_char(&sb, *p);
                p++;
            }
        } else if (*p == '@') {
            const char *start = p + 1;
            const char *q = start;
            while (*q != '\0' && *q != ' ' && *q != '\t' && *q != '\n') {
                q++;
            }
            if (q > start) {
                char *path = jc_arena_strndup(a, start, (jc_size)(q - start));
                append_file(&sb, path, cwd, a);
                p = q;
            } else {
                jc_sb_append_char(&sb, *p);
                p++;
            }
        } else {
            jc_sb_append_char(&sb, *p);
            p++;
        }
    }

    jc_vec_free(&tokens);
    *out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return JC_OK;
}
