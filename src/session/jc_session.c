/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_session.c - session persistence (see jc_session.h). */

#include "jc_session.h"
#include "jc_version.h"
#include "jc_sessmeta.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_uuid.h"
#include "jc_vec.h"
#include "jc_log.h"
#include "jc_perm.h"
#include "jc_cli.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h> /* remove() for jc_session_delete */

/* Compute the sessions directory path: ~/.jichi.d/sessions
 *
 * M204: this used to be ~/.continue/sessions, chosen when jichi was a from-scratch
 * reimplementation meant to sit BESIDE Continue CLI and share a session layout.
 * The M170 rename moved every other path -- binary, config, state dir, assets,
 * per-project dir, env vars, man page, editor plugins -- and missed this one,
 * because nothing breaks when it is wrong. The consequences were real: sessions
 * were the single piece of per-user state living in a foreign product's
 * directory, so cleaning up Continue destroyed a jichi user's history, and the
 * docs had to carry a "do not delete ~/.continue" warning that should never have
 * needed to exist. Sessions now live with telemetry, journals, checkpoints,
 * calibration, the index and the audit logs, where they belong.
 *
 * warn_legacy_paths (src/main.c) tells the operator how to bring an existing
 * store forward; it is deliberately not moved automatically. */
static void sessions_dir(char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi.d/sessions", jc_home_dir());
}

static void session_path(const char *id, char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi.d/sessions/%s.json", jc_home_dir(), id);
}

jc_status jc_session_new(struct jc_session *s, const char *workspace,
                         struct jc_arena *a)
{
    char uuid[40];
    jc_uuid_v4(uuid);
    s->arena = a;
    s->id = jc_arena_strdup(a, uuid);
    s->title = NULL;
    s->alias = NULL;
    s->workspace = jc_arena_strdup(a, workspace != NULL ? workspace : ".");
    s->mode = JC_MODE_CHAT;
    jc_history_init(&s->history);
    jc_todo_init(&s->todos);
    s->saved_gen = 0;
    s->saved_todos_gen = 0;
    s->saved_mode = s->mode;
    s->meta_dirty = 1; /* never saved: the first save must write */
    return JC_OK;
}

jc_status jc_session_fork(struct jc_session *src, struct jc_session *dst,
                          struct jc_arena *a)
{
    char uuid[40];
    jc_size i;

    if (src == NULL || dst == NULL) {
        return JC_ERR_INVALID;
    }
    jc_uuid_v4(uuid);
    dst->arena = a;
    dst->id = jc_arena_strdup(a, uuid);
    dst->alias = NULL; /* a fork is a new session; aliases stay unique */
    dst->workspace = jc_arena_strdup(a,
        src->workspace != NULL ? src->workspace : ".");
    dst->mode = src->mode;
    if (src->title != NULL && src->title[0] != '\0') {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_sb_append(&sb, src->title);
        jc_sb_append(&sb, " (fork)");
        dst->title = jc_arena_strdup(a, sb.data != NULL ? sb.data : src->title);
        jc_sb_free(&sb);
    } else {
        dst->title = NULL;
    }
    jc_history_init(&dst->history);
    jc_todo_init(&dst->todos);
    /* M606: the task list is session state, so a fork carries it -- the
     * fourth codec site (save, load, fork), and the one the M367 survey's
     * lint could not see because it scrapes struct jc_message. */
    if (jc_todo_copy(&dst->todos, &src->todos) != JC_OK) {
        return JC_ERR_OOM;
    }
    dst->saved_gen = 0;
    dst->saved_todos_gen = 0;
    dst->saved_mode = dst->mode;
    dst->meta_dirty = 1; /* never saved: the first save must write */

    for (i = 0; i < jc_history_len(&src->history); i++) {
        struct jc_message *m = jc_history_get(&src->history, i);
        if (m->role == JC_ROLE_TOOL) {
            jc_history_add_tool_result(&dst->history, m->tool_call_id,
                                       m->content != NULL ? m->content : "",
                                       m->is_error);
        } else {
            struct jc_message *nm = jc_history_add(&dst->history, m->role,
                                                   m->content);
            jc_size k;
            /* M367: the fork copy is a third codec site, and it had the same
             * hole as save/load -- every field copied except truncated. */
            if (nm != NULL) {
                nm->truncated = m->truncated;
            }
            for (k = 0; k < jc_msg_tool_call_count(m); k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                jc_msg_add_tool_call(nm, tc->id, tc->name, tc->arguments_json);
            }
            for (k = 0; k < jc_msg_image_count(m); k++) {
                struct jc_image *im = jc_msg_image_at(m, k);
                jc_msg_add_image(nm, im->media_type, im->data);
            }
        }
    }
    return JC_OK;
}

void jc_session_free(struct jc_session *s)
{
    jc_history_free(&s->history);
    jc_todo_free(&s->todos); /* M606 */
    /* id/title/workspace live in the arena; nothing else to free here. */
}

void jc_session_autotitle(struct jc_session *s)
{
    jc_size i;
    if (s->title != NULL) {
        return;
    }
    for (i = 0; i < jc_history_len(&s->history); i++) {
        struct jc_message *m = jc_history_get(&s->history, i);
        if (m->role == JC_ROLE_USER && m->content != NULL) {
            char buf[80];
            jc_size n = strlen(m->content);
            if (n > 60) {
                n = 60;
            }
            memcpy(buf, m->content, n);
            buf[n] = '\0';
            s->title = jc_arena_strdup(s->arena, buf);
            s->meta_dirty = 1;
            return;
        }
    }
}

/* --- serialisation --- */

