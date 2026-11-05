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

/* M761: ONE EDIT, applied to the board as it is ON DISK.
 *
 * A session loads the board once, when it starts, and every edit used to be
 * applied to that copy and written back whole. So two processes -- a run and a
 * `jichi board add` in another shell, or two runs -- lost each other's cards,
 * and both handed out the same id. jc_board_edit takes <cwd>/.jichi/board.lock
 * (jc_lock_take; the file is kept, and is not the board's to commit), reloads
 * the file into `b`, applies `e`, saves and lets go. A lock busy for 5 s is
 * waited out with a warning, and the edit then goes ahead unlocked.
 *
 * Returns 1 when the edit was applied and saved; 0 when it was refused (no card
 * with that id, no title, a state word it does not know), with the board on
 * disk unchanged; JC_BOARD_SAVE_FAILED when it was applied but could not be
 * saved -- a board that exists and cannot be read is never overwritten (M533),
 * so a caller must not report such an edit as done. An add sets e->id. */
enum jc_board_edit_kind {
    JC_BOARD_ADD = 1,
    JC_BOARD_MOVE,
    JC_BOARD_REMOVE,
    JC_BOARD_PHASE
};
struct jc_board_edit {
    enum jc_board_edit_kind kind;
    const char *title;  /* add */
    const char *phase;  /* add: the card's; PHASE: the board's (NULL clears) */
    const char *note;   /* add */
    int id;             /* move, remove; set by add */
    int state;          /* move: 0/1/2 */
};
#define JC_BOARD_SAVE_FAILED (-1)
int jc_board_edit(struct jc_board *b, const char *cwd, struct jc_board_edit *e);

/* Replace `b` with the board on disk, for a reader that must see what other
 * processes wrote (the board tool's `list`). Returns jc_board_load's status. */
jc_status jc_board_reload(struct jc_board *b, const char *cwd);

/* Parse a state word ("todo"/"doing"/"done", plus pending/in_progress/wip
 * aliases). Returns 0/1/2, or -1 if unrecognised. Pure. */
int jc_board_state_from_str(const char *s);

/* The display word for a state (0/1/2 => "todo"/"doing"/"done"). Pure. */
const char *jc_board_state_word(int state);

/* Render the board as text columns (TODO / DOING / DONE), cards grouped, with
 * the active phase noted. Pure; used by /board + the board subcommand. */
void jc_board_render(const struct jc_board *b, struct jc_sb *out);

/* M932 (the 2026-10-02 branch's M899, the demos drive's N37): what the `board` tool returns. A 185-card board
 * rendered whole after every action cost ~30,000 tokens a call.
 *   _counts: one line, "Board: N todo, N doing, N done[; active phase: P]".
 *   _card:   one card with its state; returns 0 (appends nothing) for no such id.
 *   _open:   the active phase, TODO and DOING in full, DONE as a count.
 * All pure. People keep jc_board_render (/board, `jichi board`). */
void jc_board_render_counts(const struct jc_board *b, struct jc_sb *out);
int jc_board_render_card(const struct jc_board *b, int id, struct jc_sb *out);
void jc_board_render_open(const struct jc_board *b, struct jc_sb *out);

/* M932 (the branch's M900): the board as a cJSON tree -- the file's shape (with_counts 0, what
 * jc_board_save writes) or the read-only view `jichi board --output json`
 * prints (with_counts 1 adds {"todo","doing","done"} counts). The caller
 * cJSON_Delete()s it; NULL on allocation failure. */
struct cJSON *jc_board_to_json(const struct jc_board *b, int with_counts);

/* Render a compact focus block (active phase + the DOING cards) for injection
 * into the system prompt; appends nothing if the board is empty. Pure. */
void jc_board_render_focus(const struct jc_board *b, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_BOARD_H */
