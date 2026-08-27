/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_argpath.h - collect the file paths a tool call's arguments declare (M501).
 *
 * WHY THIS EXISTS. M459 made an explicit `--edit-scope` outrank an *inferred*
 * read-only constraint: an operator naming the writable file on the command line
 * should not be overruled by a guess scanned out of prose. It did that by
 * checking a top-level `"path"` argument for two tools by name -- `write_file`
 * and `edit_file`.
 *
 * `apply_patch` carries its paths in an `edits[]` array instead, so it was not
 * exempt: a run told exactly which file to change still refused to change it via
 * the tool a model reaches for when making several edits. Same defect, third
 * tool. `generate_audio`, `generate_image` and `record_audio` write a
 * top-level `"path"` too and were also outside the name list.
 *
 * So the rule stops being a list of tool names (the M295 preference: no growing
 * exception list) and becomes a property of the ARGUMENTS -- every path this call
 * declares is inside the operator's explicit scope.
 *
 * FAIL-CLOSED BY CONSTRUCTION. Returning -1 when there are more paths than the
 * caller can hold is not an error path, it is the safety property: an exemption
 * decided on a TRUNCATED view of a multi-file call could permit a write the
 * operator never scoped. Better to leave the constraint applied and let the
 * operator see the refusal.
 *
 * Pure: reads a parsed cJSON tree, allocates nothing, borrows the strings.
 */
#ifndef JC_ARGPATH_H
#define JC_ARGPATH_H

#ifdef __cplusplus
extern "C" {
#endif
#include "cJSON.h"

/* Collect into `out` (cap entries) every file path `args` declares:
 *   - a top-level string "path"          (write_file, edit_file, generate_audio,
 *                                         generate_image, record_audio, ...)
 *   - each "path" inside the "edits" array (apply_patch)
 * Empty strings are skipped. Pointers are borrowed from the tree.
 *
 * Returns the number collected (0 when the call declares no path at all), or
 * **-1** when more paths exist than `cap` allows -- see the header note: a
 * truncated collection must never be used to widen a permission. */
int jc_argpath_collect(const cJSON *args, const char **out, int cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_ARGPATH_H */
