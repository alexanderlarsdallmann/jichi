/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_inhist.h - the TUI's typed-input history, kept across runs (M763).
 *
 * Up-arrow and Ctrl-R recalled only the lines of the current session; they were
 * lost on exit. HARDENING.md section 7.5 deferred keeping them because of one
 * question: should a line with a secret in it be written to disk at all?
 *
 * THE DECISION: no, as far as jichi can tell that it has one. A line is kept in
 * the session's memory as before, and is written only when it is keepable:
 *   - not empty, and a single line (the editor recalls only those, M363);
 *   - at most JC_INHIST_LINE_MAX bytes;
 *   - holding no secret jichi has registered (an API key it resolved, M24/M608);
 *   - holding no token shaped like a common credential: a known key prefix
 *     followed by at least 16 key characters, `password=` and its relatives, or
 *     a `Bearer` token.
 * The shapes are a heuristic, and cannot see a password typed as a plain word.
 * The rule for the user is the same as for any shell history: do not type a
 * secret into a prompt. `inputHistory: false` turns the file off.
 *
 * The file is ~/.jichi.d/input_history: 0600, one entry per line, outside every
 * workspace (so no rollback or snapshot reaches it). Each keepable line is
 * appended as it is submitted, so a crash loses nothing typed. It is trimmed
 * when loaded: past 2 x JC_INHIST_MAX lines it is rewritten, atomically, with
 * the newest JC_INHIST_MAX.
 */
#ifndef JC_INHIST_H
#define JC_INHIST_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JC_INHIST_MAX      500    /* entries recalled, and kept on disk */
#define JC_INHIST_LINE_MAX 2048   /* a longer line is not written */

/* ~/.jichi.d/input_history. */
void jc_inhist_path(char *buf, jc_size cap);

/* Non-zero if `line` has a token shaped like a credential (see above). Pure. */
int jc_inhist_looks_secret(const char *line);

/* Non-zero if `line` may be written (every rule above). Reads the redaction
 * registry; otherwise pure. */
int jc_inhist_keepable(const char *line);

/* Read `path` and hand the newest JC_INHIST_MAX entries to `add`, oldest first.
 * Trims the file as described above. Returns the number handed over; 0 for a
 * missing or unreadable file, which is not an error. */
int jc_inhist_load(const char *path, void (*add)(void *user, const char *line),
                   void *user);

/* Append one line to `path` (created 0600, with its directory 0700). Returns
 * JC_OK, or JC_ERR_IO. Writes nothing for a line that is not keepable. */
jc_status jc_inhist_append(const char *path, const char *line);

#ifdef __cplusplus
}
#endif
#endif /* JC_INHIST_H */
