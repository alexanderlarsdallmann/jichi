/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_drive.h - `jichi-harness drive` (M1059, H5): the outer loop
 * until the drive's end, the C89 port of the drive's loop.sh (the jichi-
 * harness drive of 2026-10-23, batch H5).
 *
 * The reference, read line by line (its comments name the incidents):
 * docs/internal/drive/2026-10-23-harness/harness/loop.sh. Mode A (the plan
 * §7.1): the harness and the agent run as the same user, so the container
 * half is dropped (the one `podman kill --signal INT` becomes a SIGINT to the
 * run's process group, CHANGED 1) and the loop, the snapshot and the
 * three-quick-failure pause are kept, re-expressed in C.
 *
 * Design note (the contract -- the sixteen numbered behaviours, the ten
 * CHANGED lines and the nine driver cells D1-D8, D6d/D6e):
 * docs/internal/drive/2026-10-23-harness/notes/M1059.md.
 *
 * The loop's own exit classes (behaviour 15):
 *   1  the drive's deadline does not parse (a broken SETTING, the reference's
 *      own class: loop.sh exits 1 on `date -d` failure, BEFORE the start line)
 *   2  a usage refusal (no --config, no --tenant named, a tenant the config
 *      does not name or without a workdir, --tick 0) -- always before the
 *      first log line and before any run starts (nothing is created)
 *   0  a loop that ENDED (the STOP file, the deadline, or --once done),
 *      regardless of the runs' rcs (the runs' rcs are in the log and the
 *      meta, as values)
 *   130 the loop itself SIGINTed (the operator's Ctrl-C; the default handler
 *      -- the loop installs NO handler of its own, behaviour 9's handler is
 *      the RUN's)
 */
#ifndef JC_HARNESS_DRIVE_H
#define JC_HARNESS_DRIVE_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_mem.h"  /* struct jc_arena */

/* Run `jichi-harness drive --config <file> --tenant <name> [--once]
 * [--tick <s>]`: the drive's outer loop, until the drive's end (the STOP
 * file, the deadline, or one run when `once` is true).
 *
 * `config_path` is the harness configuration (NULL is a named exit-2
 * refusal). `tenant` selects one tenant by name (the run the loop starts);
 * an unknown tenant or one without a workdir is the RUN's existing refusal
 * (exit 2, nothing run), reused here. `once` ends the loop after the first
 * run completes (the port of rehearse.sh's STOP touch). `tick_override` is
 * the --tick value in seconds; 0 is a named refusal (CHANGED 7: a 0 tick is
 * a cap-on-housekeeping, and it is not the reference's value), a NULL or
 * negative/empty override falls to the config's `drive.tick` (read
 * leniently, default 1200, the reference's `TICK=${ZD_TICK:-1200}`).
 *
 * The loop spawns `run` as ITS OWN binary (`/proc/self/exe`, the M1053
 * idiom) in a new process group, captures the run's stdout/stderr into
 * `<dir>/harness.log` (CHANGED 9), polls every 10 s for the tick (save_team
 * + a `partial` snapshot) and the deadline (a SIGINT to the run's group,
 * once, CHANGED 1 / behaviour 8), and at each run's end does save_team + the
 * `run N: team head <sha>` line (the MIRROR's main, CHANGED 6 -- the run's
 * own `team_head_after=` is NOT re-appended) + a `final` snapshot + the
 * three-quick-failure pause (the 600 s in 10-s slices, each checked against
 * the deadline and the STOP file, CHANGED 8). The snapshot commits to
 * `<dir>/recs` (the M2 set + empty GIT_CONFIG_GLOBAL/SYSTEM, an EXPLICIT
 * git identity from the config's drive.gitName/drive.gitEmail) and its push
 * is the CHANGED 2 NO-OP (def-360): a named log line, no push, never
 * --force, never to master.
 *
 * `a` must outlive the call; the caller creates and frees it. Returns the
 * process exit code (0 / 1 / 2 above; 130 is the default handler's, this
 * function never returns it). */
int jc_harness_drive(const char *config_path, const char *tenant,
                     int once, int tick_override, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_DRIVE_H */
