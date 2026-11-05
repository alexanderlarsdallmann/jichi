/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_doctor.h - `jichi-harness doctor` (M1052, H1b): the offline
 * report on the drive's OWN configuration (the file the M1051 loader parses).
 *
 * Ported from the drive's live doctor
 * (docs/internal/drive/2026-10-23-harness/harness/doctor-live.sh, the
 * `jichi --config <file> doctor` spelling, "never prints the key") and from
 * jichi's own doctor's output shape (src/util/jc_doctor.c: the
 * `%d ok, %d warning(s), %d problem(s)` summary line, exit 1 on a problem,
 * and the `--output json` form `{"ok":N,"warn":N,"fail":N,"exit":E,
 * "checks":[...]}`).
 *
 * WHAT THIS IS, AND IS NOT (the design note's split):
 *   IS: the doctor command, run against a harness config by path: it loads
 *       the config with the M1051 loader (check 1 is the loader's own verdict),
 *       walks the six remaining checks, and renders the report in jichi's
 *       doctor shape. A missing `--config` is a PROBLEM (exit 1), not the
 *       exit-2 not-built class.
 *   IS NOT: reading a secret's CONTENTS (that is H4, "at the moment of use").
 *       For modelKeyFile / gitTokenFile doctor prints the reference's NAME and
 *       PATH and decides ok/warn/fail with access()/stat() only -- it NEVER
 *       opens a secret file and NEVER prints its contents. A path is not a
 *       secret; the key's value is.
 *   IS NOT: --live (a real model request is H4's, and needs the key, which H1b
 *       never reads); init (H2 creates the dir doctor reports on); git (H3);
 *       the jichi config's own schema (jichi's, checked by `jichi doctor`).
 */
#ifndef JC_HARNESS_DOCTOR_H
#define JC_HARNESS_DOCTOR_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"   /* jc_status, jc_size */
#include "jc_mem.h"        /* struct jc_arena */

/* Run `jichi-harness doctor` against the harness config at `config_path`.
 *
 * - `config_path` is NULL when no `--config` was given: doctor still RUNS --
 *   it reports a single fail ("no configuration: pass --config <file>"),
 *   renders the summary and (when asked) the JSON form, and returns 1. A
 *   broken or missing config is a REPORTABLE problem (exit 1), never a
 *   rejected invocation (the exit-2 not-built class is main.c's, and doctor
 *   deliberately differs from it in exactly this one place).
 * - `output_json` is non-zero for `doctor --output json`: the report is then
 *   the JSON form (which parses as a single document), not the text form.
 * - The report is printed to stdout.
 *
 * On JC_OK from the loader doctor walks its seven checks in the design note's
 * order, collecting (status, label, detail) triples, and returns
 * `fail > 0 ? 1 : 0` (the jc_doctor_exit_code rule): a warning does NOT fail
 * doctor (a set-but-absent reference is a warn; an absent reference is a fail,
 * because an absent reference is what "stopped before its request").
 *
 * All configuration strings are arena-owned; `a` must outlive the call's use.
 * The CALLER owns the arena (creates and frees it) -- doctor does not free `a`
 * before returning (consistent with jc_harness_config.h: a outlives the config's
 * use, the caller frees). */
int jc_harness_doctor_run(const char *config_path, int output_json,
                          struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_DOCTOR_H */
