/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_walk.c - shared walker implementation (D-27/D-28/D-29/D-30).
 *
 * C89, zero warnings under -std=c89 -pedantic -Wall -Wextra -Wstrict-prototypes
 * -Wmissing-prototypes. */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "jc_walk.h"
#include "jc_platform.h"
#include "jc_path.h"
#include "jc_snprintf.h"
#include "jc_mem.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* A simple linear search in the visited set to check if a (dev,ino) is already
 * present. The set is small (a few hundred directories in practice), and the
 * walk is bounded by max_entries, so O(n) is acceptable. */
static int devino_seen(struct jc_walk_ctx *ctx, unsigned long dev, unsigned long ino)
{
    jc_size i;
    for (i = 0; i < ctx->visited.len; i++) {
        struct jc_walk_devino *p = (struct jc_walk_devino *)jc_vec_at(&ctx->visited, i);
        if (p->dev == dev && p->ino == ino) {
            return 1;
        }
    }
    return 0;
}

/* Add a (dev,ino) pair to the visited set. Returns 0 on success, -1 on OOM. */
static int devino_push(struct jc_walk_ctx *ctx, unsigned long dev, unsigned long ino)
{
    struct jc_walk_devino pair;
    pair.dev = dev;
    pair.ino = ino;
    return jc_vec_push(&ctx->visited, &pair);
}

    /* Bounded prefix check: is `path` under `root`?
     * Requirements:
     *   - path starts with root
     *   - if path is longer than root, the next byte is '/'
     *   - this prevents "/tmp/foo" from matching "/tmp/foobar"
     * Returns 1 if under root, 0 otherwise. */
    static int under_root(const char *root, size_t rootlen, const char *path)
    {
        if (path == NULL || path[0] == '\0') {
            return 0;
        }
        if (strncmp(path, root, rootlen) != 0) {
            return 0;
        }
        /* Exact match, or next byte is '/' */
        if (path[rootlen] == '\0' || path[rootlen] == '/') {
            return 1;
        }
        /* If root ends with '/', allow immediate next char to be anything */
        if (rootlen > 0 && root[rootlen - 1] == '/') {
            return 1;
        }
        return 0;
    }

