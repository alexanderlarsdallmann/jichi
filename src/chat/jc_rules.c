/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rules.c - instruction-file discovery (see jc_rules.h). */

#include "jc_rules.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>

#define JC_RULES_MAX   (32 * 1024)
#define JC_RULES_WALK_MAX 64 /* guard against pathological directory chains */

struct rules_ctx {
    struct jc_sb     sb;
    struct jc_vec    seen; /* of char*: paths already included */
    struct jc_arena *a;
    int              truncated;
};

static int already_seen(struct rules_ctx *c, const char *path)
{
    jc_size i;
    for (i = 0; i < c->seen.len; i++) {
        if (strcmp(*(char **)jc_vec_at(&c->seen, i), path) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Read `path` and append it (with an attribution header) to the buffer, honring
 * the dedup set and the size cap. Missing files are silently skipped. */
static void add_file(struct rules_ctx *c, const char *path)
{
    char *data;
    jc_size len = 0;
    char hdr[1200];
    char *pc;

    if (c->truncated || already_seen(c, path)) {
        return;
    }
    if (jc_read_file(path, &data, &len, c->a) != JC_OK) {
        return;
    }
    pc = jc_arena_strdup(c->a, path);
    jc_vec_push(&c->seen, &pc);

    jc_snprintf(hdr, sizeof(hdr), "\n\n# Rules from %s\n\n", path);
    jc_sb_append(&c->sb, hdr);
    if (c->sb.len + len > JC_RULES_MAX) {
        jc_size rem = (c->sb.len < JC_RULES_MAX) ? (JC_RULES_MAX - c->sb.len)
                                                 : 0;
        /* M516: back the cut off to a character boundary. A byte-exact cap
         * split a multi-byte character in half -- an em dash in this
         * repository's own CLAUDE.md -- on EVERY request, and the provider
         * layer then replaced the orphaned bytes with U+FFFD and warned once
         * per call. Self-healing, so invisible until a self-hosting review run
         * printed the warning six times in a row
         * (docs/analysis/2026-08-21-self-hosting-first-review.md).
         *
         * The loop, not jc_utf8_prev(): that helper steps back one whole
         * character unconditionally, which would drop a character even when
         * the cut is already clean. Here the question is only "is data[rem] a
         * continuation byte", i.e. is this cut inside a character. Note the
         * OTHER truncation path -- the context-window fit -- was already
         * boundary-aware, which is why this only showed at some window
         * sizes. */
        while (rem > 0 && ((unsigned char)data[rem] & 0xC0) == 0x80) {
            rem--;
        }
        jc_sb_append_n(&c->sb, data, rem);
        jc_sb_append(&c->sb, "\n... [rules truncated]");
        c->truncated = 1;
    } else {
        jc_sb_append_n(&c->sb, data, len);
    }
}

/* Try AGENTS.md in `dir`, then CLAUDE.md as a fallback. */
static void add_dir_rules(struct rules_ctx *c, const char *dir)
{
    char path[1100];
    jc_snprintf(path, sizeof(path), "%s/AGENTS.md", dir);
    if (jc_file_exists(path)) {
        add_file(c, path);
        return;
    }
    jc_snprintf(path, sizeof(path), "%s/CLAUDE.md", dir);
    if (jc_file_exists(path)) {
        add_file(c, path);
    }
}

char *jc_rules_load(struct jc_app *app)
{
    struct rules_ctx c;
    struct jc_vec dirs; /* cwd first, then ancestors */
    char path[1200];
    char cur[1100];
    const char *home;
    char *result;
    jc_size i;
    int guard;

    jc_sb_init(&c.sb);
    jc_vec_init(&c.seen, sizeof(char *));
    c.a = app->arena;
    c.truncated = 0;

    /* 1. Global instructions. */
    home = jc_home_dir();
    jc_snprintf(path, sizeof(path), "%s/.config/jichi/AGENTS.md", home);
    add_file(&c, path);

    /* 2. Collect cwd and its ancestors up to a git root or the filesystem
     * root, then read them in root -> cwd order. */
    jc_vec_init(&dirs, sizeof(char *));
    jc_snprintf(cur, sizeof(cur), "%s", app->cwd);
    for (guard = 0; guard < JC_RULES_WALK_MAX; guard++) {
        char *d = jc_arena_strdup(app->arena, cur);
        char *slash;
        jc_vec_push(&dirs, &d);

        jc_snprintf(path, sizeof(path), "%s/.git", cur);
        if (jc_is_dir(path)) {
            break; /* stop at the repository root */
        }
        slash = strrchr(cur, '/');
        if (slash == NULL) {
            break;
        }
        if (slash == cur) {
            if (cur[1] == '\0') {
                break; /* already at "/" */
            }
            cur[1] = '\0'; /* collapse to "/" */
        } else {
            *slash = '\0';
        }
    }
    for (i = dirs.len; i > 0; i--) {
        add_dir_rules(&c, *(char **)jc_vec_at(&dirs, i - 1));
    }
    jc_vec_free(&dirs);

    /* 3. Explicit config instruction paths (relative to cwd unless absolute). */
    for (i = 0; i < app->config.instructions.len; i++) {
        const char *s = *(char **)jc_vec_at(&app->config.instructions, i);
        if (s == NULL || s[0] == '\0') {
            continue;
        }
        if (s[0] == '/') {
            add_file(&c, s);
        } else {
            jc_snprintf(path, sizeof(path), "%s/%s", app->cwd, s);
            add_file(&c, path);
        }
    }

    jc_vec_free(&c.seen);
    if (c.sb.len == 0) {
        jc_sb_free(&c.sb);
        return NULL;
    }
    result = jc_arena_strdup(app->arena, c.sb.data);
    jc_sb_free(&c.sb);
    return result;
}
