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

static void emit_board(struct jc_app *app, struct jc_tool_result *out)
{
    struct jc_sb sb;
    jc_sb_init(&sb);
    jc_board_render(&app->board, &sb);
    out->content = jc_sb_finish(&sb);
    out->is_error = 0;
    jc_sb_free(&sb);
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
    id = cJSON_IsNumber(jid) ? (int)jid->valuedouble : 0;

    if (strcmp(action, "list") == 0) {
        emit_board(app, out);
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
        nid = jc_board_add(&app->board, title, phase, note);
        jc_board_save(&app->board, app->cwd);
        {
            char msg[64];
            jc_snprintf(msg, sizeof msg, "added card [%d]\n\n", nid);
            {
                struct jc_sb sb;
                jc_sb_init(&sb);
                jc_sb_append(&sb, msg);
                jc_board_render(&app->board, &sb);
                out->content = jc_sb_finish(&sb);
                out->is_error = 0;
                jc_sb_free(&sb);
            }
        }
        return JC_OK;
    }
    if (strcmp(action, "move") == 0) {
        int st = jc_board_state_from_str(tu_arg_str(args, "state"));
        if (id <= 0 || st < 0) {
            tu_err(out, "error: move needs 'id' and 'state' (todo/doing/done)");
            return JC_OK;
        }
        if (!jc_board_move(&app->board, id, st)) {
            tu_err(out, "error: no card with that id");
            return JC_OK;
        }
        jc_board_save(&app->board, app->cwd);
        emit_board(app, out);
        return JC_OK;
    }
    if (strcmp(action, "remove") == 0) {
        if (id <= 0 || !jc_board_remove(&app->board, id)) {
            tu_err(out, "error: remove needs a valid 'id'");
            return JC_OK;
        }
        jc_board_save(&app->board, app->cwd);
        emit_board(app, out);
        return JC_OK;
    }
    if (strcmp(action, "set_phase") == 0) {
        jc_board_set_active_phase(&app->board, tu_arg_str(args, "phase"));
        jc_board_save(&app->board, app->cwd);
        emit_board(app, out);
        return JC_OK;
    }
    tu_err(out, "error: unknown action");
    return JC_OK;
}

static const struct jc_tool BOARD_TOOL = {
    "board",
    "Read or update the project's kanban board (.jichi/board.json), which tracks "
    "tasks across phases (design/implementation/testing/...) and states "
    "(todo/doing/done). Actions: list; add {title, phase?, note?}; move {id, "
    "state}; remove {id}; set_phase {phase}. Durable + shared -- use it to keep "
    "the user and yourself focused on what is todo vs done.",
    board_schema,
    0, /* mutating: writes .jichi/board.json -> permission-gated */
    board_run,
    NULL, NULL, NULL,
    1 /* main_agent_only: the board is shared with the user (M436) */
};

const struct jc_tool *jc_tool_board(void)
{
    return &BOARD_TOOL;
}
