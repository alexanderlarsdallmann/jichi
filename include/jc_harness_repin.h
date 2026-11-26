/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_repin.h - `jichi-harness repin <SHA8>` (M1203, H11, wave 2): the
 * C89 port of the 2026-10-27 hardening-host drive's repin.sh (103 lines).
 * After a landing it rebuilds both binaries at the landed commit in a fresh
 * clone, checks both `--version` stamps, keeps the old pair (backup + cmp),
 * swaps into the drive's bin/ by rename (a running process keeps its inode),
 * then runs the harness doctor and `doctor --live` for the named models with
 * the key by env (never argv) and its 12-char prefix withheld from every
 * output (notes/M1203.md, b1-b24). The reference's every refusal string is
 * kept BYTE-FOR-BYTE; the usage refusal is rc 2, every policy refusal rc 1,
 * success rc 0.
 *
 * The verb runs host-side (the orchestrator's existing repin step, host.
 * repinRun, invokes it). The git children are all local reads (b2/b3/b9/b17)
 * through jc_hp_git_run's credential-less read form (b18). The build / clone
 * / doctor commands are shimmable host.* fields (b7/b8/b19/b20) so the
 * container's stand-ins run where the host's real make/podman forms would.
 * Returns the verb's rc; never exits. */
#ifndef JC_HARNESS_REPIN_H
#define JC_HARNESS_REPIN_H

#include "jc_mem.h"           /* struct jc_arena */

#ifdef __cplusplus
extern "C" {
#endif

/* The `repin` verb (M1203, H11, wave 2). The four command / value parameters
 * except sha are the caller's named flags (NULL = absent); the rest default
 * to the config's host.* fields, then to the code-named defaults (the b7
 * idiom: <host.dir>/... ; host.pushRepo else <host.pushHome>/push.git).
 * `config_path` is the --config value (NULL -> the load refusal, rc 2).
 * Returns:
 *   0  -- the swap completed AND every doctor (harness + each live) reported
 *         a ` 0 problems` summary without a withheld output (b22)
 *   1  -- every named policy refusal (b2-b14) or any not-clean doctor
 *   2  -- the usage refusal (b1: sha absent) and the load refusals
 */
int jc_harness_repin_run(const char *config_path,
                         const char *push_repo, const char *ci_log_dir,
                         const char *build_dir, const char *pins_dir,
                         const char *bin_dir, const char *backup_dir,
                         const char *clone_run, const char *repin_out,
                         const char *harness_doctor, const char *live_doctor,
                         const char *build_run, const char *models,
                         const char *sha, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_REPIN_H */