/* M606: the task list as a JSON array of {"content","status"} (wire words). */
static cJSON *todos_to_json(const struct jc_todo_list *t)
{
    cJSON *arr = cJSON_CreateArray();
    jc_size i;
    if (arr == NULL) {
        return NULL;
    }
    for (i = 0; i < t->items.len; i++) {
        const struct jc_todo_item *it =
            (const struct jc_todo_item *)jc_vec_at((struct jc_vec *)&t->items, i);
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            cJSON_Delete(arr);
            return NULL;
        }
        cJSON_AddStringToObject(o, "content",
                                it->content != NULL ? it->content : "");
        cJSON_AddStringToObject(o, "status", jc_todo_status_wire(it->status));
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

/* M606: the inverse -- fill an initialised list from the "todos" array. A
 * session file is a file on disk, so it is read leniently: an item without a
 * string `content` is skipped, an unknown status is pending. */
static void todos_from_json(const cJSON *arr, struct jc_todo_list *t)
{
    cJSON *ti;
    if (!cJSON_IsArray(arr)) {
        return;
    }
    cJSON_ArrayForEach(ti, arr) {
        const char *c = jc_json_get_str(ti, "content", NULL);
        struct jc_todo_item it;
        if (c == NULL) {
            continue;
        }
        it.content = jc_strdup(c);
        if (it.content == NULL) {
            continue;
        }
        it.status = jc_todo_status_from_wire(jc_json_get_str(ti, "status", NULL));
        if (jc_vec_push(&t->items, &it) != JC_OK) {
            free(it.content);
        }
    }
}

static cJSON *message_to_json(struct jc_message *m)
{
    cJSON *jm = cJSON_CreateObject();
    cJSON_AddStringToObject(jm, "role", jc_role_str(m->role));
    /* Image attachments (M29) are turn-ephemeral: persist a text placeholder so
     * the transcript records their presence, but not the base64 bytes (which
     * would bloat the session file). A resumed session therefore notes that
     * images were sent without re-sending them. */
    if (jc_msg_image_count(m) > 0) {
        struct jc_sb sb;
        jc_size k;
        jc_sb_init(&sb);
        if (m->content != NULL) {
            jc_sb_append(&sb, m->content);
        }
        for (k = 0; k < jc_msg_image_count(m); k++) {
            struct jc_image *img = jc_msg_image_at(m, k);
            jc_sb_append_fmt(&sb, "%s[image: %s]", (sb.len > 0) ? "\n" : "",
                             img->media_type ? img->media_type : "image");
        }
        cJSON_AddStringToObject(jm, "content", sb.data ? sb.data : "");
        jc_sb_free(&sb);
    } else if (m->content != NULL) {
        cJSON_AddStringToObject(jm, "content", m->content);
    }
    if (m->tool_call_id != NULL) {
        cJSON_AddStringToObject(jm, "toolCallId", m->tool_call_id);
    }
    if (m->is_error) {
        cJSON_AddBoolToObject(jm, "isError", 1);
    }
    /* M367: the M334 truncation verdict must survive a resume -- without it,
     * a session whose last assistant turn was cut at the output ceiling
     * reloads as if it had completed, and the cut-off-arguments handling
     * misfires. Found by the field-fidelity survey: every other jc_message
     * field was carried or deliberately ephemeral (images, above); this one
     * was simply forgotten when M334 added it -- in save, in load, AND in
     * the fork copy loop below, three codec sites all missing the same
     * field. session_fields_lint.sh trips on the next forgotten field. */
    if (m->truncated) {
        cJSON_AddBoolToObject(jm, "truncated", 1);
    }
    if (jc_msg_tool_call_count(m) > 0) {
        cJSON *arr = cJSON_CreateArray();
        jc_size k;
        for (k = 0; k < jc_msg_tool_call_count(m); k++) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
            cJSON *jt = cJSON_CreateObject();
            cJSON_AddStringToObject(jt, "id", tc->id ? tc->id : "");
            cJSON_AddStringToObject(jt, "name", tc->name ? tc->name : "");
            cJSON_AddStringToObject(jt, "arguments",
                                    tc->arguments_json ? tc->arguments_json
                                                       : "{}");
            cJSON_AddItemToArray(arr, jt);
        }
        cJSON_AddItemToObject(jm, "toolCalls", arr);
    }
    return jm;
}

static jc_role role_from_str(const char *s)
{
    if (s == NULL) return JC_ROLE_USER;
    if (strcmp(s, "system") == 0) return JC_ROLE_SYSTEM;
    if (strcmp(s, "assistant") == 0) return JC_ROLE_ASSISTANT;
    if (strcmp(s, "tool") == 0) return JC_ROLE_TOOL;
    return JC_ROLE_USER;
}

int jc_session_needs_save(const struct jc_session *s)
{
    return s->history.gen != s->saved_gen || s->mode != s->saved_mode ||
           s->todos.gen != s->saved_todos_gen || s->meta_dirty;
}

