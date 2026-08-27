/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_board.c - persisted kanban phase board (see jc_board.h). */

#include "jc_board.h"
#include "jc_log.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_mem.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

void jc_board_init(struct jc_board *b)
{
    jc_vec_init(&b->cards, sizeof(struct jc_board_card));
    b->active_phase = NULL;
    b->next_id = 1;
}

void jc_board_free(struct jc_board *b)
{
    jc_size i;
    if (b == NULL) {
        return;
    }
    for (i = 0; i < b->cards.len; i++) {
        struct jc_board_card *c =
            (struct jc_board_card *)jc_vec_at(&b->cards, i);
        free(c->title);
        free(c->phase);
        free(c->note);
    }
    jc_vec_free(&b->cards);
    free(b->active_phase);
    b->active_phase = NULL;
}

int jc_board_state_from_str(const char *s)
{
    if (s == NULL) {
        return -1;
    }
    if (strcmp(s, "todo") == 0 || strcmp(s, "pending") == 0) {
        return 0;
    }
    if (strcmp(s, "doing") == 0 || strcmp(s, "in_progress") == 0 ||
        strcmp(s, "wip") == 0) {
        return 1;
    }
    if (strcmp(s, "done") == 0) {
        return 2;
    }
    return -1;
}

const char *jc_board_state_word(int state)
{
    if (state == 1) {
        return "doing";
    }
    if (state == 2) {
        return "done";
    }
    return "todo";
}

static char *dup_or_null(const char *s)
{
    char *o;
    jc_size n;
    if (s == NULL || s[0] == '\0') {
        return NULL;
    }
    n = (jc_size)strlen(s);
    o = (char *)malloc(n + 1);
    if (o != NULL) {
        memcpy(o, s, n + 1);
    }
    return o;
}

int jc_board_add(struct jc_board *b, const char *title, const char *phase,
                 const char *note)
{
    struct jc_board_card c;
    if (title == NULL || title[0] == '\0') {
        return 0;
    }
    c.id = b->next_id++;
    c.title = dup_or_null(title);
    c.phase = dup_or_null(phase);
    c.note = dup_or_null(note);
    c.state = 0;
    jc_vec_push(&b->cards, &c);
    return c.id;
}

int jc_board_move(struct jc_board *b, int id, int state)
{
    jc_size i;
    if (state < 0 || state > 2) {
        return 0;
    }
    for (i = 0; i < b->cards.len; i++) {
        struct jc_board_card *c =
            (struct jc_board_card *)jc_vec_at(&b->cards, i);
        if (c->id == id) {
            c->state = state;
            return 1;
        }
    }
    return 0;
}

int jc_board_remove(struct jc_board *b, int id)
{
    jc_size i;
    for (i = 0; i < b->cards.len; i++) {
        struct jc_board_card *c =
            (struct jc_board_card *)jc_vec_at(&b->cards, i);
        if (c->id == id) {
            jc_size j;
            free(c->title);
            free(c->phase);
            free(c->note);
            /* Compact: shift the trailing card structs down one slot (their
             * owned pointers move by value -- no double free). */
            for (j = i + 1; j < b->cards.len; j++) {
                struct jc_board_card *dst =
                    (struct jc_board_card *)jc_vec_at(&b->cards, j - 1);
                struct jc_board_card *src =
                    (struct jc_board_card *)jc_vec_at(&b->cards, j);
                *dst = *src;
            }
            b->cards.len--;
            return 1;
        }
    }
    return 0;
}

void jc_board_set_active_phase(struct jc_board *b, const char *phase)
{
    free(b->active_phase);
    b->active_phase = dup_or_null(phase);
}

/* <cwd>/.jichi/board.json */
static void board_path(const char *cwd, char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi/board.json", cwd != NULL ? cwd : ".");
}

