/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_board.h - a persisted kanban phase board (#7).
 *
 * A project task board persisted to <cwd>/.jichi/board.json: cards move across
 * states (todo -> doing -> done) and carry a lifecycle `phase` (design /
 * implementation / testing / ...), so humans and agents can see which tasks and
 * phases are open vs done. Distinct from the session-ephemeral todo list
 * (jc_todo): the board is durable and shared. Load/save mirror the jc_calib JSON
 * pattern; the render + pure ops are unit-tested. No clock/scheduler (by design).
 */
#ifndef JC_BOARD_H
#define JC_BOARD_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include "jc_str.h"

/* Card state reuses the todo vocabulary: 0 todo, 1 doing, 2 done. */
struct jc_board_card {
    int   id;
    char *title;   /* malloc'd */
    char *phase;   /* malloc'd, or NULL */
    char *note;    /* malloc'd, or NULL */
    int   state;
};

struct jc_board {
    struct jc_vec cards;  /* of struct jc_board_card */
    char *active_phase;   /* malloc'd, or NULL */
    int   next_id;        /* monotonic id allocator */
    /* M533: the board file EXISTED and could not be read or parsed. Distinct
     * from "no board yet", because the two must not be treated alike: an
     * unreadable board used to load as an EMPTY one, silently, and the next
     * `board add` then wrote that empty board over the file. Measured: three
     * cards, one truncated save, one add -- and the file held one card. Save
     * refuses while this is set, so the damaged file survives for a human. */
    int   load_failed;
};

void jc_board_init(struct jc_board *b);
void jc_board_free(struct jc_board *b);

/* Load <cwd>/.jichi/board.json into `b` (already init'd).
 *
 * MISSING => an empty board, JC_OK. UNREADABLE or MALFORMED => an empty board,
 * `b->load_failed = 1`, a warning on stderr, and JC_ERR_PARSE. Callers may
 * ignore the return (all three do); what protects the file is that
 * jc_board_save refuses while the flag is set. */
jc_status jc_board_load(struct jc_board *b, const char *cwd);

/* Persist `b` to <cwd>/.jichi/board.json (creates .jichi). */
jc_status jc_board_save(const struct jc_board *b, const char *cwd);

/* Add a card; returns its new id (>0). title required; phase/note may be NULL. */
int jc_board_add(struct jc_board *b, const char *title, const char *phase,
                 const char *note);

/* Move card `id` to `state` (0/1/2). Returns 1 if found, else 0. */
int jc_board_move(struct jc_board *b, int id, int state);

/* Remove card `id`. Returns 1 if found. */
int jc_board_remove(struct jc_board *b, int id);

/* Set the board's active phase (copied). */
void jc_board_set_active_phase(struct jc_board *b, const char *phase);

/* Parse a state word ("todo"/"doing"/"done", plus pending/in_progress/wip
 * aliases). Returns 0/1/2, or -1 if unrecognised. Pure. */
int jc_board_state_from_str(const char *s);

/* The display word for a state (0/1/2 => "todo"/"doing"/"done"). Pure. */
const char *jc_board_state_word(int state);

/* Render the board as text columns (TODO / DOING / DONE), cards grouped, with
 * the active phase noted. Pure; used by /board + the board subcommand. */
void jc_board_render(const struct jc_board *b, struct jc_sb *out);

/* Render a compact focus block (active phase + the DOING cards) for injection
 * into the system prompt; appends nothing if the board is empty. Pure. */
void jc_board_render_focus(const struct jc_board *b, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_BOARD_H */
