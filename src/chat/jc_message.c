/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_message.c - chat data model (see jc_message.h). */

#include "jc_message.h"
#include "jc_str.h"
#include "jc_utf8.h"

#include <stdlib.h>
#include <string.h>

void jc_history_init(struct jc_history *h)
{
    jc_vec_init(&h->messages, sizeof(struct jc_message));
    h->gen = 0;
}

/* Duplicate message content, repairing ill-formed UTF-8 on the way in (M191).
 *
 * This is the chokepoint every message body passes -- user turns, assistant
 * text, and every tool result -- so it is the one place that can guarantee the
 * history never holds a byte the wire cannot carry. Sources are real: a tool
 * output capped mid-character, `grep` over a binary file, a latin-1 source file.
 * The alternative, catching it per producer, is a promise about code not yet
 * written; a split character costs an entire wedged run (docs/ANECDOTES.md #22).
 *
 * Safe here because every caller passes a COMPLETE string: streamed assistant
 * deltas accumulate in the provider's scratch buffer and only reach a message at
 * flush, so a character legitimately split across two SSE chunks is whole again
 * before it arrives. */
static char *dup_content(const char *s)
{
    char *fixed = NULL;
    if (s == NULL) {
        return NULL;
    }
    if (jc_utf8_sanitize(s, (jc_size)strlen(s), &fixed, NULL)) {
        return fixed; /* already a fresh NUL-terminated buffer */
    }
    return jc_strdup(s);
}

static void free_tool_call(struct jc_tool_call *tc)
{
    free(tc->id);
    free(tc->name);
    free(tc->arguments_json);
}

static void free_message(struct jc_message *m)
{
    jc_size i;
    free(m->content);
    free(m->tool_call_id);
    for (i = 0; i < m->tool_calls.len; i++) {
        free_tool_call((struct jc_tool_call *)jc_vec_at(&m->tool_calls, i));
    }
    jc_vec_free(&m->tool_calls);
    for (i = 0; i < m->images.len; i++) {
        struct jc_image *img = (struct jc_image *)jc_vec_at(&m->images, i);
        free(img->media_type);
        free(img->data);
    }
    jc_vec_free(&m->images);
}

void jc_history_free(struct jc_history *h)
{
    jc_size i;
    for (i = 0; i < h->messages.len; i++) {
        free_message((struct jc_message *)jc_vec_at(&h->messages, i));
    }
    jc_vec_free(&h->messages);
}

jc_size jc_history_len(const struct jc_history *h)
{
    return h->messages.len;
}

struct jc_message *jc_history_get(struct jc_history *h, jc_size i)
{
    return (struct jc_message *)jc_vec_at(&h->messages, i);
}

struct jc_message *jc_history_add(struct jc_history *h, jc_role role,
                                  const char *content)
{
    struct jc_message *m;
    m = (struct jc_message *)jc_vec_push_slot(&h->messages);
    if (m == NULL) {
        return NULL;
    }
    h->gen++;
    m->role = role;
    m->content = dup_content(content);
    m->tool_call_id = NULL;
    m->is_error = 0;
    jc_vec_init(&m->tool_calls, sizeof(struct jc_tool_call));
    jc_vec_init(&m->images, sizeof(struct jc_image));
    return m;
}

struct jc_message *jc_history_add_operator(struct jc_history *h,
                                           const char *text)
{
    struct jc_sb msg;
    struct jc_message *m = NULL;
    if (h == NULL || text == NULL || text[0] == '\0') {
        return NULL;
    }
    jc_sb_init(&msg);
    jc_sb_append(&msg, "[operator] ");
    jc_sb_append(&msg, text);
    if (msg.data != NULL) {
        m = jc_history_add(h, JC_ROLE_USER, msg.data);
    }
    jc_sb_free(&msg);
    return m;
}

struct jc_message *jc_history_add_tool_result(struct jc_history *h,
                                              const char *tool_call_id,
                                              const char *content,
                                              int is_error)
{
    struct jc_message *m = jc_history_add(h, JC_ROLE_TOOL, content);
    if (m == NULL) {
        return NULL;
    }
    m->tool_call_id = (tool_call_id != NULL) ? jc_strdup(tool_call_id) : NULL;
    m->is_error = is_error;
    return m;
}

