/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_doctor.h - health-check report aggregation for the `doctor` subcommand.
 *
 * `jichi doctor` runs a series of setup checks (config, models, API keys,
 * server reachability, MCP/LSP servers, git/snapshots) and prints a pass/warn/
 * fail checklist with fix hints, so a user can tell at a glance whether their
 * environment is ready. This module is the pure, unit-tested core: it collects
 * check results, renders the checklist, and computes the process exit code. The
 * actual probing lives in main.c's run_doctor.
 */
#ifndef JC_DOCTOR_H
#define JC_DOCTOR_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_str.h"

enum jc_doctor_status {
    JC_DOC_OK   = 0,
    JC_DOC_WARN = 1,
    JC_DOC_FAIL = 2
};

struct jc_doctor {
    struct jc_vec items; /* of struct jc_doctor_item */
};

void jc_doctor_init(struct jc_doctor *d);
void jc_doctor_free(struct jc_doctor *d);

/* Record one check. `label` is the headline; `detail` (may be NULL/"") is an
 * indented follow-up line, typically a fix hint. Both are copied. */
void jc_doctor_add(struct jc_doctor *d, int status, const char *label,
                   const char *detail);

/* Number of recorded checks with the given status. */
int jc_doctor_count(const struct jc_doctor *d, int status);

/* Process exit code: 1 if any check FAILED, else 0 (warnings do not fail). */
int jc_doctor_exit_code(const struct jc_doctor *d);

/* Render the checklist + a summary line into `out`. `color` enables ANSI color;
 * `unicode` selects ✓/!/✗ glyphs vs ASCII ok/!/x. */
void jc_doctor_render(const struct jc_doctor *d, int color, int unicode,
                      struct jc_sb *out);

/* Render a machine-readable JSON report into `out`:
 * {"ok":N,"warn":N,"fail":N,"exit":N,"checks":[{"status":"ok|warn|fail",
 *   "label":"...","detail":"..."}...]}. For agent-driven verify loops. */
void jc_doctor_render_json(const struct jc_doctor *d, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_DOCTOR_H */
