/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_landlock.h - a Linux Landlock WRITE fence (M831, closes O9).
 *
 * A spawn_parallel write child's FILE TOOLS are fenced to its worktree, but its
 * SHELL can write wherever the process can. A lexical guard on the command text is
 * not a fence (a variable, `cd ..`, a written-then-run script, a symlink all reach
 * the same place). Landlock is: an unprivileged, inherited, IRREVOCABLE filesystem
 * restriction in the Linux kernel (>= 5.13). Applied in the forked write child
 * before its agent loop, it confines writes -- for the child and every process it
 * starts, shell included -- to the paths granted here.
 *
 * Linux only, by nature (a Linux LSM); the query returns 0 elsewhere, so the caller
 * runs the child unfenced and says so (never silently). */
#ifndef JC_LANDLOCK_H
#define JC_LANDLOCK_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The kernel's Landlock ABI version -- >= 1 when a write fence can be applied, 0
 * when Landlock is unavailable (not Linux, not built into the kernel, or disabled).
 * Never negative. Higher ABIs add rights (2: REFER, 3: TRUNCATE); the fence handles
 * exactly the ones the running kernel supports. */
int jc_landlock_abi(void);

/* Apply an irrevocable WRITE fence to THIS process and every descendant: the
 * write/remove/make rights (and REFER/TRUNCATE where the kernel has them) are
 * allowed ONLY beneath the `n_dirs` directories in `dirs`, and WRITE_FILE only on
 * the `n_files` files in `files` (e.g. "/dev/null" -- without it `2>/dev/null`
 * breaks). READ and EXECUTE are never restricted, so the child reads freely and
 * already-open descriptors (its pipes, its log) keep working (Landlock checks at
 * open). A dir/file in the lists that cannot be opened is SKIPPED -- it grants
 * nothing and cannot widen the fence -- so a missing optional cache does not abort
 * it; the caller ensures the essential paths (the worktree) exist. Returns JC_OK
 * once the process is restricted, JC_ERR_DENIED when Landlock is unavailable
 * (abi 0), JC_ERR_IO if a syscall failed. On any error the process is NOT
 * restricted, and the caller decides whether to proceed unfenced. */
jc_status jc_landlock_write_fence(const char *const *dirs, int n_dirs,
                                  const char *const *files, int n_files);

/* M852: the same, and on JC_ERR_IO from a directory's rule, *failed_dir is that
 * directory's index in `dirs` (-1 when the failure was elsewhere, or none). A
 * directory that does not open is still SKIPPED, as above -- it grants nothing, so
 * a missing shellFenceAllow entry leaves the fence intact (measured 2026-10-01).
 * The failure this reports is the kernel refusing a rule for a path that DID open:
 * a regular file listed as a directory (directory rights on a file are EINVAL). */
jc_status jc_landlock_write_fence_ex(const char *const *dirs, int n_dirs,
                                     const char *const *files, int n_files,
                                     int *failed_dir);

/* M852 (N36): 1 when a shell command's output reads like a refused write --
 * "Permission denied", "AccessDenied", "access denied", "Operation not
 * permitted", case-insensitively. Pure. Advice, not a verdict: an ordinary
 * permission error on a READ matches as well. */
int jc_landlock_permission_error(const char *output);

/* M1018 E-02: byte-packing helper for kernel interface structures.
 * Packs an unsigned long into 8 bytes in little-endian order with zero-
 * extension of the high 4 bytes on 32-bit platforms. The kernel expects
 * __u64 in little-endian, so bytes[0..3] hold the value and bytes[4..7]
 * must be zero when the source is a 32-bit unsigned long. */
void jc_landlock_pack_u64(unsigned char *dst8, unsigned long v);

#ifdef __cplusplus
}
#endif
#endif /* JC_LANDLOCK_H */
