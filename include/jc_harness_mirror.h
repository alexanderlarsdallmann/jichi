/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_mirror.h - `jichi-harness mirror fetch` and `mirror head`
 * (M1055, H3b): the C89 port of the drive's mirror.sh mirror_fetch +
 * mirror_head.
 *
 * The jichi-harness drive of 2026-10-23, batch H3b. The reference:
 * docs/internal/drive/2026-10-23-harness/harness/mirror.sh, the functions
 * mirror_fetch and mirror_head only (the push is M1056 / H3c and is NOT
 * built here). Design note: docs/internal/drive/2026-10-23-harness/notes/
 * M1055.md.
 *
 * mirror fetch reads the team's clone (and the jichi checkpoint stores)
 * THROUGH git, into the bare mirror only the harness writes. The mirror is
 * the fetch CLIENT; the team's clone is the source. In Mode A there is no
 * container, so the source is read by a local git child, and the surface to
 * neutralise is that child (the M2 set, the empty GIT_CONFIG_GLOBAL/SYSTEM,
 * and -c core.alternateRefsCommand=) plus the mirror-integrity precheck that
 * refuses a mirror the harness did not write or that was altered.
 *
 * Exit classes (the note): a failed precheck is exit 2 with the failed check
 * named, and the fetch does not happen; a team-branches fetch that fails at
 * the transport (a bad source) is exit 1 with the captured git output; a
 * non-fast-forward REFUSAL (no `+`) is reported as a line and tolerated (the
 * other refspecs still applied); a failed kept-attempt fetch is a report
 * line, not fatal.
 */
#ifndef JC_HARNESS_MIRROR_H
#define JC_HARNESS_MIRROR_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"

/* The two forms of `jichi-harness mirror`.
 *
 *   fetch --config <file> <mirror-dir>
 *   head  --config <file> <mirror-dir>
 *
 * `config_path` may be NULL only when the caller has already loaded the
 * config (the dispatch passes the --config path). `mirror_dir` is the bare
 * mirror, absolute or relative. `a` owns every string the command reads; the
 * caller frees it. Returns the process exit code (0 / 1 / 2 as above). */
int jc_harness_mirror_fetch_run(const char *config_path,
                                const char *mirror_dir,
                                struct jc_arena *a);
int jc_harness_mirror_head_run(const char *config_path,
                               const char *mirror_dir,
                               struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_MIRROR_H */
