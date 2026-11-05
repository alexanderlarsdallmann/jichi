/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_board.c - persisted kanban phase board (see jc_board.h). */

#include "jc_board.h"
#include "jc_fault.h"   /* M761: the edit's window, in FAULT builds */
#include "jc_log.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_mem.h"
#include "jc_path.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

void jc_board_init(struct jc_board *b)
{
    jc_vec_init(&b->cards, sizeof(struct jc_board_card));
    b->active_phase = NULL;
    b->next_id = 1;
    /* M761: init did not reset this, so a reload (free + init + load) kept the
     * flag of the load before it. A session that once saw a damaged board then
     * refused every later edit, even after the file was repaired. Found by the
     * unit test of jc_board_edit, whose struct began as stack garbage. */
    b->load_failed = 0;
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
    jc_status st;
    board_path(cwd, path, sizeof path);
    /* D-15: check that .jichi/board.json is safe to read. */
    st = jc_state_file_check(cwd != NULL ? cwd : ".", path);
    if (st != JC_OK) {
        if (st == JC_ERR_DENIED) {
            jc_logf(JC_LOG_WARN, "board: .jichi/board.json directory is a symlink");
        } else {
            jc_logf(JC_LOG_WARN, "board: .jichi/board.json is a symlink or non-regular file");
        }
        b->load_failed = 1;
        return JC_ERR_DENIED;
    }
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
            c.id = cJSON_IsNumber(jid) ? jc_json_clamp_int(jid->valuedouble) : b->next_id;
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

/* M932 (this file came from the 2026-10-02 branch, where the builder was M900): the board as JSON -- the file's shape, which `jc_board_save` writes
 * (with_counts 0), and the read-only view `jichi board --output json` prints
 * (with_counts 1 adds "counts" after "phase"). NULL on allocation failure. */
cJSON *jc_board_to_json(const struct jc_board *b, int with_counts)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    jc_size i;
    if (root == NULL) {
        return NULL;
    }
    if (b->active_phase != NULL) {
        cJSON_AddStringToObject(root, "phase", b->active_phase);
    } else if (with_counts) {
        cJSON_AddNullToObject(root, "phase");   /* the view always has the key */
    }
    if (with_counts) {
        cJSON *cnt = cJSON_CreateObject();
        int n[3];
        n[0] = n[1] = n[2] = 0;
        for (i = 0; i < b->cards.len; i++) {
            const struct jc_board_card *c =
                (const struct jc_board_card *)jc_vec_at(
                    (struct jc_vec *)&b->cards, i);
            if (c->state >= 0 && c->state <= 2) {
                n[c->state]++;
            }
        }
        cJSON_AddNumberToObject(cnt, "todo", (double)n[0]);
        cJSON_AddNumberToObject(cnt, "doing", (double)n[1]);
        cJSON_AddNumberToObject(cnt, "done", (double)n[2]);
        cJSON_AddItemToObject(root, "counts", cnt);
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
    return root;
}

jc_status jc_board_save(const struct jc_board *b, const char *cwd)
{
    char path[1200];
    char dir[1100];
    cJSON *root;
    char *text;
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

    root = jc_board_to_json(b, 0);
    if (root == NULL) {
        return JC_ERR_OOM;
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

jc_status jc_board_reload(struct jc_board *b, const char *cwd)
{
    jc_board_free(b);
    jc_board_init(b);
    return jc_board_load(b, cwd);
}

/* A board edit takes microseconds; a lock held longer is a process stopped
 * inside one, and costs the next edit this long, not the edit. */
#define JC_BOARD_LOCK_WAIT_MS 5000L

int jc_board_edit(struct jc_board *b, const char *cwd, struct jc_board_edit *e)
{
    char lpath[1200];
    int lk;
    int r = 0;

    if (b == NULL || e == NULL) {
        return 0;
    }
    jc_snprintf(lpath, sizeof lpath, "%s/.jichi", cwd != NULL ? cwd : ".");
    /* M997: guard the .jichi directory AND the board.json leaf BEFORE any
     * create. jc_mkdir_p and jc_lock_take (O_CREAT on board.lock) would both
     * run before the guarded jc_board_load below; with .jichi a symlink to an
     * outside dir, the 0600 lock file was created OUTSIDE the workspace. The
     * directory check returns JC_ERR_DENIED for a symlink; the leaf check
     * returns JC_ERR_INVALID for a symlink/non-regular file. Refuse before we
     * touch anything, leaving e->id untouched. */
    {
        jc_status gst;
        char bpath[1200];
        gst = jc_state_file_check(cwd != NULL ? cwd : ".", lpath);
        if (gst != JC_OK) {
            jc_logf(JC_LOG_WARN,
                    "board: .jichi is a symlink or non-regular file; edit "
                    "refused");
            return JC_BOARD_SAVE_FAILED;
        }
        jc_snprintf(bpath, sizeof bpath, "%s/board.json", lpath);
        gst = jc_state_file_check(cwd != NULL ? cwd : ".", bpath);
        if (gst != JC_OK) {
            jc_logf(JC_LOG_WARN,
                    "board: .jichi/board.json is a symlink or non-regular "
                    "file; edit refused");
            return JC_BOARD_SAVE_FAILED;
        }
    }
    jc_mkdir_p(lpath);
    jc_snprintf(lpath, sizeof lpath, "%s/.jichi/board.lock",
                cwd != NULL ? cwd : ".");
    lk = jc_lock_take(lpath, JC_BOARD_LOCK_WAIT_MS);
    if (lk == JC_LOCK_BUSY) {
        jc_logf(JC_LOG_WARN, "board: another process has held %s for more "
                "than %ld s -- editing without it, so an edit made at this "
                "moment may be lost", lpath, JC_BOARD_LOCK_WAIT_MS / 1000L);
    }
    /* The board as it is NOW, not as this session found it. A damaged file
     * loads as empty with load_failed set, and the save below refuses. */
    (void)jc_board_reload(b, cwd);
    jc_fault_board_pause();   /* M761: the window, held open in FAULT builds */
    switch (e->kind) {
    case JC_BOARD_ADD:
        e->id = jc_board_add(b, e->title, e->phase, e->note);
        r = (e->id > 0) ? 1 : 0;
        break;
    case JC_BOARD_MOVE:
        r = jc_board_move(b, e->id, e->state);
        break;
    case JC_BOARD_REMOVE:
        r = jc_board_remove(b, e->id);
        break;
    case JC_BOARD_PHASE:
        jc_board_set_active_phase(b, e->phase);
        r = 1;
        break;
    }
    if (r == 1 && jc_board_save(b, cwd) != JC_OK) {
        r = JC_BOARD_SAVE_FAILED;
    }
    jc_lock_release(lk);
    return r;
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

void jc_board_render_counts(const struct jc_board *b, struct jc_sb *out)
{
    jc_size i;
    int n[3];
    n[0] = n[1] = n[2] = 0;
    for (i = 0; i < b->cards.len; i++) {
        const struct jc_board_card *c =
            (const struct jc_board_card *)jc_vec_at(
                (struct jc_vec *)&b->cards, i);
        if (c->state >= 0 && c->state <= 2) {
            n[c->state]++;
        }
    }
    jc_sb_append_fmt(out, "Board: %d todo, %d doing, %d done", n[0], n[1], n[2]);
    if (b->active_phase != NULL) {
        jc_sb_append_fmt(out, "; active phase: %s", b->active_phase);
    }
    jc_sb_append(out, "\n");
}

int jc_board_render_card(const struct jc_board *b, int id, struct jc_sb *out)
{
    jc_size i;
    for (i = 0; i < b->cards.len; i++) {
        const struct jc_board_card *c =
            (const struct jc_board_card *)jc_vec_at(
                (struct jc_vec *)&b->cards, i);
        if (c->id != id) {
            continue;
        }
        jc_sb_append_fmt(out, "[%d] %s  (%s", c->id,
                         c->title != NULL ? c->title : "",
                         jc_board_state_word(c->state));
        if (c->phase != NULL) {
            jc_sb_append_fmt(out, ", %s", c->phase);
        }
        jc_sb_append(out, ")");
        if (c->note != NULL) {
            jc_sb_append_fmt(out, " -- %s", c->note);
        }
        jc_sb_append(out, "\n");
        return 1;
    }
    return 0;
}

void jc_board_render_open(const struct jc_board *b, struct jc_sb *out)
{
    jc_size i;
    int done = 0;
    if (b->active_phase != NULL) {
        jc_sb_append_fmt(out, "Active phase: %s\n\n", b->active_phase);
    }
    render_column(b, 0, out);
    render_column(b, 1, out);
    for (i = 0; i < b->cards.len; i++) {
        const struct jc_board_card *c =
            (const struct jc_board_card *)jc_vec_at(
                (struct jc_vec *)&b->cards, i);
        if (c->state == 2) {
            done++;
        }
    }
    jc_sb_append_fmt(out, "## DONE\n  %d card%s (list with \"all\": true shows them)\n",
                     done, done == 1 ? "" : "s");
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
