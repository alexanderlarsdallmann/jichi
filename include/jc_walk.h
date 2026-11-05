/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_walk.h - shared walker API for repository walk (D-27/D-28/D-29/D-30).
 *
 * The three repository walkers (index walk, repomap walk, ls walk) all had
 * the same defect: they used jc_is_dir(), which is stat()-based and FOLLOWS
 * symlinks, so a workspace with a symlink to an outside directory would
 * escape and enumerate files outside the workspace (D-28/D-30). Combined
 * with the lack of an entry budget and a visited set for directories, a
 * symlink loop would also allocate without bound (D-29).
 *
 * This module provides ONE shared walker that:
 *   - lstats each entry, so it knows what is really a symlink
 *   - checks the resolved target is under the walk root (string prefix)
 *   - records (dev,ino) of REAL directories to avoid hardlink loops
 *   - respects entry and depth bounds
 *
 * C89, zero warnings under -std=c89 -pedantic -Wall -Wextra -Wstrict-prototypes
 * -Wmissing-prototypes. */
#ifndef JC_WALK_H
#define JC_WALK_H

#include "jc_vec.h"
#include <sys/types.h>

/* A (dev,ino) pair, matching struct stat's st_dev and st_ino.
 * Do NOT use <stdint.h> — C89 rule; cast to %lu. */
struct jc_walk_devino {
    unsigned long dev;
    unsigned long ino;
};

/* Walk context, created once for a walk and passed down to classify. */
struct jc_walk_ctx {
    char root_canon[4096];        /* realpath of the walk root */
    struct jc_vec visited;        /* of struct jc_walk_devino */
    int depth;                    /* current depth (caller threads) */
    int max_depth;                /* stop when depth >= max_depth */
    int max_entries;              /* entry budget */
    int n_entries;                /* entries seen so far */
    int capped;                   /* set when budget exhausted */
    int skipped_outside;          /* symlinks that resolved outside root */
};

/* Classification result. */
enum jc_walk_kind {
    JC_WALK_SKIP,   /* not to be entered/processed */
    JC_WALK_DIR,    /* a real directory, or a link whose target is a dir */
    JC_WALK_FILE    /* a regular file */
};

/* Initialise the context.
 *   - root must exist as a directory or returns -1
 *   - root_canon is the realpath of root (for under-root check)
 *   - visited vec is initialised, all other fields zeroed
 * Returns 0 on success, -1 on failure. */
int jc_walk_ctx_init(struct jc_walk_ctx *ctx, const char *root, int max_depth,
                     int max_entries);

/* Free resources in the context. */
void jc_walk_ctx_free(struct jc_walk_ctx *ctx);

/* Classify an entry under a known-visited directory.
 *   - full: absolute or relative path to the entry
 *   - depth: caller threads this; if >= max_depth, directories are skipped
 * Returns JC_WALK_SKIP, JC_WALK_DIR, or JC_WALK_FILE. */
enum jc_walk_kind jc_walk_classify(struct jc_walk_ctx *ctx, const char *full,
                                   int depth);

#endif /* JC_WALK_H */
