/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_board.c - the `board` tool (#7): read/update the persisted kanban
 * phase board on jc_app.board (<cwd>/.jichi/board.json). Unlike the ephemeral
 * todo list, the board is durable and shared, so every mutation is saved to
 * disk. Main-agent only (a subagent must not stomp the shared board). Readonly
 * flag off => permission-gated like any mutating tool. */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_board.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_json.h"

#include <stdlib.h>
#include <string.h>

static cJSON *board_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props = cJSON_GetObjectItem(s, "properties");
    cJSON *req = cJSON_GetObjectItem(s, "required");
    cJSON *action = cJSON_CreateObject();
    cJSON *enumv = cJSON_CreateArray();
    tu_schema_string(s, "title", "Card title (for add)", 0);
    tu_schema_string(s, "phase",
        "Lifecycle phase, e.g. design/implementation/testing (add / "
        "set_phase)", 0);
    tu_schema_string(s, "note", "Optional short note (for add)", 0);
    tu_schema_bool(s, "all",
        "list: true also lists the done cards (by default they are counted)", 0);
    cJSON_AddStringToObject(action, "type", "string");
    cJSON_AddItemToArray(enumv, cJSON_CreateString("list"));
    cJSON_AddItemToArray(enumv, cJSON_CreateString("add"));
    cJSON_AddItemToArray(enumv, cJSON_CreateString("move"));
    cJSON_AddItemToArray(enumv, cJSON_CreateString("remove"));
    cJSON_AddItemToArray(enumv, cJSON_CreateString("set_phase"));
    cJSON_AddItemToObject(action, "enum", enumv);
    cJSON_AddStringToObject(action, "description",
        "list | add | move | remove | set_phase");
    cJSON_AddItemToObject(props, "action", action);
    /* id (move/remove) + state (move) as numbers/strings. */
    {
        cJSON *id = cJSON_CreateObject();
        cJSON *st = cJSON_CreateObject();
        cJSON_AddStringToObject(id, "type", "integer");
        cJSON_AddStringToObject(id, "description", "Card id (move / remove)");
        cJSON_AddItemToObject(props, "id", id);
        cJSON_AddStringToObject(st, "type", "string");
        cJSON_AddStringToObject(st, "description",
            "Target column for move: todo | doing | done");
        cJSON_AddItemToObject(props, "state", st);
    }
    cJSON_AddItemToArray(req, cJSON_CreateString("action"));
    return s;
}

/* M932 (the 2026-10-02 branch's M899, the demos drive's N37): an action returns what it changed and the
 * counts, never the whole board. A lead's 185-card board, rendered whole after
 * every list, add and move, cost ~30,000 tokens a call and kept its history
 * above the compactor's target. `list` gives TODO and DOING in full and DONE as
 * a count; `"all": true` gives everything. `head` may be NULL; `id` 0 shows no
 * card. */
static void emit_change(struct jc_app *app, const char *head, int id,
                        struct jc_tool_result *out)
{
    struct jc_sb sb;
    jc_sb_init(&sb);
    if (head != NULL) {
        jc_sb_append(&sb, head);
    }
    if (id > 0) {
        (void)jc_board_render_card(&app->board, id, &sb);
    }
    jc_board_render_counts(&app->board, &sb);
    out->content = jc_sb_finish(&sb);
    out->is_error = 0;
    jc_sb_free(&sb);
}

static void emit_list(struct jc_app *app, int all, struct jc_tool_result *out)
{
    struct jc_sb sb;
    jc_sb_init(&sb);
    if (all) {
        jc_board_render(&app->board, &sb);
    } else {
        jc_board_render_open(&app->board, &sb);
    }
    out->content = jc_sb_finish(&sb);
    out->is_error = 0;
    jc_sb_free(&sb);
}

/* M761: an edit that was applied but could not be saved did not happen on disk,
 * and the model must not be told it did. Only a board that exists and cannot be
 * read refuses a save (M533). Returns 1 when it set the error. */
static int board_edit_failed(int r, struct jc_tool_result *out)
{
    if (r != JC_BOARD_SAVE_FAILED) {
        return 0;
    }
    tu_err(out, "error: the board was not changed: .jichi/board.json exists but "
                "could not be read, and it is never overwritten -- fix or move "
                "it first");
    return 1;
}

static jc_status board_run(const cJSON *args, struct jc_tool_result *out,
                           struct jc_app *app)
{
    const char *action = tu_arg_str(args, "action");
    cJSON *jid;
    int id;