int jc_walk_ctx_init(struct jc_walk_ctx *ctx, const char *root, int max_depth,
                     int max_entries)
{
    char canon[PATH_MAX];
    struct stat st;

    if (ctx == NULL || root == NULL || root[0] == '\0') {
        return -1;
    }

    /* Zero everything first. */
    memset(ctx, 0, sizeof(*ctx));

    /* Resolve the root to its canonical path. This must be a directory. */
    if (realpath(root, canon) == NULL) {
        return -1;
    }
    
    /* realpath may add a trailing slash - strip it */
    {
        size_t len = strlen(canon);
        if (len > 1 && canon[len - 1] == '/') {
            canon[len - 1] = '\0';
        }
    }

    /* Confirm it is a directory. */
    if (stat(canon, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1;
    }

    /* Copy the canonical root (bounded to our buffer size). */
    {
        size_t len = strlen(canon);
        if (len > 1 && canon[len - 1] == '/') {
            canon[len - 1] = '\0';
        }
        jc_snprintf(ctx->root_canon, sizeof(ctx->root_canon), "%s", canon);
    }

    /* Don't seed the root in visited - let classify add it on first encounter.
     * This allows classify to properly detect hardlink loops when the same path
     * (like a symlink to root) is classified multiple times. */
    jc_vec_init(&ctx->visited, sizeof(struct jc_walk_devino));

    ctx->max_depth = max_depth;
    ctx->max_entries = max_entries;
    ctx->depth = 0;
    ctx->n_entries = 0;
    ctx->capped = 0;
    ctx->skipped_outside = 0;

    return 0;
}

void jc_walk_ctx_free(struct jc_walk_ctx *ctx)
{
    if (ctx != NULL) {
        jc_vec_free(&ctx->visited);
    }
}

enum jc_walk_kind jc_walk_classify(struct jc_walk_ctx *ctx, const char *full,
                                   int depth)
{
    char resolved[PATH_MAX];
    char fullres[PATH_MAX];
    struct stat st;
    unsigned long dev, ino;

    /* If we've already hit the entry budget, skip everything. */
    if (ctx->capped) {
        return JC_WALK_SKIP;
    }

    /* Count this entry. */
    ctx->n_entries++;
    if (ctx->n_entries > ctx->max_entries) {
        ctx->capped = 1;
        return JC_WALK_SKIP;
    }

    /* lstat the entry to see what it is without following links. */
    if (lstat(full, &st) != 0) {
        return JC_WALK_SKIP;
    }

    /* If it's a directory, apply the depth bound. */
    if (S_ISDIR(st.st_mode)) {
        if (depth >= ctx->max_depth) {
            return JC_WALK_SKIP;
        }

        /* Resolve `full` to an absolute path to handle relative roots correctly. */
        if (realpath(full, fullres) == NULL) {
            return JC_WALK_SKIP;
        }

        /* Check if this directory is under the walk root (using resolved path). */
        if (!under_root(ctx->root_canon, strlen(ctx->root_canon), fullres)) {
            ctx->skipped_outside++;
            return JC_WALK_SKIP;
        }

        dev = (unsigned long)st.st_dev;
        ino = (unsigned long)st.st_ino;

        /* Special case: if this is exactly the root path (depth=0), return JC_WALK_DIR. */
        if (depth == 0 && strcmp(ctx->root_canon, fullres) == 0) {
            return JC_WALK_DIR;
        }

        /* Check if we've already visited this (dev,ino) (hardlink loop). */
        if (devino_seen(ctx, dev, ino)) {
            return JC_WALK_SKIP;
        }

        /* Add to visited set and return JC_WALK_DIR. */
        if (devino_push(ctx, dev, ino) != 0) {
            return JC_WALK_SKIP;
        }
        return JC_WALK_DIR;
    }

    /* If it's a symlink, we need to resolve it and check where it points. */
    if (S_ISLNK(st.st_mode)) {
        /* Resolve the symlink. If resolution fails, skip. */
        if (realpath(full, resolved) == NULL) {
            return JC_WALK_SKIP;
        }

        /* Check if the resolved path is under the walk root. */
        if (!under_root(ctx->root_canon, strlen(ctx->root_canon), resolved)) {
            ctx->skipped_outside++;
            return JC_WALK_SKIP;
        }

        /* Now stat the resolved target to see what it really is. */
        if (stat(resolved, &st) != 0) {
            return JC_WALK_SKIP;
        }

        /* If it's a directory after resolution, apply the same checks. */
        if (S_ISDIR(st.st_mode)) {
            if (depth >= ctx->max_depth) {
                return JC_WALK_SKIP;
            }

            dev = (unsigned long)st.st_dev;
            ino = (unsigned long)st.st_ino;

            if (depth == 0 && strcmp(ctx->root_canon, resolved) == 0) {
                /* Check if already visited first. */
                if (devino_seen(ctx, dev, ino)) {
                    return JC_WALK_SKIP;
                }
                if (devino_push(ctx, dev, ino) != 0) {
                    return JC_WALK_SKIP;
                }
                return JC_WALK_DIR;
            }

            /* Also handle the case where the resolved path is the root (even at non-zero depth). */
            if (strcmp(ctx->root_canon, resolved) == 0) {
                /* Check if already visited first. */
                if (devino_seen(ctx, dev, ino)) {
                    return JC_WALK_SKIP;
                }
                if (devino_push(ctx, dev, ino) != 0) {
                    return JC_WALK_SKIP;
                }
                return JC_WALK_DIR;
            }

            if (devino_seen(ctx, dev, ino)) {
                return JC_WALK_SKIP;
            }

            if (devino_push(ctx, dev, ino) != 0) {
                return JC_WALK_SKIP;
            }
            return JC_WALK_DIR;
        }

        /* If it's a regular file, return JC_WALK_FILE. */
        if (S_ISREG(st.st_mode)) {
            return JC_WALK_FILE;
        }

        /* Other types (fifo, device, socket) -> skip. */
        return JC_WALK_SKIP;
    }

    /* If it's a regular file, return JC_WALK_FILE. */
    if (S_ISREG(st.st_mode)) {
        return JC_WALK_FILE;
    }

    /* Other types (fifo, device, socket, etc.) -> skip. */
    return JC_WALK_SKIP;
}
