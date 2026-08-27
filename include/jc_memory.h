/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_memory.h - persistent agent memory (.jichi/memory.md).
 *
 * A small, durable note store the agent maintains across sessions: facts about
 * the project, conventions, decisions, or user preferences worth remembering.
 * It is loaded at startup and injected into the system prompt (like the rules
 * files), and the agent appends to it with the `remember` tool. Stored as a
 * plain markdown bullet list at <cwd>/.jichi/memory.md, so the user can read and
 * edit it directly (or delete entries with edit_file).
 */
#ifndef JC_MEMORY_H
#define JC_MEMORY_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

struct jc_app; /* jc_app.h */
struct jc_sb;  /* jc_str.h  */

/* Largest memory block injected into the system prompt. */
#define JC_MEMORY_MAX (8 * 1024)

/* Load <cwd>/.jichi/memory.md into an arena-owned string (bounded to
 * JC_MEMORY_MAX, most-recent tail kept), or NULL when there is no memory file
 * / it is empty. M143: when the file exceeds the budget a warning is logged --
 * the file is intact on disk, but the oldest notes are silently NOT loaded
 * into the prompt, which should never be silent. */
char *jc_memory_load(struct jc_app *app);

/* The memory file's on-disk size in bytes (0 when absent). M143: lets the
 * remember tool and doctor say when the file has outgrown the injection
 * budget instead of quietly dropping old notes from the prompt. */
/* Reload the notes into app->memory, freeing the previous copy (M199). Use this
 * rather than assigning jc_memory_load's result directly: the result is
 * malloc-owned, and every `remember` call reloads. */
void jc_memory_refresh(struct jc_app *app);

long jc_memory_file_size(struct jc_app *app);

/* Append `note` as a bullet to <cwd>/.jichi/memory.md, creating the file (and the
 * .jichi directory) if needed. The note is normalized to a single line. A note
 * already present is not duplicated. On success *was_new (if non-NULL) is set to
 * 1 when the note was added, 0 when it already existed. */
jc_status jc_memory_add(struct jc_app *app, const char *note, int *was_new);

/* Correct existing memory (M78, the learning loop's "correct", not just "teach"):
 * drop every bullet line whose text contains `match`, and — when `replacement`
 * is non-NULL/non-empty — append the corrected note. Rewrites
 * <cwd>/.jichi/memory.md and refreshes app->memory. *changed (if non-NULL) gets
 * the number of lines removed plus 1 when a replacement was added; 0 means the
 * match wasn't found (no write). A NULL/absent memory file is a no-op. */
jc_status jc_memory_correct(struct jc_app *app, const char *match,
                            const char *replacement, int *changed);

/* --- pure helpers (unit-tested) --- */

/* Pure core of jc_memory_correct: copy `content` to `out`, dropping every "- "
 * bullet whose text contains `match`; then, if `replacement` is non-NULL and
 * non-empty and something was dropped, append "- <replacement>\n" unless that
 * exact bullet is already present. Returns the number of lines removed plus 1
 * if a replacement was appended (0 if `match` matched nothing). */
int jc_memory_apply_correction(const char *content, const char *match,
                               const char *replacement, struct jc_sb *out);

/* Normalize `note` into `out` (cap bytes): collapse all whitespace runs
 * (including newlines/tabs) to single spaces and trim the ends. Returns the
 * length written. */
jc_size jc_memory_clean_note(const char *note, char *out, jc_size cap);

/* 1 if `content` already contains the exact bullet line "- <note>". */
int jc_memory_has_line(const char *content, const char *note);

#ifdef __cplusplus
}
#endif
#endif /* JC_MEMORY_H */