    if (action == NULL) {
        tu_err(out, "error: 'action' is required (list/add/move/remove/"
                    "set_phase)");
        return JC_OK;
    }
    jid = cJSON_GetObjectItem(args, "id");
    id = cJSON_IsNumber(jid) ? jc_json_clamp_int(jid->valuedouble) : 0;

    if (strcmp(action, "list") == 0) {
        /* M761: what is on disk, not what this session loaded at its start. */
        (void)jc_board_reload(&app->board, app->cwd);
        emit_list(app, tu_arg_bool(args, "all", 0), out);
        return JC_OK;
    }
    if (strcmp(action, "add") == 0) {
        const char *title = tu_arg_str(args, "title");
        const char *phase = tu_arg_str(args, "phase");
        const char *note = tu_arg_str(args, "note");
        int nid;
        if (title == NULL || title[0] == '\0') {
            tu_err(out, "error: 'title' is required for add");
            return JC_OK;
        }
        {
            struct jc_board_edit e;
            memset(&e, 0, sizeof(e));
            e.kind = JC_BOARD_ADD;
            e.title = title;
            e.phase = phase;
            e.note = note;
            if (board_edit_failed(jc_board_edit(&app->board, app->cwd, &e),
                                  out)) {
                return JC_OK;
            }
            nid = e.id;
        }
        {
            char msg[64];
            jc_snprintf(msg, sizeof msg, "added card [%d]\n", nid);
            emit_change(app, msg, nid, out);
        }
        return JC_OK;
    }
    if (strcmp(action, "move") == 0) {
        int st = jc_board_state_from_str(tu_arg_str(args, "state"));
        if (id <= 0 || st < 0) {
            tu_err(out, "error: move needs 'id' and 'state' (todo/doing/done)");
            return JC_OK;
        }
        {
            struct jc_board_edit e;
            int r;
            memset(&e, 0, sizeof(e));
            e.kind = JC_BOARD_MOVE;
            e.id = id;
            e.state = st;
            r = jc_board_edit(&app->board, app->cwd, &e);
            if (r == 0) {
                tu_err(out, "error: no card with that id");
                return JC_OK;
            }
            if (board_edit_failed(r, out)) {
                return JC_OK;
            }
        }
        {
            char msg[64];
            jc_snprintf(msg, sizeof msg, "moved card [%d]\n", id);
            emit_change(app, msg, id, out);
        }
        return JC_OK;
    }
    if (strcmp(action, "remove") == 0) {
        struct jc_board_edit e;
        int r;
        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_REMOVE;
        e.id = id;
        r = (id > 0) ? jc_board_edit(&app->board, app->cwd, &e) : 0;
        if (r == 0) {
            tu_err(out, "error: remove needs a valid 'id'");
            return JC_OK;
        }
        if (board_edit_failed(r, out)) {
            return JC_OK;
        }
        {
            char msg[64];
            jc_snprintf(msg, sizeof msg, "removed card [%d]\n", id);
            emit_change(app, msg, 0, out);
        }
        return JC_OK;
    }
    if (strcmp(action, "set_phase") == 0) {
        struct jc_board_edit e;
        memset(&e, 0, sizeof(e));
        e.kind = JC_BOARD_PHASE;
        e.phase = tu_arg_str(args, "phase");
        if (board_edit_failed(jc_board_edit(&app->board, app->cwd, &e), out)) {
            return JC_OK;
        }
        emit_change(app, "set the active phase\n", 0, out);
        return JC_OK;
    }
    tu_err(out, "error: unknown action");
    return JC_OK;
}

static const struct jc_tool BOARD_TOOL = {
    "board",
    "Read or update the project's kanban board (.jichi/board.json), which tracks "
    "tasks across phases (design/implementation/testing/...) and states "
    "(todo/doing/done). Actions: list {all?}; add {title, phase?, note?}; move "
    "{id, state}; remove {id}; set_phase {phase}. list shows todo and doing in "
    "full and counts the done cards (all: true lists them); a change returns the "
    "changed card and the counts. Durable + shared -- use it to keep the user and "
    "yourself focused on what is todo vs done.",
    board_schema,
    0, /* mutating: writes .jichi/board.json -> permission-gated */
    board_run,
    NULL, NULL, NULL,
    1 /* main_agent_only: the board is shared with the user (M436) */,
    0  /* plan_allowed (M631): only write_plan sets this */
};

const struct jc_tool *jc_tool_board(void)
{
    return &BOARD_TOOL;
}
