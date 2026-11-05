/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rules.c - instruction-file discovery (see jc_rules.h). */

#include "jc_rules.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_path.h"
#include "jc_log.h"

#include <string.h>
#include <sys/stat.h>

#define JC_RULES_MAX   (32 * 1024)
#define JC_RULES_WALK_MAX 64 /* guard against pathological directory chains */

struct rules_ctx {
    struct jc_sb     sb;
    struct jc_vec    seen; /* of char*: paths already included */
    struct jc_arena *a;
    int              truncated;
    const char      *wsroot; /* canonical workspace root for symlink guard */
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
    struct stat st;

    if (c->truncated || already_seen(c, path)) {
        return;
    }
    /* M1108 (D-33): a non-regular file here -- a FIFO/device/socket would
     * block jc_read_file's fopen until the run's deadline; a directory is
     * skipped the same way without ever being read (M1108-b: the WARN says
     * so, true for every non-regular shape). This is the single choke
     * point for the two
     * routes M1020's add_dir_rules lstat guard never saw: the global
     * ~/.config/jichi/AGENTS.md and the explicit config `instructions`
     * paths. Following stat (not lstat), so a symlink that resolves to a
     * regular file still reads (M992/M1020 semantics); a missing or broken
     * link gives stat != 0 and falls through to jc_read_file, which returns
     * NOTFOUND and the skip below stays silent -- behaviour unchanged. */
    if (stat(path, &st) == 0 && !S_ISREG(st.st_mode)) {
        jc_logf(JC_LOG_WARN, "rules: skipping %s: not a regular file -- skipped without reading", path);
        return;
    }
    /* M1109 (D-35): read at most JC_RULES_MAX + 1 LEADING bytes -- the head
     * the cut below keeps. The +1 is the UTF-8 boundary byte the M516
     * backstep needs at data[rem]. Pre-fix the whole file (up to the 64 MB
     * JC_READ_FILE_MAX) sat on the session arena for the process lifetime. */
    if (jc_read_file_bounded(path, &data, &len, c->a,
                             (jc_size)JC_RULES_MAX + 1, 1) != JC_OK) {
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

/* Try AGENTS.md in `dir`, then CLAUDE.md as a fallback.
 * Regular files are read directly (pre-M992 behavior).
 * Symlinks are guarded: they are only read if their resolved target is
 * under the canonical workspace root (M992 D-34 protection).
 * Other file types (FIFO, device, socket) are skipped. */
static void add_dir_rules(struct rules_ctx *c, const char *dir)
{
    char path[1100];
    char resolved[JC_PATH_MAX];
    struct stat st;

    /* First try AGENTS.md */
    jc_snprintf(path, sizeof(path), "%s/AGENTS.md", dir);
    if (jc_file_exists(path)) {
        /* lstat to determine file type without following symlinks */
        if (lstat(path, &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                /* Regular file: read without resolve/under-root check (pre-M992) */
                add_file(c, path);
            } else if (S_ISLNK(st.st_mode)) {
                /* Symlink: resolve and check if target is under root (M992 guard) */
                if (jc_path_resolve(path, resolved, sizeof(resolved)) != JC_OK ||
                    !jc_path_under_root(c->wsroot, resolved)) {
                    jc_logf(JC_LOG_WARN, "rules: skipping %s: symlink target outside the workspace (or unresolvable)", path);
                } else {
                    add_file(c, path);
                }
            } else {
                jc_logf(JC_LOG_WARN, "rules: skipping %s: not a regular file or symlink", path);
            }
        }
        return;
    }
    /* Then try CLAUDE.md */
    jc_snprintf(path, sizeof(path), "%s/CLAUDE.md", dir);
    if (jc_file_exists(path)) {
        /* lstat to determine file type without following symlinks */
        if (lstat(path, &st) == 0) {
            if (S_ISREG(st.st_mode)) {
                /* Regular file: read without resolve/under-root check (pre-M992) */
                add_file(c, path);
            } else if (S_ISLNK(st.st_mode)) {
                /* Symlink: resolve and check if target is under root (M992 guard) */
                if (jc_path_resolve(path, resolved, sizeof(resolved)) != JC_OK ||
                    !jc_path_under_root(c->wsroot, resolved)) {
                    jc_logf(JC_LOG_WARN, "rules: skipping %s: symlink target outside the workspace (or unresolvable)", path);
                } else {
                    add_file(c, path);
                }
            } else {
                jc_logf(JC_LOG_WARN, "rules: skipping %s: not a regular file or symlink", path);
            }
        }
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
    char wsroot[JC_PATH_MAX];
    jc_size i;
    int guard;

    jc_sb_init(&c.sb);
    jc_vec_init(&c.seen, sizeof(char *));
    c.a = app->arena;
    c.truncated = 0;

    /* Compute the canonical workspace root for the symlink guard.
     * The walk stops at the nearest .git directory (or file, for worktrees).
     * The workspace root is the PARENT of .git (the repository directory).
     * If found, wsroot is the realpath of the parent of .git; otherwise,
     * wsroot is the realpath of app->cwd. On any resolve failure wsroot
     * stays empty, which FAILS CLOSED: an empty root denies every
     * discovered SYMLINKED workspace file (jc_path_under_root rejects an
     * empty root by design), so an unresolvable cwd means no symlinked
     * rules/glossary files are loaded. Regular files are read without
     * this guard, preserving pre-M992 behavior. */
    {
        char *root_path = NULL;
        char *slash;
        char tmp_cur[1100];
        strcpy(tmp_cur, app->cwd);
        for (guard = 0; guard < JC_RULES_WALK_MAX; guard++) {
            /* Check for .git directory or file (worktree/submodule) */
            jc_snprintf(path, sizeof(path), "%s/.git", tmp_cur);
            if (jc_file_exists(path)) {
                root_path = tmp_cur;  /* use the PARENT directory, not .git itself */
                break;
            }
            /* Walk up */
            slash = strrchr(tmp_cur, '/');
            if (slash == NULL) {
                break;
            }
            if (slash == tmp_cur) {
                if (tmp_cur[1] == '\0') {
                    break; /* already at "/" */
                }
                tmp_cur[1] = '\0';
            } else {
                *slash = '\0';
            }
        }
        /* root_path points to parent of .git if found, else use app->cwd */
        if (root_path) {
            if (jc_path_resolve(root_path, wsroot, sizeof(wsroot)) != JC_OK) {
                /* Resolve failed: fall back to raw app->cwd canonicalized */
                if (jc_path_resolve(app->cwd, wsroot, sizeof(wsroot)) != JC_OK) {
                    wsroot[0] = '\0'; /* fail closed */
                }
            }
        } else {
            if (jc_path_resolve(app->cwd, wsroot, sizeof(wsroot)) != JC_OK) {
                wsroot[0] = '\0'; /* fail closed */
            }
        }
    }
    c.wsroot = wsroot;

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

        /* Stop at the repository root: the nearest directory holding a `.git`
         * of either kind -- a directory in a checkout, a FILE ("gitdir: ...")
         * in a linked worktree or a submodule. Testing for a directory alone
         * let the walk climb out of every worktree and read the rules files
         * above it (M884). */
        jc_snprintf(path, sizeof(path), "%s/.git", cur);
        if (jc_file_exists(path)) {
            break;
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