jc_status jc_msg_add_tool_call(struct jc_message *m, const char *id,
                               const char *name, const char *arguments_json)
{
    struct jc_tool_call *tc;
    tc = (struct jc_tool_call *)jc_vec_push_slot(&m->tool_calls);
    if (tc == NULL) {
        return JC_ERR_OOM;
    }
    tc->id = (id != NULL) ? jc_strdup(id) : NULL;
    tc->name = (name != NULL) ? jc_strdup(name) : NULL;
    tc->arguments_json = (arguments_json != NULL)
                         ? jc_strdup(arguments_json) : NULL;
    return JC_OK;
}

jc_size jc_msg_tool_call_count(const struct jc_message *m)
{
    return m->tool_calls.len;
}

struct jc_tool_call *jc_msg_tool_call_at(struct jc_message *m, jc_size i)
{
    return (struct jc_tool_call *)jc_vec_at(&m->tool_calls, i);
}

jc_status jc_msg_add_image(struct jc_message *m, const char *media_type,
                           const char *data)
{
    struct jc_image *img = (struct jc_image *)jc_vec_push_slot(&m->images);
    if (img == NULL) {
        return JC_ERR_OOM;
    }
    img->media_type = (media_type != NULL) ? jc_strdup(media_type) : NULL;
    img->data = (data != NULL) ? jc_strdup(data) : NULL;
    return JC_OK;
}

jc_status jc_msg_add_image_owned(struct jc_message *m, const char *media_type,
                                 char *data)
{
    struct jc_image *img = (struct jc_image *)jc_vec_push_slot(&m->images);
    if (img == NULL) {
        return JC_ERR_OOM; /* caller keeps ownership of `data` */
    }
    img->media_type = (media_type != NULL) ? jc_strdup(media_type) : NULL;
    img->data = data; /* ownership transferred; freed by free_message */
    return JC_OK;
}

jc_size jc_msg_image_count(const struct jc_message *m)
{
    return m->images.len;
}

struct jc_image *jc_msg_image_at(struct jc_message *m, jc_size i)
{
    return (struct jc_image *)jc_vec_at(&m->images, i);
}

void jc_history_drop_front(struct jc_history *h, jc_size count)
{
    struct jc_vec *v = &h->messages;
    struct jc_message *base;
    jc_size i;

    if (count > v->len) {
        count = v->len;
    }
    if (count == 0) {
        return;
    }
    h->gen++;
    base = (struct jc_message *)v->data;
    for (i = 0; i < count; i++) {
        free_message(&base[i]);
    }
    if (v->len > count) {
        memmove(base, base + count,
                (size_t)(v->len - count) * sizeof(struct jc_message));
    }
    v->len -= count;
}

void jc_history_truncate(struct jc_history *h, jc_size len)
{
    struct jc_vec *v = &h->messages;
    struct jc_message *base;
    jc_size i;

    if (len >= v->len) {
        return;
    }
    h->gen++;
    base = (struct jc_message *)v->data;
    for (i = len; i < v->len; i++) {
        free_message(&base[i]);
    }
    v->len = len;
}

jc_status jc_msg_set_content(struct jc_message *m, const char *content)
{
    char *copy = dup_content(content);
    if (content != NULL && copy == NULL) {
        return JC_ERR_OOM;
    }
    free(m->content);
    m->content = copy;
    return JC_OK;
}

jc_status jc_msg_tool_call_set_args(struct jc_tool_call *tc,
                                    const char *args_json)
{
    char *copy = dup_content(args_json);
    if (args_json != NULL && copy == NULL) {
        return JC_ERR_OOM;
    }
    free(tc->arguments_json);
    tc->arguments_json = copy;
    return JC_OK;
}

const char *jc_role_str(jc_role r)
{
    switch (r) {
        case JC_ROLE_SYSTEM:    return "system";
        case JC_ROLE_USER:      return "user";
        case JC_ROLE_ASSISTANT: return "assistant";
        case JC_ROLE_TOOL:      return "tool";
        default:                return "user";
    }
}

/* --- M364: the wire-shape validator ---------------------------------------
 *
 * The history feeds both provider serializers, and the contract they rely on
 * was folklore spread across ~69 mutation sites in 12 files: every tool call
 * answered exactly once by the tool-result run that follows it, every result
 * claiming a call from the assistant message it follows, the conversation
 * opening with a user turn. A violation is an HTTP 400 mid-run (a dead
 * unattended run) or a request the server accepts and the model misreads.
 *
 * Deliberate NON-checks, each verified against the code before being left
 * out: placeholder-shaped assistant messages are LEGAL anywhere (an empty
 * model answer persists in history -- M167 warns about it -- and both
 * serializers skip them); consecutive user-role messages are legal (tool
 * results coalesce into a user message and the notice family appends user
 * notes after them; shipped clean against the Messages API since M159); a
 * TRUNCATED assistant's calls are still answered (M334 executes them and
 * error results land), so no truncation exemption exists; and an abort
 * cannot strand a call (the only abort exit is at round top, and aborted
 * tools still append error results). */

