/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lineno.h - format text with a line-number gutter (for read_file).
 *
 * Pure helper: append a `cat -n`-style numbered view of `text` to `out`. Line
 * numbers are absolute (1-based) so a sliced read still shows real file lines.
 * The gutter is display-only — the file itself has no line numbers, so
 * exact-string edits (edit_file/apply_patch) are unaffected.
 */
#ifndef JC_LINENO_H
#define JC_LINENO_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

/* Append lines [start_line, start_line+max_lines) of `text` to `out`, each as
 * "<6-wide number>\t<line>\n". `start_line` is 1-based (values < 1 clamp to 1);
 * `max_lines` <= 0 means "to the end". Returns the number of lines emitted, and
 * (when non-NULL) writes the file's total line count to *total_lines. */
/* Escape control characters in `name` into `out` C-style (\n, \r, \t, \xNN),
 * returning the number of bytes written (excluding the NUL), or 0 if `cap` is 0.
 * Truncates safely.
 *
 * M200: a filename may legally contain a newline, and the line-oriented output
 * that list_files hands the model uses newline as its record separator -- so
 * `evil\nplanted.txt` rendered as TWO entries, one of which does not exist. The
 * model would then try to read a file that was never there, and a directory
 * jichi did not author (a cloned repo, an unpacked archive) could inject a
 * plausible-looking entry into tool output the model trusts. Escaping keeps the
 * format's one-record-per-line invariant and tells the truth about the name.
 * Pure; unit-tested in tests/test_lineno.c. */
jc_size jc_escape_ctrl(const char *name, char *out, jc_size cap);

int jc_format_numbered(const char *text, int start_line, int max_lines,
                       struct jc_sb *out, int *total_lines);

/* Count the lines in `text` the way jc_format_numbered counts them: one per
 * segment, with a trailing newline NOT creating a phantom final line. `len`
 * bounds the scan; a NUL inside ends it, matching the formatter's strchr/strlen
 * view of the same bytes.
 *
 * M594: read_file caps its OUTPUT at readMaxBytes but reads the whole file, so
 * the capped buffer's line count is not the file's. Reporting the first as the
 * second told a model "file has 4027 lines" about a 12,509-line file, and it
 * spent the rest of the turn looking for content the tool had just declared
 * absent. The true count is one pass over bytes already in memory. */
int jc_count_lines(const char *text, jc_size len);

#ifdef __cplusplus
}
#endif
#endif /* JC_LINENO_H */
