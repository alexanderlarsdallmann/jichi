/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_init.h - `jichi-harness init` (M1053, H2): lay out the drive's
 * (or a rehearsal's) runtime directory, the C89 port of the drive's
 * setup-dir.sh.
 *
 * The jichi-harness drive of 2026-10-23, batch H2. `init` is the FIRST
 * harness command that runs git (the M1049 "never runs git" line for H0 is
 * lifted here, and only here): real `git` children through the shared spawn
 * primitive, every one neutralised the way jichi's own git tools run (the M2
 * `-c` flags, the secret-env scrub, no credential cache consulted). In a
 * rehearsal everything is offline and local: a local bare stand-in instead
 * of the remote, random canaries instead of the key and the token.
 *
 * Design note: docs/internal/drive/2026-10-23-harness/notes/M1053.md (the
 * numbered port of setup-dir.sh and its CHANGED lines). This module is a
 * COMMAND: it owns the whole init run -- the layout, the git children, the
 * `init (<mode>): <part>` report lines and the final `done: <dir>` -- and it
 * returns the process exit code (0 complete layout, 2 a usage refusal BEFORE
 * any part is written, 1 a git step failed mid-layout).
 *
 * WHAT IT DELIBERATELY DOES NOT DO (the note's CHANGED lines, kept out):
 *   - no `DIR/harness/` archive (CHANGED 4): the C port IS the harness;
 *   - no rehearsal container-prefix / unit-name rewrite (CHANGED 6): Mode A
 *     has no containers (def-336, lands with Mode D);
 *   - no pre-push hooks installed (CHANGED 11, 13): `init` lays out the
 *     repos without arming them (def-337, H3).
 *
 * The never-delete rule (setup-dir.sh line 18): a part that already exists is
 * reported ("left as it is") and left alone; init only creates.
 */
#ifndef JC_HARNESS_INIT_H
#define JC_HARNESS_INIT_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_mem.h"  /* struct jc_arena */

/* Run the `init` command: lay out DIR for MODE (drive or rehearsal),
 * reading the drive's identity (the branch prefix, the records-commit
 * name/email, the model-key reference) from the harness config at
 * `config_path` (NULL = no --config: a drive's missing identity is a NAMED
 * exit-2 refusal; rehearsal needs no secrets). `from` is the source repo
 * (`--from`; required, absent -> exit 2), `jichi_bin` the binary to pin
 * (`--jichi-bin`; absent -> the `jichi` next to the running harness).
 *
 * Prints the `init (<mode>): <part>` lines and the final `done: <dir>`.
 * Returns the exit code: 0 on a complete layout, 2 on a usage refusal
 * (an unknown mode, a missing DIR/--from, a missing jichi-bin, a drive with
 * no model-key reference -- always BEFORE any part is written), 1 when a
 * git step fails mid-layout (reported; nothing deleted, ever).
 *
 * `a` must outlive the call; all arena allocations are released with it. */
int jc_harness_init_run(const char *config_path, const char *mode,
                        const char *dir, const char *from,
                        const char *jichi_bin, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_INIT_H */
