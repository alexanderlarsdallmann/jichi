/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_notify.h - `jichi-harness notify` (M1152, batch H8): deliver a
 * host-gate verdict, the C89 port of the 2026-10-27 hardening-host drive's
 * gate-note.sh.
 *
 * Design note (the contract: the eleven numbered behaviours B1-B11):
 * docs/internal/drive/2026-11-05-harness-port/notes/M1152.md. Ported reference
 * (read line by line; this driver holds B1-B7, B9-B10): docs/internal/drive/
 * 2026-10-27-hardening-host/gate-note.sh.
 *
 * `notify` is a COMMAND: it owns the whole one-verdict delivery -- the note in
 * the drive's inbox (and, when a run is LIVE, the inject of the note body
 * through that run's control socket), the line in the gate home's digest.md,
 * and the config-named operator hook -- and returns the process exit code. It
 * is a PRINTER and a MESSENGER: it carries no verdict logic of its own
 * (behaviour 11), so a new verdict word costs nothing here.
 *
 * WHAT IT DELIBERATELY DOES NOT DO (the note's IS-NOT / CHANGED lines, kept out
 * on purpose): the watch.out file (behaviour 8 -- its content moves to a stdout
 * line the caller can log); notify-send (the hook is a config-named command,
 * behaviour 6, CHANGED); the withdrawal digest lines (H9a's).
 */
#ifndef JC_HARNESS_NOTIFY_H
#define JC_HARNESS_NOTIFY_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_mem.h"  /* struct jc_arena */

/* Run `jichi-harness notify --config <file> [--inbox <dir>] <SHA8> <TEXT>
 * [ATTACH]`: deliver one host-gate verdict to its two readers.
 *
 * `config_path` is the harness configuration (NULL is a named exit-2 refusal:
 * notify needs the drive dir and the two host fields). `inbox` is the scratch
 * inbox (NULL = the drive's own <dir>/inbox); when set, the note carries the
 * REHEARSAL prefix and the inject is suppressed (behaviours 1, 3). `sha8`,
 * `text` and `attach` are the positionals (attach NULL when absent).
 *
 * Exit classes (the note's behaviour 10, the driver's cell 7):
 *   2  a pre-write refusal (a missing/bad --config, a missing SHA8 or TEXT,
 *      an unreadable dir, a gate home that cannot be created) -- ALWAYS before
 *      any note / digest line / hook, so a refused notify changes nothing.
 *   0  the verdict was delivered (the note and the digest line were written
 *      and the hook was run or absent). The inject's rc and the hook's rc are
 *      NEVER fatal: a failed inject is recorded in the stdout line only, and a
 *      failed hook is logged, never fatal (behaviours 5, 6).
 *
 * `a` must outlive the call; the caller creates and frees it. */
int jc_harness_notify(const char *config_path, const char *inbox,
                      const char *sha8, const char *text,
                      const char *attach, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_NOTIFY_H */