static void hc_violation(int *count, struct jc_sb *out, jc_size idx,
                         const char *what, const char *id)
{
    (*count)++;
    if (out == NULL) {
        return;
    }
    if (*count > 8) {
        return; /* bounded samples; the count still tells the whole story */
    }
    jc_sb_append_fmt(out, "history[%lu]: %s", (unsigned long)idx, what);
    if (id != NULL && id[0] != '\0') {
        jc_sb_append_fmt(out, " '%s'", id);
    }
    jc_sb_append_char(out, '\n');
}

int jc_history_check(const struct jc_history *hist, struct jc_sb *out)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n;
    jc_size i;
    int violations = 0;
    int seen_nonsystem = 0;

    if (h == NULL) {
        return 0;
    }
    n = jc_history_len(h);
    for (i = 0; i < n; i++) {
        struct jc_message *m = jc_history_get(h, i);
        if (m == NULL) {
            continue;
        }
        if (m->role == JC_ROLE_SYSTEM) {
            continue; /* legacy sessions may carry one; serializers skip it */
        }
        if (!seen_nonsystem) {
            seen_nonsystem = 1;
            if (m->role != JC_ROLE_USER) {
                hc_violation(&violations, out, i,
                             "the first message is not a user turn", NULL);
            }
        }
        if (m->role == JC_ROLE_USER) {
            if ((m->content == NULL || m->content[0] == '\0') &&
                jc_msg_image_count(m) == 0) {
                hc_violation(&violations, out, i,
                             "empty user message (no text, no images)", NULL);
            }
            continue;
        }
        if (m->role == JC_ROLE_TOOL) {
            /* Orphan hunt: the id must be declared by the nearest preceding
             * assistant-with-calls, with only TOOL messages in between --
             * the shape the loop writes (results land immediately after
             * their round; notes come after the whole run). */
            jc_size j = i;
            struct jc_message *anchor = NULL;
            if (m->tool_call_id == NULL || m->tool_call_id[0] == '\0') {
                hc_violation(&violations, out, i,
                             "tool result with no tool_call_id", NULL);
                continue;
            }
            while (j > 0) {
                struct jc_message *p = jc_history_get(h, j - 1);
                if (p->role == JC_ROLE_TOOL) {
                    j--;
                    continue;
                }
                if (p->role == JC_ROLE_ASSISTANT &&
                    jc_msg_tool_call_count(p) > 0) {
                    anchor = p;
                }
                break;
            }
            if (anchor == NULL) {
                hc_violation(&violations, out, i,
                             "orphan tool result: no preceding assistant "
                             "tool round", m->tool_call_id);
            } else {
                jc_size k, nc = jc_msg_tool_call_count(anchor);
                int found = 0;
                for (k = 0; k < nc; k++) {
                    struct jc_tool_call *tc = jc_msg_tool_call_at(anchor, k);
                    if (tc->id != NULL &&
                        strcmp(tc->id, m->tool_call_id) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    hc_violation(&violations, out, i,
                                 "tool result answers no call in its round",
                                 m->tool_call_id);
                }
            }
            continue;
        }
        if (m->role == JC_ROLE_ASSISTANT &&
            jc_msg_tool_call_count(m) > 0) {
            /* Every call answered exactly once by the run that follows. */
            jc_size k, nc = jc_msg_tool_call_count(m);
            for (k = 0; k < nc; k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                int answers = 0;
                jc_size j;
                if (tc->id == NULL || tc->id[0] == '\0') {
                    hc_violation(&violations, out, i,
                                 "assistant tool call with no id",
                                 tc->name);
                    continue;
                }
                for (j = i + 1; j < n; j++) {
                    struct jc_message *r = jc_history_get(h, j);
                    if (r->role != JC_ROLE_TOOL) {
                        break;
                    }
                    if (r->tool_call_id != NULL &&
                        strcmp(r->tool_call_id, tc->id) == 0) {
                        answers++;
                    }
                }
                if (answers == 0) {
                    hc_violation(&violations, out, i,
                                 "unanswered tool call", tc->id);
                } else if (answers > 1) {
                    hc_violation(&violations, out, i,
                                 "tool call answered more than once", tc->id);
                }
            }
        }
    }
    if (out != NULL && violations > 8) {
        jc_sb_append_fmt(out, "(+%d more)\n", violations - 8);
    }
    return violations;
}
