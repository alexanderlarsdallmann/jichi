/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_push.h - `jichi-harness mirror push` (M1056, H3c): the C89 port of
 * the drive's mirror.sh mirror_push + pre-push-keyscan.sh + pre-push-sizecheck.sh
 * (the push half of the mirror; the fetch/head half is M1055, jc_harness_mirror).
 *
 * THE MIRROR IS THE PUSH CLIENT. The remote is the rehearsal stand-in (the
 * drive's GitLab over HTTPS in the real loop, never touched by a rehearsal).
 * In Mode A there is no container, so the two pre-push gates (the key scan,
 * the size check) are C logic the harness runs BEFORE the push, not hooks in
 * the mirror the agent could edit (the note's Decision H3c). The push runs
 * the SAME mirror-integrity precheck the fetch runs (M1055, the five checks,
 * git-free, exit 2 naming the check), then the URL guard (the effective push
 * URL `git remote get-url --push origin` must equal the stored copy's
 * `remote.origin.url` byte-exactly, exit 2 naming both on a mismatch, before
 * any ls-remote/push child), then the key scan and size check (each exit 1
 * naming a path or a size, never a secret), then the push by the stored
 * absolute URL (never the `origin` name), never --force, never a master
 * refspec.
 *
 * Exit classes (the note's behaviour 14): precheck or URL-guard or master-
 * guard failure = exit 2, the check named; a key-scan or size-check refusal =
 * exit 1 (the push did not happen, a path or size named, no secret); a push
 * that git refused (non-FF, a bad remote) = exit 1 with git's first 200 bytes;
 * a push that landed = exit 0. A scan that CANNOT run (fail-closed) = exit 1,
 * the missing secret or object named -- never exit 0.
 */
#ifndef JC_HARNESS_PUSH_H
#define JC_HARNESS_PUSH_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"

/* `mirror push --config <file> <mirror-dir>`. `config_path` may be NULL only
 * when the caller has already loaded the config (the dispatch passes the
 * --config path). `mirror_dir` is the bare mirror. `a` owns every string the
 * command reads; the caller frees it. Returns the process exit code (0 / 1 /
 * 2 as above). */
int jc_harness_mirror_push_run(const char *config_path,
                               const char *mirror_dir,
                               struct jc_arena *a);

/* def-350: the PUSH_MAX_TIPS / PUSH_MAX_EXCL values THIS build was compiled
 * with (24/48 in every default build; the smoke-push-boundary stage lowers
 * them to 1/1). print_version reports them under #ifdef JC_PUSH_BOUNDARY
 * and the driver's gate 2 greps the values, so a marker without the lowered
 * values cannot pass the gate. */
void jc_harness_push_caps(int *tips, int *excl);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_PUSH_H */