char *jc_session_serialize(struct jc_session *s)
{
    cJSON *env;
    char *meta;
    struct jc_sb sb;
    jc_size mlen;
    jc_size i;
    int first;
    int oom = 0;

    /* M232: build the metadata envelope (everything but the history array) as a
     * small object and print it, then stream the history one message at a time
     * -- so the whole-history cJSON tree never exists at once (the old path
     * built every message into one array tree before printing: ~2-3x the
     * serialized text at peak; this is ~1x + one message). The output is
     * BYTE-IDENTICAL to the old whole-tree print: cJSON prints an object as
     * '{' members '}' and an array as '[' elems ']', so splicing
     * ,"history":[ <msgs> ] onto the brace-stripped metadata object reproduces
     * exactly what cJSON_PrintUnformatted(root) produced.
     *
     * M140 note preserved: the output stays UNFORMATTED (the load path parses
     * either form, so old sessions remain readable). */
    env = cJSON_CreateObject();
    if (env == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(env, "sessionId", s->id);
    /* M606: the store's shape version (JC_SESSION_STORE_V). Every other
     * persisted format had one (JC_EVENTLOG_SCHEMA, JC_CALIB_SCHEMA); the one
     * file a pairing session accumulates for weeks did not. */
    cJSON_AddNumberToObject(env, "v", (double)JC_SESSION_STORE_V);
    /* M290: the build that last SAVED this session. A session is rewritten after
     * every turn, so this is the newest jichi that touched it -- which is what a
     * reader of a resumed or archived session needs, and what `--resume` across a
     * version change would otherwise hide. */
    cJSON_AddStringToObject(env, "jichi", JC_VERSION);
    if (s->title != NULL) {
        cJSON_AddStringToObject(env, "title", s->title);
    }
    if (s->alias != NULL) {
        cJSON_AddStringToObject(env, "alias", s->alias);
    }
    cJSON_AddStringToObject(env, "workspaceDirectory",
                            s->workspace ? s->workspace : ".");
    cJSON_AddStringToObject(env, "mode",
                            jc_agent_mode_name((enum jc_agent_mode)s->mode));
    meta = cJSON_PrintUnformatted(env);
    cJSON_Delete(env);
    if (meta == NULL) {
        return NULL;
    }
    mlen = strlen(meta);

    jc_sb_init(&sb);
    /* meta is "{...}"; append it without the closing brace, then splice in the
     * history array so the result is one well-formed object. */
    if (mlen > 0 && meta[mlen - 1] == '}') {
        if (jc_sb_append_n(&sb, meta, mlen - 1) != JC_OK) oom = 1;
    } else {
        if (jc_sb_append(&sb, meta) != JC_OK) oom = 1; /* defensive */
    }
    free(meta);
    if (jc_sb_append(&sb, ",\"history\":[") != JC_OK) oom = 1;
    first = 1;
    for (i = 0; i < jc_history_len(&s->history); i++) {
        struct jc_message *m = jc_history_get(&s->history, i);
        cJSON *mj;
        char *ms;
        if (m->role == JC_ROLE_SYSTEM) {
            continue; /* system messages are rebuilt each turn */
        }
        mj = message_to_json(m);
        if (mj == NULL) {
            jc_sb_free(&sb);
            return NULL;
        }
        ms = cJSON_PrintUnformatted(mj);
        cJSON_Delete(mj);
        if (ms == NULL) {
            jc_sb_free(&sb);
            return NULL;
        }
        if (!first) {
            if (jc_sb_append(&sb, ",") != JC_OK) oom = 1;
        }
        if (jc_sb_append(&sb, ms) != JC_OK) oom = 1;
        free(ms);
        first = 0;
    }
    if (jc_sb_append(&sb, "]") != JC_OK) oom = 1;
    /* M606: the task list, as the codec's own array -- {"content","status"}
     * with the WIRE status words, so a file reads like the tool's input. Always
     * present (possibly empty), so a reader can tell "no list" from "written
     * before v2". Spliced after the history array; the whole document is still
     * exactly what a whole-tree cJSON_PrintUnformatted would produce. */
    {
        cJSON *ta = todos_to_json(&s->todos);
        char *ts = (ta != NULL) ? cJSON_PrintUnformatted(ta) : NULL;
        cJSON_Delete(ta);
        if (ts == NULL) {
            jc_sb_free(&sb);
            return NULL;
        }
        if (jc_sb_append(&sb, ",\"todos\":") != JC_OK) oom = 1;
        if (jc_sb_append(&sb, ts) != JC_OK) oom = 1;
        free(ts);
    }
    if (jc_sb_append(&sb, "}") != JC_OK) oom = 1;

    if (oom) {
        /* Don't return a truncated (corrupt) document -- match the old
         * PrintUnformatted-returned-NULL path so the caller writes nothing. */
        jc_sb_free(&sb);
        return NULL;
    }
    return jc_sb_finish(&sb);
}

jc_status jc_session_save(struct jc_session *s)
{
    char dir[1024];
    char path[1100];
    char *text;
    jc_status st;

    /* M218: skip a byte-identical rewrite. Several TUI sites save at one
     * turn boundary; only the first after a change pays the re-serialize. */
    if (!jc_session_needs_save(s)) {
        return JC_OK;
    }

    sessions_dir(dir, sizeof(dir));
    /* 0700 on what we create, so session ids do not leak via a dir listing
     * (M132); a pre-existing directory keeps the mode its owner chose (M488). */
    (void)jc_mkdir_p_private(dir);
    session_path(s->id, path, sizeof(path));

    text = jc_session_serialize(s); /* M232: streamed; NULL on OOM */
    if (text == NULL) {
        return JC_ERR_OOM;
    }
    /* M146: atomic replace -- an every-turn write that external readers (a
     * second jichi, a supervisor, a backup job) may race; they now see the old
     * session or the new one, never a torn file. mkstemp's 0600 carries over
     * (M132's owner-only requirement, guaranteed rather than patched after). */
    st = jc_write_file_atomic(path, text, strlen(text));
    free(text);
    if (st == JC_OK) {
        jc_make_private(path); /* belt-and-suspenders; mkstemp already 0600 */
        s->saved_gen = s->history.gen;
        s->saved_todos_gen = s->todos.gen;
        s->saved_mode = s->mode;
        s->meta_dirty = 0;
    }
    return st;
}

static jc_status load_from_text(const char *text, struct jc_session *s,
                                struct jc_arena *a)
{
    cJSON *root;
    cJSON *msgs;
    cJSON *jm;
    const char *id;
    const char *title;
    const char *ws;
    const char *mode_s;
    enum jc_agent_mode m;

    root = jc_json_parse(text);
    if (root == NULL) {
        return JC_ERR_PARSE;
    }
    s->arena = a;
    id = jc_json_get_str(root, "sessionId", NULL);
    title = jc_json_get_str(root, "title", NULL);
    ws = jc_json_get_str(root, "workspaceDirectory", ".");
    s->id = jc_arena_strdup(a, id != NULL ? id : "unknown");
    s->title = (title != NULL) ? jc_arena_strdup(a, title) : NULL;
    {
        const char *al = jc_json_get_str(root, "alias", NULL);
        s->alias = (al != NULL) ? jc_arena_strdup(a, al) : NULL;
    }
    s->workspace = jc_arena_strdup(a, ws);
    mode_s = jc_json_get_str(root, "mode", NULL);
    s->mode = (mode_s != NULL && jc_agent_mode_parse(mode_s, &m))
              ? (int)m : JC_MODE_CHAT;
    /* M606: a store written by a NEWER jichi is loaded anyway (the
     * conversation is the user's and is not regenerable), with one warning
     * that whatever the newer shape added is not restored here. */
    {
        double v = jc_json_get_num_lenient(root, "v", 1.0);
        if (v > (double)JC_SESSION_STORE_V) {
            jc_logf(JC_LOG_WARN, "[session] %s was saved by a newer jichi "
                    "(store v%.0f, this build reads v%d): fields the newer "
                    "shape added are not restored", s->id, v,
                    JC_SESSION_STORE_V);
        }
    }
    jc_history_init(&s->history);
    jc_todo_init(&s->todos);
    todos_from_json(jc_json_get_obj(root, "todos"), &s->todos);

    msgs = jc_json_get_obj(root, "history");
    if (cJSON_IsArray(msgs)) {
        cJSON_ArrayForEach(jm, msgs) {
            jc_role role = role_from_str(jc_json_get_str(jm, "role", "user"));
            const char *content = jc_json_get_str(jm, "content", NULL);
            struct jc_message *m;
            cJSON *tcs;
            if (role == JC_ROLE_TOOL) {
                const char *tcid = jc_json_get_str(jm, "toolCallId", NULL);
                /* M530: lenient. A session file is on disk under .jichi/,
                 * which docs/EMBEDDING.md calls Provisional and hand-editable,
                 * and it may have been written by an older jichi or a migration
                 * script. `isError` decides whether a tool result WAS an error,
                 * so reading `1` as false reloads a failed call as a success --
                 * the same invariant ("tool errors are values") that the MCP
                 * `isError` conversion protected at M519, on the reload path. */
                int is_err = jc_json_get_bool_lenient(jm, "isError", 0);
                jc_history_add_tool_result(&s->history, tcid,
                                           content ? content : "", is_err);
                continue;
            }
            m = jc_history_add(&s->history, role, content);
            if (m != NULL) {
                m->truncated = jc_json_get_bool_lenient(jm, "truncated", 0);
            }
            tcs = jc_json_get_obj(jm, "toolCalls");
            if (cJSON_IsArray(tcs)) {
                cJSON *jt;
                cJSON_ArrayForEach(jt, tcs) {
                    jc_msg_add_tool_call(m,
                        jc_json_get_str(jt, "id", ""),
                        jc_json_get_str(jt, "name", ""),
                        jc_json_get_str(jt, "arguments", "{}"));
                }
            }
        }
    }
    cJSON_Delete(root);
    /* M218: memory now equals disk, so an immediate no-change save may be
     * skipped (the appends above bumped gen; settle it here). */
    s->saved_gen = s->history.gen;
    s->saved_todos_gen = s->todos.gen;
    s->saved_mode = s->mode;
    s->meta_dirty = 0;
    return JC_OK;
}

jc_status jc_session_load_by_id(const char *id, struct jc_session *s,
                                struct jc_arena *a)
{
    char path[1100];
    char *text;
    jc_status st;
    struct jc_arena *ta;

    session_path(id, path, sizeof(path));
    /* M197: the file text is a transient the parser consumes -- load_from_text
     * copies the four scalars onto `a` and the history into malloc'd messages.
     * Reading it onto `a` (the session arena, freed only at exit) retained a
     * full copy of every resumed session for the life of the process. */
    ta = jc_arena_new(0);
    if (ta == NULL) {
        return JC_ERR_OOM;
    }
    if (jc_read_file(path, &text, NULL, ta) != JC_OK) {
        jc_arena_free(ta);
        return JC_ERR_NOTFOUND;
    }
    st = load_from_text(text, s, a);
    jc_arena_free(ta);
    /* M198: the file we just read is <id>.json, so the session's identity is
     * `id` whatever its sessionId field claims. Without this, loading a
     * mismatched session would save it back under a DIFFERENT path, silently
     * duplicating it. Pairs with jc_session_list's stem-derived meta.id. */
    if (st == JC_OK && s->id != NULL && strcmp(s->id, id) != 0) {
        s->id = jc_arena_strdup(a, id);
    }
    return st;
}

jc_status jc_session_load_recent(struct jc_session *s, struct jc_arena *a)
{
    return jc_session_load_recent_scoped(NULL, s, a);
}

/* Sort jc_session_meta newest-first (descending mtime). */
static int meta_cmp_desc(const void *a, const void *b)
{
    const struct jc_session_meta *x = (const struct jc_session_meta *)a;
    const struct jc_session_meta *y = (const struct jc_session_meta *)b;
    if (x->mtime < y->mtime) return 1;
    if (x->mtime > y->mtime) return -1;
    /* M198: break the tie by id so the order is TOTAL. jc_file_mtime is
     * second-granularity and qsort is not stable, so two sessions saved in the
     * same second used to sort unpredictably -- which made "the most recent
     * session" nondeterministic for bare /resume, --continue and
     * jc_session_load_recent_scoped whenever two instances share the store (a
     * documented deployment, see docs/AUTONOMOUS_LOOPS.md). An unreproducible
     * "it resumed the wrong session" is worse than an arbitrary-but-stable
     * choice. See docs/proposals/2026-07-robustness-edge-cases.md (#6). */
    if (x->id != NULL && y->id != NULL) {
        return strcmp(x->id, y->id);
    }
    if (x->id == NULL && y->id != NULL) return 1;
    if (x->id != NULL && y->id == NULL) return -1;
    return 0;
}

jc_status jc_session_list(struct jc_vec *out, struct jc_arena *a)
{
    return jc_session_list_ex(out, a, NULL);
}

jc_status jc_session_list_ex(struct jc_vec *out, struct jc_arena *a,
                             int *skipped)
{
    char dir[1024];
    struct jc_vec names;
    jc_size i;
    struct jc_arena *na; /* dirent names: one loop's lifetime */
    struct jc_arena *fa; /* one file's text: reset per iteration */
    jc_status st;

    if (skipped != NULL) {
        *skipped = 0;
    }
    sessions_dir(dir, sizeof(dir));
    /* M197: this function needs four scalars and the history array's length out
     * of each session, yet it used to read every file's FULL TEXT onto the
     * caller's arena -- which in the TUI is app->arena, freed only at process
     * exit. One /sessions on a 250-file/17.9 MB store therefore retained
     * 17.5 MB permanently, and so did every Tab press on `/resume `. The cJSON
     * tree that actually held the wanted fields was correctly deleted, so the
     * retention was exactly backwards.
     *
     * Two build-local arenas, in the shape M140 used for the repo map
     * (src/index/jc_repomap.c:666,719): `na` for the dirent list, and `fa` for
     * the file text, RESET each iteration -- so the transient peak is one
     * session file rather than the whole store. Only the bounded per-session
     * metadata survives, on the caller's arena. */
    na = jc_arena_new(0);
    fa = jc_arena_new(0);
    if (na == NULL || fa == NULL) {
        if (na != NULL) jc_arena_free(na);
        if (fa != NULL) jc_arena_free(fa);
        return JC_ERR_OOM;
    }
    jc_vec_init(&names, sizeof(char *));
    /* M482: propagate rather than flatten. NOTFOUND means "no store yet", which
     * is an ordinary state on a fresh install and must stay an empty listing with
     * a success exit; JC_ERR_IO means the directory exists and could not be read,
     * which is a fault the caller has to report. Flattening both to NOTFOUND is
     * what let an unreadable store look like an empty one. */
    st = jc_list_dir(dir, &names, na);
    if (st != JC_OK) {
        jc_vec_free(&names);
        jc_arena_free(na);
        jc_arena_free(fa);
        return st;
    }
    for (i = 0; i < names.len; i++) {
        const char *name = *(char **)jc_vec_at(&names, i);
        char path[1100];
        char *text;
        jc_size nl = strlen(name);
        struct jc_session_meta meta;
        cJSON *root;
        cJSON *hist;
        if (nl < 6 || strcmp(name + nl - 5, ".json") != 0) {
            continue;
        }
        jc_arena_reset(fa); /* release the previous file before reading the next */
        jc_snprintf(path, sizeof(path), "%s/%s", dir, name);
        /* M198: skip anything that is not a regular file. A FIFO named *.json
         * here would block fopen/fread forever, hanging /sessions, /resume and
         * --continue startup with no output and no timeout. This is a SCANNED
         * path -- we found it, the user did not name it -- so a non-regular
         * entry is garbage, not a request. */
        if (!jc_is_regular_file(path)) {
            if (skipped != NULL) (*skipped)++;
            continue;
        }
        if (jc_read_file(path, &text, NULL, fa) != JC_OK) {
            /* Unreadable, or larger than JC_READ_FILE_MAX (64 MiB). Either way
             * the user has a session file they cannot see -- say so (M198). */
            if (skipped != NULL) (*skipped)++;
            continue;
        }
        /* M202: read the listing fields WITHOUT building a parse tree.
         *
         * cJSON_Parse costs ~64 bytes of node per JSON value plus a copy of every
         * string, so the price of listing tracked the number of VALUES rather
         * than the bytes -- and glibc keeps the freed nodes, making the peak
         * cumulative across the scan. `ls --all` over a real 243-file / 17 MB
         * store peaked at 193 MB RSS; on synthetic stores of identical byte size
         * the peak ran 9.3 / 16.9 / 237.8 MB at 2 / 200 / 2000 messages per
         * session. LOW_MEMORY.md's tiers start at 32 MB of RAM.
         *
         * jc_sessmeta_scan answers exactly the listing's questions in one pass
         * with no allocation, and returns 0 when it cannot be sure -- so anything
         * unusual (a foreign writer, a shape we do not expect) still gets a real
         * parse rather than a half-read row. */
        {
            struct jc_sessmeta sm;
            char stem[128];
            jc_size sl = nl - 5; /* strip ".json" */
            const char *in_id = NULL;
            const char *in_title = NULL;
            const char *in_alias = NULL;
            const char *in_ws = NULL;
            int nmsgs = 0;

            if (sl >= sizeof(stem)) sl = sizeof(stem) - 1;
            memcpy(stem, name, sl);
            stem[sl] = '\0';

            if (jc_sessmeta_scan(text, strlen(text), &sm)) {
                root = NULL; /* the fast path allocates nothing to free */
                in_id    = sm.has_id ? sm.id : NULL;
                in_title = sm.has_title ? sm.title : NULL;
                in_alias = sm.has_alias ? sm.alias : NULL;
                in_ws    = sm.has_workspace ? sm.workspace : NULL;
                nmsgs    = sm.nmsgs;
            } else {
                root = jc_json_parse(text);
                if (root == NULL) {
                    if (skipped != NULL) (*skipped)++;
                    continue;
                }
                in_id    = jc_json_get_str(root, "sessionId", NULL);
                in_title = jc_json_get_str(root, "title", NULL);
                in_alias = jc_json_get_str(root, "alias", NULL);
                in_ws    = jc_json_get_str(root, "workspaceDirectory", NULL);
                hist = jc_json_get_obj(root, "history");
                nmsgs = cJSON_IsArray(hist) ? cJSON_GetArraySize(hist) : 0;
            }

            /* M206: a .json file in the store that carries no sessionId is not
             * one of ours -- Continue CLI's sessions.json index (a JSON array),
             * a stray config, an editor scratch file. It used to be LISTED as an
             * "(untitled)" row named after its stem, because the M198 identity
             * rule takes the id from the FILENAME and the content fields simply
             * came back NULL. Skip it, and do NOT count it as an unreadable
             * session -- it was never a session, so a "N session file(s) could
             * not be read" warning would be a lie. jc_session_save always writes
             * sessionId, so a real session never trips this; a file with a WRONG
             * sessionId still HAS one and is still listed (the M198 mismatch
             * path, preserved). */
            if (in_id == NULL) {
                if (root != NULL) {
                    cJSON_Delete(root);
                }
                continue;
            }

            /* M198: identity comes from the FILENAME STEM, not the sessionId
             * field. session_path() rebuilds the path from the id, so a file
             * whose stem and sessionId disagree used to be listed yet resolvable
             * by neither name -- a permanently unreachable entry. The stem is the
             * name that can always be found again, so it wins; the disagreement
             * is logged, not silently repaired. jc_session_load_by_id applies the
             * matching rule. */
            if (in_id != NULL && strcmp(in_id, stem) != 0) {
                jc_logf(JC_LOG_WARN,
                        "session %s: sessionId '%s' disagrees with its filename;"
                        " using the filename", stem, in_id);
            }
            meta.id = jc_arena_strdup(a, stem);
            meta.title = jc_arena_strdup(a,
                (in_title != NULL) ? in_title : "(untitled)");
            meta.alias = (in_alias != NULL) ? jc_arena_strdup(a, in_alias) : NULL;
            meta.workspace = jc_arena_strdup(a,
                (in_ws != NULL) ? in_ws : ".");
            meta.nmsgs = nmsgs;
            meta.mtime = jc_file_mtime(path);
            if (root != NULL) {
                cJSON_Delete(root);
            }
            jc_vec_push(out, &meta);
        }
    }
    jc_vec_free(&names);
    jc_arena_free(na);
    jc_arena_free(fa); /* releases the last file's text */
    if (out->len > 1) {
        qsort(out->data, out->len, sizeof(struct jc_session_meta),
              meta_cmp_desc);
    }
    return JC_OK;
}

jc_status jc_session_load_recent_scoped(const char *workspace,
                                        struct jc_session *s,
                                        struct jc_arena *a)
{
    return jc_session_load_recent_scoped_ex(workspace, NULL, s, a);
}

jc_status jc_session_load_recent_scoped_ex(const char *workspace,
                                           const char *exclude_id,
                                           struct jc_session *s,
                                           struct jc_arena *a)
{
    struct jc_vec metas;
    jc_size i;
    char id[128];
    struct jc_arena *la; /* M197: the listing is a transient, not session state */

    id[0] = '\0';
    la = jc_arena_new(0);
    if (la == NULL) {
        return JC_ERR_OOM;
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    if (jc_session_list(&metas, la) != JC_OK) {
        jc_vec_free(&metas);
        jc_arena_free(la);
        return JC_ERR_NOTFOUND;
    }
    /* metas is newest-first; take the first matching the workspace scope. */
    for (i = 0; i < metas.len; i++) {
        struct jc_session_meta *mm =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        if (exclude_id != NULL && mm->id != NULL &&
            strcmp(mm->id, exclude_id) == 0) {
            continue; /* M198: never resume the session we are already in */
        }
        if (workspace == NULL ||
            (mm->workspace != NULL && strcmp(mm->workspace, workspace) == 0)) {
            /* Copy: mm->id lives on `la`, which is freed below. */
            if (mm->id != NULL) jc_snprintf(id, sizeof(id), "%s", mm->id);
            break;
        }
    }
    jc_vec_free(&metas);
    jc_arena_free(la);
    if (id[0] == '\0') {
        return JC_ERR_NOTFOUND;
    }
    return jc_session_load_by_id(id, s, a);
}

int jc_session_resolve_prefix(const char *prefix, char *out, jc_size cap,
                              struct jc_arena *a)
{
    struct jc_vec metas;
    const char **ids;
    jc_size i;
    int idx;
    struct jc_arena *la; /* M197: listing + ids[] are transients, not session state */

    (void)a; /* kept for API compatibility; the listing no longer outlives the call */
    la = jc_arena_new(0);
    if (la == NULL) {
        return -1;
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    if (jc_session_list(&metas, la) != JC_OK || metas.len == 0) {
        jc_vec_free(&metas);
        jc_arena_free(la);
        return -1;
    }
    ids = (const char **)jc_arena_alloc(la, sizeof(char *) * metas.len);
    if (ids == NULL) {
        jc_vec_free(&metas);
        jc_arena_free(la);
        return -1;
    }
    for (i = 0; i < metas.len; i++) {
        ids[i] = ((struct jc_session_meta *)jc_vec_at(&metas, i))->id;
    }
    idx = jc_id_prefix_unique(ids, (int)metas.len, prefix);
    if (idx >= 0) {
        jc_snprintf(out, cap, "%s", ids[idx]); /* copy before `la` dies */
    }
    jc_vec_free(&metas);
    jc_arena_free(la);
    return (idx >= 0) ? 0 : idx; /* 0 ok, -1 none, -2 ambiguous */
}

int jc_session_alias_valid(const char *alias)
{
    jc_size i, n;
    if (alias == NULL) return 0;
    n = (jc_size)strlen(alias);
    if (n == 0 || n > 64) return 0;
    for (i = 0; i < n; i++) {
        char c = alias[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

void jc_session_set_alias(struct jc_session *s, const char *alias)
{
    if (s == NULL) return;
    s->alias = (alias != NULL && s->arena != NULL)
             ? jc_arena_strdup(s->arena, alias) : NULL;
    s->meta_dirty = 1;
}

int jc_session_resolve_alias(const char *alias, char *out, jc_size cap,
                             struct jc_arena *a)
{
    struct jc_vec metas;
    jc_size i;
    int hit = -1; /* -1 none, -2 ambiguous, else the resolved index */
    struct jc_arena *la; /* M197: the listing is a transient, not session state */

    if (alias == NULL || alias[0] == '\0') return -1;
    (void)a; /* kept for API compatibility; the listing no longer outlives the call */
    la = jc_arena_new(0);
    if (la == NULL) {
        return -1;
    }
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    if (jc_session_list(&metas, la) != JC_OK) {
        jc_vec_free(&metas);
        jc_arena_free(la);
        return -1;
    }
    for (i = 0; i < metas.len; i++) {
        struct jc_session_meta *mm =
            (struct jc_session_meta *)jc_vec_at(&metas, i);
        if (mm->alias != NULL && strcmp(mm->alias, alias) == 0) {
            if (hit >= 0) { hit = -2; break; } /* two sessions share an alias */
            hit = (int)i;
        }
    }
    if (hit >= 0) {
        struct jc_session_meta *mm =
            (struct jc_session_meta *)jc_vec_at(&metas, (jc_size)hit);
        jc_snprintf(out, cap, "%s", mm->id); /* copy before `la` dies */
    }
    jc_vec_free(&metas);
    jc_arena_free(la);
    return (hit >= 0) ? 0 : hit;
}

jc_size jc_session_prune_select(const struct jc_session_meta *metas, jc_size n,
                                long keep, double cutoff, int *del)
{
    jc_size i;
    jc_size selected = 0;

    for (i = 0; i < n; i++) {
        int by_keep;
        int by_age;
        /* metas are newest-first (the jc_session_list order), so "keep the
         * `keep` newest" means indices >= keep are candidates. */
        by_keep = (keep >= 0) ? (i >= (jc_size)keep) : 1;
        by_age = (cutoff >= 0.0) ? (metas[i].mtime < cutoff) : 1;
        /* With NO criterion, nothing is selected (refuse-by-default: prune
         * is destructive); with both, both must agree (AND). */
        if (keep < 0 && cutoff < 0.0) {
            del[i] = 0;
        } else {
            del[i] = by_keep && by_age;
        }
        if (del[i]) {
            selected++;
        }
    }
    return selected;
}

void jc_session_store_stats(long *files_out, double *bytes_out)
{
    char dir[1024];
    struct jc_vec names;
    struct jc_arena *na;
    jc_size i;
    long files = 0;
    double bytes = 0.0;

    if (files_out != NULL) *files_out = 0;
    if (bytes_out != NULL) *bytes_out = 0.0;
    sessions_dir(dir, sizeof(dir));
    na = jc_arena_new(0);
    if (na == NULL) {
        return;
    }
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, na) == JC_OK) {
        for (i = 0; i < names.len; i++) {
            const char *nm = *(char **)jc_vec_at(&names, i);
            char path[1200];
            long sz;
            if (nm == NULL || strstr(nm, ".json") == NULL) {
                continue;
            }
            jc_snprintf(path, sizeof(path), "%s/%s", dir, nm);
            sz = jc_file_size(path);
            if (sz > 0) {
                bytes += (double)sz;
            }
            files++;
        }
    }
    jc_vec_free(&names);
    jc_arena_free(na);
    if (files_out != NULL) *files_out = files;
    if (bytes_out != NULL) *bytes_out = bytes;
}

jc_status jc_session_delete(const char *id)
{
    char path[1100];
    if (id == NULL || id[0] == '\0') return JC_ERR_INVALID;
    session_path(id, path, sizeof(path));
    if (remove(path) != 0) {
        /* Missing is fine (idempotent); other errors are real. */
        if (!jc_file_exists(path)) return JC_OK;
        return JC_ERR_IO;
    }
    return JC_OK;
}

/* True for a bare session-id token: [A-Za-z0-9_-] only. Used to decide whether
 * a token may be handed straight to session_path(), which interpolates it into
 * a path -- so '/' and '.' must never reach it (no traversal). */
static int id_is_plain(const char *id)
{
    jc_size i;
    if (id == NULL) return 0;
    for (i = 0; id[i] != '\0'; i++) {
        char c = id[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return 0;
    }
    return i > 0;
}

enum jc_session_open_result jc_session_open(struct jc_session *s,
                                            const char *id_or_prefix,
                                            int resume_recent,
                                            const char *scope_ws,
                                            const char *new_ws,
                                            struct jc_arena *a)
{
    if (id_or_prefix != NULL && id_or_prefix[0] != '\0') {
        char full[128];
        int r;
        /* M197: an exact id needs no listing at all. Try it directly before
         * paying a whole-store scan to resolve what may already be a full id
         * (the /resume <id> and /resume @alias paths always are). A prefix
         * won't name an existing file, so it falls through unchanged.
         * load_from_text only fails before touching *s, so the retry is safe. */
        if (id_is_plain(id_or_prefix) &&
            jc_session_load_by_id(id_or_prefix, s, a) == JC_OK) {
            return JC_SESSION_OPENED;
        }
        r = jc_session_resolve_prefix(id_or_prefix, full, sizeof(full), a);
        if (r == -2) return JC_SESSION_AMBIGUOUS;
        if (r != 0 || jc_session_load_by_id(full, s, a) != JC_OK) {
            return JC_SESSION_NONE;
        }
        return JC_SESSION_OPENED;
    }
    if (resume_recent &&
        jc_session_load_recent_scoped(scope_ws, s, a) == JC_OK) {
        return JC_SESSION_OPENED;
    }
    jc_session_new(s, new_ws, a);
    return JC_SESSION_CREATED;
}

void jc_session_set_title(struct jc_session *s, const char *title)
{
    if (s == NULL || title == NULL) return;
    s->title = jc_arena_strdup(s->arena, title);
    s->meta_dirty = 1;
}

/* --- transcript export (M34b) ----------------------------------------------
 *
 * jc_session_render walks the stored history and emits a human-readable
 * transcript in Markdown or a self-contained HTML document. It is pure: it
 * reads only the session struct and writes into the caller's jc_sb. The system
 * prompt is omitted (it isn't part of the conversation the user wants to share).
 */

/* Append `s` to `out` with HTML metacharacters escaped. */
static void html_escape(struct jc_sb *out, const char *s)
{
    jc_size i;
    if (s == NULL) {
        return;
    }
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c == '&') {
            jc_sb_append(out, "&amp;");
        } else if (c == '<') {
            jc_sb_append(out, "&lt;");
        } else if (c == '>') {
            jc_sb_append(out, "&gt;");
        } else {
            jc_sb_append_char(out, c);
        }
    }
}

/* A display heading for each role. */
static const char *role_heading(jc_role r)
{
    switch (r) {
    case JC_ROLE_USER:      return "User";
    case JC_ROLE_ASSISTANT: return "Assistant";
    case JC_ROLE_TOOL:      return "Tool result";
    default:                return "System";
    }
}

static const char *HTML_HEAD =
    "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";

/* Split across two literals to stay under the C90 509-char limit. */
static const char *HTML_STYLE_1 =
    "<style>\n"
    "body{font:16px/1.6 -apple-system,Segoe UI,Roboto,sans-serif;"
    "max-width:48rem;margin:2rem auto;padding:0 1rem;color:#1a1a1a}\n"
    ".meta{color:#666;font-size:.9rem;border-bottom:1px solid #ddd;"
    "padding-bottom:1rem;margin-bottom:1rem}\n"
    ".turn{margin:1.5rem 0}\n"
    ".role{font-weight:600;margin:0 0 .3rem}\n";
static const char *HTML_STYLE_2 =
    ".user .role{color:#0a58ca}\n.assistant .role{color:#198754}\n"
    ".tool .role{color:#9a6700}\n"
    "pre{background:#f6f8fa;border-radius:6px;padding:.8rem;overflow-x:auto;"
    "white-space:pre-wrap;word-wrap:break-word}\n"
    "code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}\n"
    ".err{color:#b42318}\n</style>\n";

/* Render the metadata header (title + session facts). */
static void render_header(struct jc_session *s, int html, struct jc_sb *out,
                          int nmsgs)
{
    const char *title = (s->title != NULL && s->title[0] != '\0')
                            ? s->title : "Session";
    const char *mode = jc_agent_mode_name((enum jc_agent_mode)s->mode);
    const char *ws = (s->workspace != NULL) ? s->workspace : "?";
    const char *id = (s->id != NULL) ? s->id : "?";

    if (html) {
        jc_sb_append(out, HTML_HEAD);
        jc_sb_append(out, "<title>");
        html_escape(out, title);
        jc_sb_append(out, "</title>\n");
        jc_sb_append(out, HTML_STYLE_1);
        jc_sb_append(out, HTML_STYLE_2);
        jc_sb_append(out, "</head>\n<body>\n<h1>");
        html_escape(out, title);
        jc_sb_append(out, "</h1>\n<div class=\"meta\">\n");
        jc_sb_append(out, "Session <code>");
        html_escape(out, id);
        jc_sb_append(out, "</code> &middot; ");
        jc_sb_append_fmt(out, "mode %s &middot; %d message%s &middot; ",
                         mode, nmsgs, nmsgs == 1 ? "" : "s");
        jc_sb_append(out, "<code>");
        html_escape(out, ws);
        jc_sb_append(out, "</code>\n</div>\n");
    } else {
        jc_sb_append(out, "# ");
        jc_sb_append(out, title);
        jc_sb_append(out, "\n\n");
        jc_sb_append_fmt(out, "- **Session:** `%s`\n", id);
        jc_sb_append_fmt(out, "- **Workspace:** `%s`\n", ws);
        jc_sb_append_fmt(out, "- **Mode:** %s\n", mode);
        jc_sb_append_fmt(out, "- **Messages:** %d\n\n", nmsgs);
        jc_sb_append(out, "---\n");
    }
}

/* Render one message's text + any tool calls / tool-result fences. */
static void render_message(struct jc_message *m, int html, struct jc_sb *out)
{
    const char *head = role_heading(m->role);
    jc_size nt = jc_msg_tool_call_count(m);
    jc_size k;

    if (html) {
        const char *cls = m->role == JC_ROLE_USER ? "user"
                        : m->role == JC_ROLE_ASSISTANT ? "assistant" : "tool";
        jc_sb_append_fmt(out, "<div class=\"turn %s\">\n<p class=\"role\">",
                         cls);
        jc_sb_append(out, head);
        if (m->role == JC_ROLE_TOOL && m->is_error) {
            jc_sb_append(out, " <span class=\"err\">(error)</span>");
        }
        jc_sb_append(out, "</p>\n");
        if (m->role == JC_ROLE_TOOL) {
            jc_sb_append(out, "<pre><code>");
            html_escape(out, m->content != NULL ? m->content : "");
            jc_sb_append(out, "</code></pre>\n");
        } else if (m->content != NULL && m->content[0] != '\0') {
            jc_sb_append(out, "<pre>");
            html_escape(out, m->content);
            jc_sb_append(out, "</pre>\n");
        }
        for (k = 0; k < nt; k++) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
            jc_sb_append(out, "<p class=\"role\">&rarr; <code>");
            html_escape(out, tc->name != NULL ? tc->name : "?");
            jc_sb_append(out, "</code></p>\n<pre><code>");
            html_escape(out, tc->arguments_json != NULL ? tc->arguments_json
                                                        : "{}");
            jc_sb_append(out, "</code></pre>\n");
        }
        jc_sb_append(out, "</div>\n");
    } else {
        jc_sb_append(out, "\n## ");
        jc_sb_append(out, head);
        if (m->role == JC_ROLE_TOOL && m->is_error) {
            jc_sb_append(out, " (error)");
        }
        jc_sb_append(out, "\n\n");
        if (m->role == JC_ROLE_TOOL) {
            jc_sb_append(out, "```\n");
            jc_sb_append(out, m->content != NULL ? m->content : "");
            jc_sb_append(out, "\n```\n");
        } else if (m->content != NULL && m->content[0] != '\0') {
            jc_sb_append(out, m->content);
            jc_sb_append(out, "\n");
        }
        for (k = 0; k < nt; k++) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
            jc_sb_append_fmt(out, "\n**Tool call:** `%s`\n```json\n",
                             tc->name != NULL ? tc->name : "?");
            jc_sb_append(out, tc->arguments_json != NULL ? tc->arguments_json
                                                         : "{}");
            jc_sb_append(out, "\n```\n");
        }
    }
}

/* Lowercase role key for the JSON projection. */
static const char *role_key(jc_role r)
{
    switch (r) {
    case JC_ROLE_USER:      return "user";
    case JC_ROLE_ASSISTANT: return "assistant";
    case JC_ROLE_TOOL:      return "tool";
    default:                return "system";
    }
}

/* Render the session as a structured JSON transcript projection (M165). A
 * tool call's `arguments` is embedded as a parsed object when it parses,
 * else as the raw string, so a consumer needn't re-parse a stringified blob.
 * Pure: builds a cJSON tree and appends its unformatted print to `out`. */
static void render_json(struct jc_session *s, struct jc_sb *out)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr;
    jc_size n = jc_history_len(&s->history);
    jc_size i;
    char *printed;

    if (root == NULL) {
        return;
    }
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "id", s->id != NULL ? s->id : "");
    /* M290: the build that EXPORTED this transcript. Additive to the M165
     * contract, so an existing consumer is unaffected; a supervisor archiving
     * transcripts gets provenance without a second lookup. */
    cJSON_AddStringToObject(root, "jichi", JC_VERSION);
    if (s->title != NULL && s->title[0] != '\0') {
        cJSON_AddStringToObject(root, "title", s->title);
    }
    cJSON_AddStringToObject(root, "workspace",
                            s->workspace != NULL ? s->workspace : "");
    cJSON_AddStringToObject(root, "mode",
                            jc_agent_mode_name((enum jc_agent_mode)s->mode));
    arr = cJSON_AddArrayToObject(root, "messages");
    for (i = 0; i < n && arr != NULL; i++) {
        struct jc_message *m = jc_history_get(&s->history, i);
        cJSON *mo;
        jc_size nt;
        jc_size k;
        if (m->role == JC_ROLE_SYSTEM) {
            continue; /* omitted, matching MD/HTML */
        }
        mo = cJSON_CreateObject();
        if (mo == NULL) {
            continue;
        }
        cJSON_AddStringToObject(mo, "role", role_key(m->role));
        if (m->content != NULL && m->content[0] != '\0') {
            cJSON_AddStringToObject(mo, "content", m->content);
        }
        if (m->role == JC_ROLE_TOOL) {
            if (m->tool_call_id != NULL) {
                cJSON_AddStringToObject(mo, "tool_call_id", m->tool_call_id);
            }
            if (m->is_error) {
                cJSON_AddBoolToObject(mo, "is_error", 1);
            }
        }
        nt = jc_msg_tool_call_count(m);
        if (nt > 0) {
            cJSON *tcs = cJSON_AddArrayToObject(mo, "tool_calls");
            for (k = 0; k < nt && tcs != NULL; k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                cJSON *to = cJSON_CreateObject();
                cJSON *args;
                if (to == NULL) {
                    continue;
                }
                cJSON_AddStringToObject(to, "id", tc->id != NULL ? tc->id : "");
                cJSON_AddStringToObject(to, "name",
                                        tc->name != NULL ? tc->name : "");
                args = (tc->arguments_json != NULL)
                           ? cJSON_Parse(tc->arguments_json) : NULL;
                if (args != NULL) {
                    cJSON_AddItemToObject(to, "arguments", args);
                } else {
                    cJSON_AddStringToObject(to, "arguments",
                        tc->arguments_json != NULL ? tc->arguments_json : "{}");
                }
                cJSON_AddItemToArray(tcs, to);
            }
        }
        cJSON_AddItemToArray(arr, mo);
    }

    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed != NULL) {
        jc_sb_append(out, printed);
        jc_sb_append_char(out, '\n');
        free(printed);
    }
}

