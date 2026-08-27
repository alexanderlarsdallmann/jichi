/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_todo.h - the agent's task list (backing the todowrite/todoread tools).
 *
 * One list per SESSION (M606: struct jc_session owns it and the session codec
 * saves and restores it; app->todos points at the live session's list -- until
 * M606 the list lived on jc_app alone and no resume restored it). todowrite
 * replaces it wholesale and todoread renders it. Item content strings are
 * malloc-owned (M199) and the vector is heap-owned; jc_todo_free releases both.
 */
#ifndef JC_TODO_H
#define JC_TODO_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

enum jc_todo_status {
    JC_TODO_PENDING = 0,
    JC_TODO_IN_PROGRESS,
    JC_TODO_DONE
};

struct jc_todo_item {
    char *content;          /* malloc-owned (M199)         */
    int   status;           /* enum jc_todo_status         */
};

struct jc_todo_list {
    struct jc_vec items;    /* of struct jc_todo_item      */
    unsigned long gen;      /* M606: bumped on every replacement, so the
                             * session codec can skip a byte-identical
                             * rewrite -- the jc_history.gen shape       */
};

void jc_todo_init(struct jc_todo_list *t);
void jc_todo_free(struct jc_todo_list *t);

/* M606: drop every item (freeing the content strings) and bump `gen`. The
 * list stays initialised and usable. */
void jc_todo_clear(struct jc_todo_list *t);

/* M606: replace `dst` (an initialised list) with a deep copy of `src`; the
 * copy's `gen` is bumped so a session holding it reads as changed. Backs
 * the session fork. JC_ERR_OOM leaves `dst` empty. */
jc_status jc_todo_copy(struct jc_todo_list *dst, const struct jc_todo_list *src);

/* M606: the WIRE word for a status -- "pending", "in_progress", "completed" --
 * the todowrite schema's enum and the session codec's key. Distinct from the
 * DISPLAY word (jc_todo_status_word) on purpose: the display word changed at
 * M299 and the wire enum did not. Never NULL. */
const char *jc_todo_status_wire(int status);

/* M606: parse a status word leniently -- the wire words, the display words
 * and the board's aliases (`completed`/`complete`/`done`, `in_progress`/
 * `in-progress`/`doing`); anything else, including NULL, is pending. A model
 * writes the status and so does a session file on disk, and both are read
 * leniently by the CLAUDE.md rule. Pure. */
int jc_todo_status_from_wire(const char *s);

/* --- state as a word, not a box (M299) -------------------------------------
 *
 * Reported from use: a model writes `- [ ]` checkbox lists and then does not go
 * back to flip them to `- [x]`, so the list quietly stops being true. Mutating a
 * two-character cell in place is a fiddly edit; writing a sentence with its state
 * in front of it is not. So the rendered list reads
 *
 *     pending:     write the design note
 *     in-progress: extract the pure helper
 *     complete:    measure the current behaviour
 *
 * The wire enum (`pending`/`in_progress`/`completed`) is UNCHANGED -- this is a
 * rendering and input-tolerance change, not a schema break. */

/* The display word for a status: "pending", "in-progress", "complete".
 * Returns a static string; never NULL, even for an out-of-range value. */
const char *jc_todo_status_word(int status);

/* Strip a leading list/state marker from `content` and report the state it
 * implied. Tolerant BY DESIGN: a model will produce `- [ ] foo` out of habit for a
 * long time, so jichi normalises it instead of refusing it. Recognises an optional
 * `- `/`* ` bullet, then either a checkbox (`[ ]`, `[x]`, `[X]`, `[~]`) or a state
 * word followed by `:` (`pending`, `in-progress`, `in_progress`, `incomplete`,
 * `complete`, `completed`, `done`).
 *
 * Returns a pointer into `content` at the first character of the real text. When a
 * state marker was recognised, writes its `enum jc_todo_status` to *status_out and
 * sets *found to 1; otherwise leaves *status_out alone and sets *found to 0, so a
 * caller can prefer the status the model passed explicitly. `content` NULL yields
 * "" with *found 0. Pure; unit-tested. */
const char *jc_todo_strip_marker(const char *content, int *status_out,
                                 int *found);

#ifdef __cplusplus
}
#endif
#endif /* JC_TODO_H */