jc_status jc_board_load(struct jc_board *b, const char *cwd)
{
    char path[1200];
    char *text = NULL;
    cJSON *root, *arr, *it, *ph;
    struct jc_arena *ta;
    board_path(cwd, path, sizeof path);
    if (!jc_file_exists(path)) {
        return JC_OK; /* no board yet */
    }
    /* A short-lived arena for the file read; card strings are malloc-dup'd
     * (dup_or_null) so they outlive it. */
    ta = jc_arena_new(0);
    if (ta == NULL || jc_read_file(path, &text, NULL, ta) != JC_OK ||
        text == NULL) {
        if (ta != NULL) {
            jc_arena_free(ta);
        }
        /* M533: the file is there and unreadable -- not the same as absent. */
        b->load_failed = 1;
        jc_logf(JC_LOG_WARN, "board: %s exists but could not be read; the board "
                "is shown EMPTY and will not be overwritten", path);
        return JC_ERR_IO;
    }
    root = cJSON_Parse(text);
    jc_arena_free(ta);
    if (root == NULL) {
        /* M533: this used to return JC_OK and call itself tolerant of a
         * malformed board. Tolerance here meant SILENCE, and silence plus the
         * next save meant
         * destruction: an unparseable board loaded as empty and `board add`
         * wrote the empty board over it. Say so, and refuse to save. */
        b->load_failed = 1;
        jc_logf(JC_LOG_WARN, "board: %s is not valid JSON; the board is shown "
                "EMPTY and will NOT be overwritten -- fix or move the file "
                "(its cards are still in it)", path);
        return JC_ERR_PARSE;
    }
    ph = cJSON_GetObjectItemCaseSensitive(root, "phase");
    if (cJSON_IsString(ph)) {
        jc_board_set_active_phase(b, ph->valuestring);
    }
    arr = cJSON_GetObjectItemCaseSensitive(root, "cards");
    if (cJSON_IsArray(arr)) {
        for (it = arr->child; it != NULL; it = it->next) {
            struct jc_board_card c;
            cJSON *jid = cJSON_GetObjectItemCaseSensitive(it, "id");
            c.id = cJSON_IsNumber(jid) ? (int)jid->valuedouble : b->next_id;
            c.title = dup_or_null(jc_json_get_str(it, "title", NULL));
            c.phase = dup_or_null(jc_json_get_str(it, "phase", NULL));
            c.note = dup_or_null(jc_json_get_str(it, "note", NULL));
            c.state = jc_board_state_from_str(
                jc_json_get_str(it, "state", "todo"));
            if (c.state < 0) {
                c.state = 0;
            }
            if (c.title == NULL) {
                free(c.phase);
                free(c.note);
                continue;
            }
            if (c.id >= b->next_id) {
                b->next_id = c.id + 1;
            }
            jc_vec_push(&b->cards, &c);
        }
    }
    cJSON_Delete(root);
    return JC_OK;
}

jc_status jc_board_save(const struct jc_board *b, const char *cwd)
{
    char path[1200];
    char dir[1100];
    cJSON *root, *arr;
    char *text;
    jc_size i;
    jc_status st;

    /* M533: never overwrite a board we could not read. docs/BOARD.md tells the
     * operator to COMMIT this file, so a silent overwrite lands in git looking
     * legitimate. Refusing costs one card; not refusing cost three. */
    if (b != NULL && b->load_failed) {
        jc_logf(JC_LOG_WARN, "board: refusing to save over a board that could "
                "not be read -- fix or move .jichi/board.json first");
        return JC_ERR_INVALID;
    }

    jc_snprintf(dir, sizeof dir, "%s/.jichi", cwd != NULL ? cwd : ".");
    jc_mkdir_p(dir);
    board_path(cwd, path, sizeof path);

    root = cJSON_CreateObject();
    if (root == NULL) {
        return JC_ERR_OOM;
    }
    if (b->active_phase != NULL) {
        cJSON_AddStringToObject(root, "phase", b->active_phase);
    }
    arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "cards", arr);
    for (i = 0; i < b->cards.len; i++) {
        const struct jc_board_card *c =
            (const struct jc_board_card *)jc_vec_at(
                (struct jc_vec *)&b->cards, i);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", (double)c->id);
        cJSON_AddStringToObject(o, "title", c->title != NULL ? c->title : "");
        if (c->phase != NULL) {
            cJSON_AddStringToObject(o, "phase", c->phase);
        }
        if (c->note != NULL) {
            cJSON_AddStringToObject(o, "note", c->note);
        }
        cJSON_AddStringToObject(o, "state", jc_board_state_word(c->state));
        cJSON_AddItemToArray(arr, o);
    }
    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return JC_ERR_OOM;
    }
    /* M533: atomic, like the session/lease/calibration sinks -- an interrupted
     * write is what produced the unparseable file in the first place, and
     * include/jc_lease.h already asserts "atomicity keeps each file valid"
     * while naming this one. */
    st = jc_write_file_atomic(path, text, strlen(text));
    free(text);
    return st;
}