jc_status jc_session_render(struct jc_session *s, int format, struct jc_sb *out)
{
    int html = (format == JC_EXPORT_HTML);
    jc_size n;
    jc_size i;
    int nmsgs = 0;

    if (s == NULL || out == NULL) {
        return JC_ERR_INVALID;
    }
    if (format == JC_EXPORT_JSON) {
        render_json(s, out);
        return JC_OK;
    }
    n = jc_history_len(&s->history);
    for (i = 0; i < n; i++) {
        if (jc_history_get(&s->history, i)->role != JC_ROLE_SYSTEM) {
            nmsgs++;
        }
    }

    render_header(s, html, out, nmsgs);
    for (i = 0; i < n; i++) {
        struct jc_message *m = jc_history_get(&s->history, i);
        if (m->role == JC_ROLE_SYSTEM) {
            continue;
        }
        render_message(m, html, out);
    }
    if (html) {
        jc_sb_append(out, "</body>\n</html>\n");
    }
    return JC_OK;
}

/* ---- M350: resume drift -------------------------------------------------- */

static void drift_push_unique(struct jc_vec *out, const char *p)
{
    jc_size i;
    char *d;

    if (p == NULL || p[0] == '\0' || out->len >= 128) {
        return;
    }
    for (i = 0; i < out->len; i++) {
        if (strcmp(*(char **)jc_vec_at(out, i), p) == 0) {
            return;
        }
    }
    d = jc_strdup(p);
    if (d != NULL && jc_vec_push(out, &d) != JC_OK) {
        free(d);
    }
}

