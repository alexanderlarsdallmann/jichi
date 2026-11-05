/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_run.h - `jichi-harness run` (M1057, H4): one lead run in Mode A,
 * the C89 port of the drive's run-lead.sh (the container half dropped, the
 * record and key-delivery half kept, re-expressed over the jichi primitives
 * H0 linked: the spawn, the env scrub, the redactor).
 *
 * The jichi-harness drive of 2026-10-23, batch H4. Design note (the contract:
 * the fourteen numbered behaviours and the nine driver cells R1-R9):
 * docs/internal/drive/2026-10-23-harness/notes/M1057.md. Ported reference (read
 * line by line; its comments name the incidents): docs/internal/drive/
 * 2026-10-23-harness/harness/run-lead.sh.
 *
 * `run` is a COMMAND: it owns the whole one-run lifecycle -- the pre-run
 * refusals, the model-key read-from-file-and-env-delivery, the prompt
 * assembly, the spawn of the pinned jichi with the full flag set (caps off,
 * fences on), the records channel (stream, stderr, journal, telemetry, meta,
 * the config copy), the inbox notes consumed per run, and the exit classes --
 * and returns the process exit code.
 *
 * WHAT IT DELIBERATELY DOES NOT DO (the note's IS-NOT / CHANGED lines, kept
 * out on purpose): the handoff body (M1058 ports handoff.py and fills the
 * "What the previous run left you" section); the post-run record collection
 * (sessions/attempts/lmstudio copy, def-355, a follow-up); the drive's outer
 * loop (H5); a container (Mode A, behaviour 3, def-336); a gateway-up probe
 * (def-357, H5's loop concern).
 */
#ifndef JC_HARNESS_RUN_H
#define JC_HARNESS_RUN_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_mem.h"  /* struct jc_arena */

/* Run `jichi-harness run --config <file> --tenant <name> --run <N>`: start one
 * lead run against the tenant's workdir and carry its records.
 *
 * `config_path` is the harness configuration (NULL is a named exit-2 refusal:
 * run needs the tenant table, the drive identity and the model-key reference).
 * `tenant` selects one tenant by name; `run` is the run number (it names
 * runs/<N>/ under the drive's dir).
 *
 * Exit classes (the note's behaviour 12, the driver cells R1-R9):
 *   2  a pre-run refusal (a missing/bad --config, no tenant named, a tenant
 *      without a workdir, the drive dir not set, a missing/unreadable
 *      promptFile, the model-key reference not set, a missing/unreadable
 *      model-key file, a missing bin, a tenant without a home) -- ALWAYS
 *      before any write or spawn, so a refused run changes nothing.
 *   1  a spawn that could not start (the pinned jichi would not exec) --
 *      meta.txt is written with start= and rc=<the spawn error> appended.
 *   the child's exit code  a run that completed -- meta.txt carries rc=<N>
 *      and the code is returned to the caller (a signal death is non-zero;
 *      it is never masked to 0).
 *
 * The model key is read from the file the configuration names at the moment
 * the run starts and handed to jichi through the environment under the fixed
 * name JICHI_API_KEY -- never on a command line, never set in the harness's
 * own process environment, never printed. `a` must outlive the call; the
 * caller creates and frees it. */
int jc_harness_run(const char *config_path, const char *tenant,
                   const char *run, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_RUN_H */
