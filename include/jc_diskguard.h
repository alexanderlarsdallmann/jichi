/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_diskguard.h - the disk-space FENCE (M813, plan step P4b).
 *
 * A run that fills its disk corrupts the very records a post-mortem needs -- the
 * session, the transcripts, the preserved worktrees. This stops the run BEFORE
 * that, while there is still room to write those records.
 *
 * It is a FENCE, not a cap (CLAUDE.md's distinction): it stays on even in a
 * measurement run, because -- like `connect` and the dead-stream fence -- it can
 * only fire on a condition that is never a legitimate result: the disk is actually
 * nearly full. A cap that fires manufactures a different answer; a full disk does
 * not.
 *
 * TWO INVARIANTS, both from the measurement lesson (M805):
 *   - UNKNOWN NEVER STOPS. When `jc_disk_space` cannot measure (a failed statvfs),
 *     the guard carries on. "I could not measure" is not "the disk is full", and a
 *     fence that stopped on the unknown would refuse to run wherever measuring is
 *     unsupported. This is the else-branch-must-say-unknown rule.
 *   - THROTTLED BY TIME, NOT BY CALL COUNT. One `zig build` writes gigabytes in a
 *     single tool call, so a per-call check would miss it; and statvfs on every
 *     call is waste. The guard measures at most once per JC_DISKGUARD_INTERVAL_MS
 *     at a tool boundary, but the caller can `force` an unthrottled check at the
 *     moments that matter -- run start, before a batch allocates worktrees, before
 *     each preservation.
 *
 * The decision (`_below`) and the throttle (`_due`) are PURE, so the invariants are
 * unit-testable without a filesystem or a clock. `_check` is the stateful wrapper
 * that measures and latches.
 */
#ifndef JC_DISKGUARD_H
#define JC_DISKGUARD_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* At a tool boundary the guard measures at most this often (ms). */
#define JC_DISKGUARD_INTERVAL_MS 10000.0

struct jc_diskguard {
    double floor_mb;     /* stop when free MiB < this; <= 0 disables the dimension */
    double floor_files;  /* stop when free inodes < this; <= 0 disables it         */
    double last_ms;      /* monotonic ms of the last measurement, or -1 (never)    */
    int    tripped;      /* latched: once it fires, it stays fired                 */
    char   why[160];     /* the reason, set when it trips                          */
};

/* Set the floors and the "never measured" state. */
void jc_diskguard_init(struct jc_diskguard *g, double floor_mb,
                       double floor_files);

/* PURE. Is free space below a LIVE floor? `free_mb`/`free_files` < 0 mean UNKNOWN
 * (jc_disk_space's sentinel) and never trip; a floor <= 0 disables that dimension.
 * Returns 1 to stop. */
int jc_diskguard_below(double free_mb, double free_files,
                       double floor_mb, double floor_files);

/* PURE. Should the guard measure now? `force` => yes; otherwise yes when at least
 * JC_DISKGUARD_INTERVAL_MS has elapsed since `last_ms` (or it never has). */
int jc_diskguard_due(double last_ms, double now_ms, int force);

/* Stateful. When due (or forced), measure the filesystem holding `path` via
 * jc_disk_space and update the guard. Returns 1 to STOP -- free is below a floor --
 * else 0. UNKNOWN never stops. Once it has returned 1 it latches and keeps
 * returning 1. `now_ms` is a monotonic clock (jc_now_millis). Both floors <= 0, or
 * a NULL guard, means disabled: always 0. Fills `g->why` on the trip. */
int jc_diskguard_check(struct jc_diskguard *g, const char *path, int force,
                       double now_ms);

#ifdef __cplusplus
}
#endif
#endif /* JC_DISKGUARD_H */