/* Pull the path argument(s) out of one stored file-tool call. Non-file tools
 * are ignored: a search result goes stale too, but its arguments do not name
 * the files it matched, so the honest scope is the four tools whose `path`
 * IS the belief (stated in the docs as a limit, not hidden). */
static void drift_collect(struct jc_vec *out, const char *name,
                          const char *args_json)
{
    cJSON *root;

    if (name == NULL || args_json == NULL) {
        return;
    }
    if (strcmp(name, "read_file") != 0 && strcmp(name, "write_file") != 0 &&
        strcmp(name, "edit_file") != 0 && strcmp(name, "apply_patch") != 0) {
        return;
    }
    root = cJSON_Parse(args_json);
    if (root == NULL) {
        return;
    }
    if (strcmp(name, "apply_patch") == 0) {
        cJSON *edits = jc_json_get_obj(root, "edits");
        int n = cJSON_GetArraySize(edits);
        int i;
        for (i = 0; i < n; i++) {
            cJSON *e = cJSON_GetArrayItem(edits, i);
            drift_push_unique(out, jc_json_get_str(e, "path", NULL));
        }
    } else {
        drift_push_unique(out, jc_json_get_str(root, "path", NULL));
    }
    cJSON_Delete(root);
}

void jc_session_believed_paths(const struct jc_history *hist,
                               struct jc_vec *out)
{
    jc_size n;
    jc_size i;

