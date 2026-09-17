/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_plan.h - the plan artifact: `.jichi/PLAN.md`, written in plan mode,
 * reconciled at run end (M631, the argumentation program's fourth slice).
 *
 * Plan mode was a read-only fence plus a prose request: the plan lived in the
 * conversation, where compaction can drop it, and nothing compared it with
 * what was then done. DESIGN_INPUT.md is the INPUT twin -- a human's design,
 * "authoritative for planning, not a licence to ignore the code". This is the
 * output twin. The model writes the plan through ONE tool (`write_plan`) into
 * ONE path, in the shape the learner's own registers use -- markdown with
 * headings a script can count -- so the same person can read both:
 *
 *   ## Claim       what will change, and why (one paragraph)
 *   ## Rejected    >= 1 alternative, each with the reason it lost ("--" or em dash)
 *   ## Falsifier   the observation that would show the plan wrong
 *   ## Not-goals   what this deliberately leaves alone
 *   ## Touches     the files the work expects to change, one per line
 *
 * A plan is a PREDICTION, not a fence: `--edit-scope` fences. At run end the
 * files the run actually wrote (the envelope's `wrote`, root-relative) are
 * compared with `## Touches`, and the difference -- drift -- is reported in the
 * reach footer and the journal, which is what makes the plan a calibration
 * record rather than a promise. Pure: line scanning over a string, no YAML, no
 * model, no filesystem except the one loader. */
#ifndef JC_PLAN_H
#define JC_PLAN_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

#define JC_PLAN_PATH ".jichi/PLAN.md"

struct jc_plan {
    int has_claim;
    int has_rejected;
    int has_falsifier;
    int has_notgoals;
    int has_touches;
    int n_rejected;          /* bullets under ## Rejected carrying a reason */
    int n_rejected_bare;     /* bullets there WITHOUT a reason separator   */
    struct jc_vec touches;   /* of char* (arena), root-relative, as written */
};

void jc_plan_init(struct jc_plan *p);
void jc_plan_free(struct jc_plan *p);

/* Parse PLAN.md text. Always fills *p; never fails on shape -- the caller
 * asks jc_plan_missing what is wrong. Strings land on `a`. */
void jc_plan_parse(const char *text, struct jc_plan *p, struct jc_arena *a);

/* The first thing a plan lacks, as the message write_plan returns and the
 * learner reads, or NULL when the plan is complete. Order: Claim, Rejected
 * (present, >= 1 bullet, each with a reason), Falsifier, Not-goals, Touches
 * (present, >= 1 path). */
const char *jc_plan_missing(const struct jc_plan *p);

/* Render the five sections from their parts -- what write_plan writes.
 * `rejected` and `touches` are newline- or "; "-separated lists; bullets are
 * added when absent. Owned string, or NULL on OOM. */
char *jc_plan_render(const char *claim, const char *rejected,
                     const char *falsifier, const char *notgoals,
                     const char *touches);

/* Drift: the paths in `wrote` (root-relative, of char*) that `## Touches` did
 * not name. Paths under .jichi/ are never drift (the plan file itself, the
 * records). Appends char* (arena) to `out`; returns the count. Pure. */
int jc_plan_drift(const struct jc_plan *p, const struct jc_vec *wrote,
                  struct jc_vec *out, struct jc_arena *a);

/* Load <root>/.jichi/PLAN.md into *p (root = the workspace root the
 * envelope's `wrote` paths are relative to). 1 when a plan was present and parsed,
 * 0 when absent (p is left empty). */
int jc_plan_load(const char *root, struct jc_plan *p, struct jc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* JC_PLAN_H */