static void render_column(const struct jc_board *b, int state,
                          struct jc_sb *out)
{
    jc_size i;
    int n = 0;
    jc_sb_append(out, "## ");
    {
        const char *w = jc_board_state_word(state);
        /* uppercase the header */
        while (*w != '\0') {
            char u = (*w >= 'a' && *w <= 'z') ? (char)(*w - 32) : *w;
            jc_sb_append_n(out, &u, 1);
            w++;
        }
    }
    jc_sb_append(out, "\n");
    for (i = 0; i < b->cards.len; i++) {
        const struct jc_board_card *c =
            (const struct jc_board_card *)jc_vec_at(
                (struct jc_vec *)&b->cards, i);
        if (c->state != state) {
            continue;
        }
        jc_sb_append_fmt(out, "  [%d] %s", c->id,
                         c->title != NULL ? c->title : "");
        if (c->phase != NULL) {
            jc_sb_append_fmt(out, "  (%s)", c->phase);
        }
        if (c->note != NULL) {
            jc_sb_append_fmt(out, " -- %s", c->note);
        }
        jc_sb_append(out, "\n");
        n++;
    }
    if (n == 0) {
        jc_sb_append(out, "  (none)\n");
    }
}

void jc_board_render(const struct jc_board *b, struct jc_sb *out)
{
    if (b->active_phase != NULL) {
        jc_sb_append_fmt(out, "Active phase: %s\n\n", b->active_phase);
    }
    render_column(b, 0, out);
    render_column(b, 1, out);
    render_column(b, 2, out);
}

void jc_board_render_focus(const struct jc_board *b, struct jc_sb *out)
{
    jc_size i;
    int doing = 0;
    int todo = 0;
    if (b->cards.len == 0) {
        return;
    }
    jc_sb_append(out, "\n# Project board (focus)\n");
    if (b->active_phase != NULL) {
        jc_sb_append_fmt(out, "Active phase: %s\n", b->active_phase);
    }
    for (i = 0; i < b->cards.len; i++) {
        const struct jc_board_card *c =
            (const struct jc_board_card *)jc_vec_at(
                (struct jc_vec *)&b->cards, i);
        if (c->state == 1) {
            if (doing == 0) {
                jc_sb_append(out, "In progress:\n");
            }
            jc_sb_append_fmt(out, "- %s\n", c->title != NULL ? c->title : "");
            doing++;
        }
    }
    if (doing == 0) {
        for (i = 0; i < b->cards.len && todo < 3; i++) {
            const struct jc_board_card *c =
                (const struct jc_board_card *)jc_vec_at(
                    (struct jc_vec *)&b->cards, i);
            if (c->state == 0) {
                if (todo == 0) {
                    jc_sb_append(out, "Next up (todo):\n");
                }
                jc_sb_append_fmt(out, "- %s\n",
                                 c->title != NULL ? c->title : "");
                todo++;
            }
        }
    }
}