    if (hist == NULL || out == NULL) {
        return;
    }
    n = jc_history_len((struct jc_history *)hist);
    for (i = 0; i < n; i++) {
        struct jc_message *m = jc_history_get((struct jc_history *)hist, i);
        jc_size j;
        int matched = 0;
        if (m == NULL || m->role != JC_ROLE_TOOL || m->is_error ||
            m->tool_call_id == NULL) {
            continue;
        }
        /* The originating call is found BACKWARDS from the result: a
         * tool_call_id is unique only within one provider response, so a
         * forward scan charges everything to the first reuse (M315). */
        for (j = i; j > 0 && !matched; j--) {
            struct jc_message *a =
                jc_history_get((struct jc_history *)hist, j - 1);
            jc_size k;
            if (a == NULL || a->role != JC_ROLE_ASSISTANT) {
                continue;
            }
            for (k = 0; k < a->tool_calls.len; k++) {
                struct jc_tool_call *c =
                    (struct jc_tool_call *)jc_vec_at(&a->tool_calls, k);
                if (c->id != NULL && strcmp(c->id, m->tool_call_id) == 0) {
                    drift_collect(out, c->name, c->arguments_json);
                    matched = 1;
                    break;
                }
            }
        }
    }
}

jc_status jc_session_drift_names(const struct jc_session *s,
                                 struct jc_sb *out)
{
    char spath[600];
    double saved_at;
    struct jc_vec paths;
    jc_size i;

