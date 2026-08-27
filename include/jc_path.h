/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_path.h - workspace path canonicalization and containment (M24).
 *
 * The file tools follow symlinks and accept arbitrary paths. Under an
 * autonomous run (--auto) on an untrusted workspace that is a problem: a model
 * could be steered into reading/writing outside the project (e.g. via a
 * `../../etc/passwd` argument or a symlink that escapes the tree). These helpers
 * provide an opt-in containment fence: canonicalize a path with realpath(),
 * then verify the result is under a canonical workspace root.
 *
 * jc_path_under_root is a pure string-containment predicate (no filesystem
 * access), so it is unit-tested directly. jc_path_resolve / jc_path_in_root wrap
 * realpath() and so touch the filesystem.
 */
#ifndef JC_PATH_H
#define JC_PATH_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Maximum canonical path length handled (bytes, incl. terminator). */
#define JC_PATH_MAX 4096

/* Pure containment check: is the already-canonical `path` lexically inside the
 * already-canonical `root`? True when path == root, or path begins with
 * `root` followed by a '/' (so "/work" contains "/work/x" but NOT "/workother").
 * Both arguments must be absolute and canonical (no "." / ".." / symlinks); this
 * routine does no filesystem access. A NULL/empty root denies everything. */
int jc_path_under_root(const char *root, const char *path);

/* Lexically normalize `path` into `out` (capacity `cap`) for IDENTITY comparison:
 * a relative path is joined against `cwd` (which must be absolute), and "//" and
 * "." segments are collapsed. Pure -- no filesystem access, no symlink
 * resolution, so it is unit-testable offline and cheap enough to call in a loop.
 *
 * Returns JC_ERR_INVALID when `path` contains a ".." segment. Collapsing ".."
 * lexically is wrong across a symlink ("a/link/../b" is not "a/b"), and a caller
 * comparing two paths for identity would then treat two DISTINCT files as one.
 * For the one caller that matters -- the superseded-read dedup, whose false match
 * would elide a read that was never superseded and lose information -- a missed
 * dedup is the strictly cheaper error, so callers fall back to comparing the raw
 * strings. Use jc_path_resolve instead when true canonicalization is required
 * and the syscalls are acceptable (M192).
 */
jc_status jc_path_normalize(const char *cwd, const char *path,
                           char *out, jc_size cap);

/* Canonicalize `path` into `out` (capacity `cap`) via realpath().
 *
 * If `path` does not exist yet (the common case for a write target), the parent
 * directory is canonicalized and the final component re-appended -- this still
 * defeats a symlinked parent while allowing not-yet-created files. Returns:
 *   JC_OK           - *out holds the canonical absolute path
 *   JC_ERR_TOOBIG   - the result would not fit in `cap`
 *   JC_ERR_NOTFOUND - neither the path nor its parent directory resolves
 *   JC_ERR_INVALID  - bad arguments
 *
 * M607: a leaf that is a symlink to a not-yet-existing target resolves to the
 * TARGET (relative targets against the link's directory), through at most 40
 * hops; a cycle or an unreadable link is JC_ERR_NOTFOUND, which every caller
 * treats as "deny". Before M607 such a leaf was re-appended verbatim and a
 * write through it landed wherever the link pointed. */
jc_status jc_path_resolve(const char *path, char *out, jc_size cap);

/* Resolve `path` and report whether it lands under canonical `root`. On any
 * resolve failure the path is treated as NOT contained (fail closed). `root`
 * must already be canonical (see jc_path_resolve at startup). */
int jc_path_in_root(const char *root, const char *path);

#ifdef __cplusplus
}
#endif
#endif /* JC_PATH_H */
