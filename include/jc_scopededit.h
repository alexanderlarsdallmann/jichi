/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_scopededit.h - the D6 scoped edit check (M975).
 *
 * A turn-lived mechanism that tracks which paths were merged by worker
 * batches (the "worker-owned set") and enforces a tripwire on the Nth
 * top-level agent edit of such paths, plus detects shell edits to them.
 *
 * API (all take struct jc_app *app first):
 *   void jc_scopededit_turn_start(struct jc_app *app);  -- reset for new turn
 *   void jc_scopededit_owner(struct jc_app *app, const char *path, int task,
 *                            const char *content, size_t len); -- record merge
 *   int  jc_scopededit_path(struct jc_app *app, const char *path, int *task_out);
 *   const char *jc_scopededit_pre_write(struct jc_app *app, const char *path);
 *   void jc_scopededit_post_write(struct jc_app *app, const char *path);
 *   const char *jc_scopededit_post_tool(struct jc_app *app);
 */
#ifndef JC_SCOPEDEDIT_H
#define JC_SCOPEDEDIT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct jc_app;

/* An entry in the per-turn owned-path set. */
struct jc_scopededit_entry {
    char    *path;        /* root-relative path, arena-owned               */
    int      task;        /* 1-based task index that owned it              */
    char    *snapshot;    /* merged-content snapshot (bytes), arena-owned  */
    size_t   snapshot_len;/* snapshot length in bytes                      */
    int      edit_count;  /* number of edits by the top-level agent        */
};

/* The per-turn state (turn-lived, app->scopededit). */
struct jc_scopededit {
    int        n;              /* from app->config.scoped_edit_max          */
    int        count;          /* entries count                             */
    int        capacity;       /* allocated entries capacity                */
    struct jc_scopededit_entry *entries; /* of struct jc_scopededit_entry      */
};

/* Turn start: reset owned-set and counters, re-read N from config.
 * A no-op when N==0 (the check is off). */
void jc_scopededit_turn_start(struct jc_app *app);

/* Record a path as worker-owned (called only on successful merge). */
void jc_scopededit_owner(struct jc_app *app, const char *path, int task,
                         const char *content, size_t len);

/* 1 if path is owned (fill *task_out), else 0. */
int jc_scopededit_path(struct jc_app *app, const char *path, int *task_out);

/* pre_write check: returns NULL if write may proceed, else a STATIC error.
 * Trips from the Nth edit on; N==0 -> always NULL (off); agent_depth!=0 -> always NULL.
 * For a TOP-LEVEL edit of an OWNED path, increments that path's edit counter.
 * The error message MUST contain 'worker-owned path', 'follow-up task', and 'jichi attempts'. */
const char *jc_scopededit_pre_write(struct jc_app *app, const char *path);

/* post_write: after an ALLOWED write of an owned path, re-baseline the snapshot. */
void jc_scopededit_post_write(struct jc_app *app, const char *path);

/* post_tool: after every top-level tool call, detect shell edits to owned paths.
 * Returns NULL if no owned path changed; else a STATIC notice naming changed paths.
 * Emits ONE telemetry event per changed path and re-baselines. */
const char *jc_scopededit_post_tool(struct jc_app *app);

#ifdef __cplusplus
}
#endif

#endif /* JC_SCOPEDEDIT_H */