    if (s == NULL || out == NULL || s->id == NULL) {
        return JC_ERR_INVALID;
    }
    session_path(s->id, spath, sizeof(spath));
    saved_at = jc_file_mtime(spath);
    if (saved_at < 0) {
        return JC_ERR_NOTFOUND;
    }
    jc_vec_init(&paths, sizeof(char *));
    jc_session_believed_paths(&s->history, &paths);
    for (i = 0; i < paths.len; i++) {
        const char *p = *(char **)jc_vec_at(&paths, i);
        char full[1200];
        double mt;
        if (p[0] == '/') {
            jc_snprintf(full, sizeof(full), "%s", p);
        } else {
            jc_snprintf(full, sizeof(full), "%s/%s",
                        s->workspace != NULL ? s->workspace : ".", p);
        }
        mt = jc_file_mtime(full);
        /* Strictly newer, or gone: both mean the conversation's picture of
         * this file predates the disk's. The agent's own last-turn edits are
         * older than the save that followed them, so they never trip this. */
        if (mt < 0 || mt > saved_at) {
            jc_sb_append(out, p);
            jc_sb_append_char(out, '\n');
        }
    }
    for (i = 0; i < paths.len; i++) {
        free(*(char **)jc_vec_at(&paths, i));
    }
    jc_vec_free(&paths);
    return JC_OK;
}

