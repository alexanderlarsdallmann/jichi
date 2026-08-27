/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_progress.h - the learner's progress record (C5, M174).
 *
 * `grade --record` (M173b) appends one JSON line per graded attempt to the
 * per-workspace `.jichi/progress.jsonl`. This module is the READ side plus the
 * single owner of the line format: scan the file's text for one spec's
 * standing (pure), render an aligned listing row (pure -- shared by the
 * `assignments` subcommand and the TUI `/assignments` so both show the same
 * table), and append a record line (thin I/O). The file is the learner's own:
 * append-only by us, plain JSONL, editable/deletable by them -- a malformed
 * line is skipped, never an error.
 */
#ifndef JC_PROGRESS_H
#define JC_PROGRESS_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* One spec's standing, folded over every matching record line. */
struct jc_progress {
    int attempts;  /* recorded grade lines for this spec           */
    int passed;    /* 1 iff any recorded line has passed:true      */
    int best_pct;  /* max pct across the matching lines, else 0    */
};

/* The file name component after the last '/'; p itself when it has none. */
const char *jc_progress_base(const char *p);

/* Fold the progress file's text (may be NULL) into one spec's standing.
 * Records match on the basename of their "spec" field vs the basename of
 * spec_name, so `grade docs/assignments/00-hello.md --record` from the root
 * and `grade 00-hello.md --record` from inside the directory both count. */
void jc_progress_scan(const char *jsonl, const char *spec_name,
                      struct jc_progress *out);

/* Render one aligned listing row (no indent, no newline):
 * name, phase, points, status [ (+solution) ]. phase may be NULL, points 0 =
 * unset, prog may be NULL (no progress file). */
void jc_progress_row(const char *name, const char *phase, int points,
                     int has_solution, const struct jc_progress *prog,
                     char *buf, jc_size cap);

/* The matching header row for the table above. */
void jc_progress_row_header(char *buf, jc_size cap);

/* Append one record line to <dir>/.jichi/progress.jsonl (creating .jichi/ if
 * needed). hints < 0 omits the field (the headless `grade` is stateless and
 * does not know it; the TUI does). Returns JC_OK iff the line was written. */
/* M502: one spec's hint usage, folded over the hint log. */
struct jc_hints {
    int pulls;     /* how many rungs were pulled (lines for this spec) */
    int max_rung;  /* the deepest rung reached, 0 when none           */
};

/* Append one hint pull to <dir>/.jichi/hints.jsonl (creating .jichi/ if
 * needed).
 *
 * WHY A SEPARATE FILE, and not a row in progress.jsonl. Every reader of the
 * progress file treats a line as AN ATTEMPT WITH A VERDICT -- jc_progress_scan
 * counts it in `attempts` and reads its `passed` -- so a hint row there would
 * show up as a graded attempt, and one with `passed:false` would read as a
 * failure. That is the same reasoning M412 used to refuse
 * `grade --expect-fail --record`: a record whose meaning differs from the
 * file's meaning poisons the file. A separate sink also makes the course's
 * promise -- "hints are free and never penalised" -- true BY CONSTRUCTION
 * rather than by everyone remembering not to score it.
 *
 * The 74 shipped specs, the scaffold glossary and CURRICULUM.md have all said
 * "free, and recorded" since M174; until M502 nothing wrote the record, so a
 * teacher could not see that a learner needed all three rungs. */
jc_status jc_progress_hint_append(const char *dir, const char *spec, int rung);

/* Fold the hint log's text (may be NULL) into one spec's hint usage. Matches on
 * basename, exactly like jc_progress_scan. Pure. */
void jc_progress_hints_scan(const char *jsonl, const char *spec_name,
                            struct jc_hints *out);

jc_status jc_progress_append(const char *dir, const char *spec, int passed,
                             int pct, int tests_run, int tests_failed,
                             int hints);

#ifdef __cplusplus
}
#endif
#endif /* JC_PROGRESS_H */
