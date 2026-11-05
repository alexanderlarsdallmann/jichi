/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_handoff.h - the handoff section, the C89 port of the drive's
 * handoff.py (M1058, H4 batch): what the previous run left the next one,
 * built from the previous run's own records (its stream.jsonl, its meta.txt,
 * its filed notes). The jichi-harness drive of 2026-10-23.
 *
 * Design note (the contract -- the fourteen numbered behaviours and the six
 * CHANGED lines): docs/internal/drive/2026-10-23-harness/notes/M1058.md.
 * Reference (read line by line; its comments name the incidents):
 * docs/internal/drive/2026-10-23-harness/harness/handoff.py.
 *
 * The `run` command is the caller: it builds the section BEFORE the spawn
 * (behaviour 12's order -- the value is known for the meta's handoff= line),
 * writes the section to runs/<N>/handoff.md (always -- empty when there is no
 * previous run or any drop condition), writes the filed reason to
 * runs/<N>/handoff.err (always, empty on success), embeds the section into
 * the prompt, and appends team_head_after=<short sha> to its own meta AFTER
 * the child exits (CHANGED 1: the run writes the line its own ended() will
 * read; the reference's loop.sh wrote it from the mirror).
 *
 * THE FAIL-OPEN CONTRACT (behaviour 1, CHANGED 5): the build returns
 * JC_OK with an EMPTY section on every drop condition (no previous run, a
 * missing or unreadable stream.jsonl, a notes directory that fails to list,
 * an out-of-memory build) and never fails the run -- a run must start
 * without the section rather than not start. The reason goes to `err_out`
 * in the reference's filed shape ("handoff: <reason>" + newline; the
 * reference's "handoff.py: <ExcType>: <msg>" keeps its observable: the
 * reason). `out_buf` must hold the finished section plus the 6000-byte cut
 * (JC_HARNESS_HANDOFF_OUT_CAP); when the section exceeds 6000 BYTES it is
 * cut at the byte boundary (a partial UTF-8 sequence at the cut is dropped),
 * back to the last newline, with the marker line appended (behaviour 10).
 */
#ifndef JC_HARNESS_HANDOFF_H
#define JC_HARNESS_HANDOFF_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"  /* jc_status */

/* The 6000-byte cap (behaviour 10) plus the cut marker line and a byte of
 * room; the caller's out_buf must be at least this large. */
#define JC_HARNESS_HANDOFF_OUT_CAP 6144

/* Build the "What the previous run left you" section from the previous
 * run's records.
 *
 * `prev_run_dir` is the previous run's directory (runs/<PREV>/); NULL or an
 * empty string means there is no previous run (a first run, a run number
 * of 0, a non-numeric run name) and the section is empty with no reason
 * (behaviour 2). Otherwise the builder reads <prev_run_dir>/stream.jsonl
 * (line by line; a line that is not valid JSON is SKIPPED -- behaviour 1:
 * a stream cut mid-line by a crash must not drop the section),
 * <prev_run_dir>/meta.txt (k=v lines, first '='; missing or unreadable
 * yields an EMPTY meta -- behaviour 4/13 fall-throughs, not a failure), and
 * the names of <prev_run_dir>/notes/ (a notes directory that fails to list
 * IS a drop; an unreadable NOTE is not -- behaviour 9).
 * `records_dir` is the ABSOLUTE records path the section's notes line
 * states ("the full text is in `<records_dir>/runs/<PREV>/notes/`",
 * CHANGED 2); the basename of `prev_run_dir` is the <PREV> it interpolates.
 *
 * On success the finished section (with the 6000-byte cut applied when it
 * fired) is in `out_buf` and `*out_bytes` its length in bytes; empty means
 * "no section" (a first run). `err_out` receives the filed reason line
 * ("handoff: <reason>" + newline) on a drop and stays empty on success; the
 * caller writes it to handoff.err verbatim (the reference's
 * 2> "$R/handoff.err").
 *
 * Returns JC_OK always (the fail-open is in the VALUE, behaviour 1/14 --
 * the child's rc is the run's rc, with or without the section). */
jc_status jc_harness_build_handoff(const char *prev_run_dir, const char *prev_meta,
                                   const char *prev_notes_dir, const char *records_dir,
                                   char *out_buf, size_t out_cap,
                                   char *err_out, size_t err_cap,
                                   int *out_bytes);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_HANDOFF_H */