void jc_session_drift_render(const char *names, struct jc_sb *out)
{
    jc_size n = 0;
    jc_size listed = 0;
    jc_size i = 0;
    jc_size len;

    if (out == NULL || names == NULL) {
        return;
    }
    len = (jc_size)strlen(names);
    while (i < len) {
        jc_size ls = i;
        while (i < len && names[i] != '\n') {
            i++;
        }
        if (i > ls) {
            n++;
        }
        if (i < len) {
            i++;
        }
    }
    if (n == 0) {
        return;
    }
    jc_sb_append_fmt(out,
        "[resume] since this conversation last ran, %lu file(s) it worked "
        "with changed on disk or disappeared: ", (unsigned long)n);
    i = 0;
    while (i < len && listed < 8) {
        jc_size ls = i;
        while (i < len && names[i] != '\n') {
            i++;
        }
        if (i > ls) {
            if (listed > 0) {
                jc_sb_append(out, ", ");
            }
            jc_sb_append_n(out, names + ls, i - ls);
            listed++;
        }
        if (i < len) {
            i++;
        }
    }
    if (n > listed) {
        jc_sb_append_fmt(out, " (+%lu more)", (unsigned long)(n - listed));
    }
    jc_sb_append(out,
        ". Tool results earlier in this conversation may describe their "
        "PRE-change content -- re-read these files before relying on or "
        "editing them.");
}
