/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_todo.c - todowrite/todoread tools + the jc_todo_list helpers.
 *
 * The agent uses these to track a structured task list across a long run. The
 * list belongs to the SESSION (M606: struct jc_session owns it, app->todos
 * points at the live session's copy, and the session codec saves and restores
 * it -- before M606 it lived on jc_app alone, so `--continue` resumed a history
 * full of todos beside an empty list, silently). Both tools are read-only (no
 * fs/exec) so they run without a permission prompt, and both are restricted to
 * the main agent (a subagent must not stomp the user's list).
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_todo.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

void jc_todo_init(struct jc_todo_list *t)
{
    jc_vec_init(&t->items, sizeof(struct jc_todo_item));
    t->gen = 0;
}

void jc_todo_clear(struct jc_todo_list *t)
{
    jc_size i;
    for (i = 0; i < t->items.len; i++) {
        struct jc_todo_item *it =
            (struct jc_todo_item *)jc_vec_at(&t->items, i);
        free(it->content);
    }
    jc_vec_free(&t->items);
    jc_vec_init(&t->items, sizeof(struct jc_todo_item));
    t->gen++;
}

jc_status jc_todo_copy(struct jc_todo_list *dst, const struct jc_todo_list *src)
{
    jc_size i;
    jc_todo_clear(dst);
    for (i = 0; i < src->items.len; i++) {
        const struct jc_todo_item *it =
            (const struct jc_todo_item *)jc_vec_at((struct jc_vec *)&src->items, i);
        struct jc_todo_item copy;
        copy.content = jc_strdup(it->content != NULL ? it->content : "");
        copy.status = it->status;
        if (copy.content == NULL || jc_vec_push(&dst->items, &copy) != JC_OK) {
            free(copy.content);
            jc_todo_clear(dst);
            return JC_ERR_OOM;
        }
    }
    return JC_OK;
}

const char *jc_todo_status_wire(int status)
{
    if (status == JC_TODO_DONE) {
        return "completed";
    }
    if (status == JC_TODO_IN_PROGRESS) {
        return "in_progress";
    }
    return "pending";
}

int jc_todo_status_from_wire(const char *s)
{
    if (s == NULL) {
        return JC_TODO_PENDING;
    }
    if (strcmp(s, "completed") == 0 || strcmp(s, "complete") == 0 ||
        strcmp(s, "done") == 0) {
        return JC_TODO_DONE;
    }
    if (strcmp(s, "in_progress") == 0 || strcmp(s, "in-progress") == 0 ||
        strcmp(s, "doing") == 0) {
        return JC_TODO_IN_PROGRESS;
    }
    return JC_TODO_PENDING;
}

void jc_todo_free(struct jc_todo_list *t)
{
    /* M199: content is malloc-owned now (it used to be strdup'd onto the
     * session arena, leaking a copy per list revision), so free each item. */
    {
        jc_size i;
        for (i = 0; i < t->items.len; i++) {
            struct jc_todo_item *it =
                (struct jc_todo_item *)jc_vec_at(&t->items, i);
            free(it->content);
        }
    }
    jc_vec_free(&t->items);
}

const char *jc_todo_status_word(int status)
{
    if (status == JC_TODO_DONE) {
        return "complete";
    }
    if (status == JC_TODO_IN_PROGRESS) {
        return "in-progress";
    }
    return "pending";
}

/* One recognised state marker: the literal text, its length, and the state. */
struct todo_marker {
    const char *word;
    int         status;
};

const char *jc_todo_strip_marker(const char *content, int *status_out,
                                 int *found)
{
    /* Longest-first within each prefix group, so "in-progress" is not matched as
     * the shorter "in" of some other word and "completed" is not cut to
     * "complete". `incomplete` maps to PENDING deliberately: it says the work is
     * not done, and reading it as in-progress would claim work had STARTED, which
     * is a stronger statement than the word makes. */
    static const struct todo_marker WORDS[] = {
        { "in-progress", JC_TODO_IN_PROGRESS },
        { "in_progress", JC_TODO_IN_PROGRESS },
        { "in progress", JC_TODO_IN_PROGRESS },
        { "incomplete",  JC_TODO_PENDING     },
        { "completed",   JC_TODO_DONE        },
        { "complete",    JC_TODO_DONE        },
        { "pending",     JC_TODO_PENDING     },
        { "doing",       JC_TODO_IN_PROGRESS },
        { "done",        JC_TODO_DONE        },
        { "todo",        JC_TODO_PENDING     },
        { NULL,          0                   }
    };
    const char *p;
    int i;

    if (found != NULL) {
        *found = 0;
    }
    if (content == NULL) {
        return "";
    }
    p = content;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    /* An optional markdown bullet, which carries no state by itself. */
    if ((*p == '-' || *p == '*' || *p == '+') && (p[1] == ' ' || p[1] == '\t')) {
        p += 2;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }
    /* A checkbox. `[~]` is jichi's own in-progress rendering from before M299, so
     * a list round-tripped through an older transcript still reads correctly. */
    if (p[0] == '[' && p[1] != '\0' && p[2] == ']') {
        int st = -1;
        if (p[1] == ' ') {
            st = JC_TODO_PENDING;
        } else if (p[1] == 'x' || p[1] == 'X') {
            st = JC_TODO_DONE;
        } else if (p[1] == '~' || p[1] == '/' || p[1] == '-') {
            st = JC_TODO_IN_PROGRESS;
        }
        if (st >= 0) {
            p += 3;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (status_out != NULL) {
                *status_out = st;
            }
            if (found != NULL) {
                *found = 1;
            }
            return p;
        }
    }
    /* A state word followed by a colon. */
    for (i = 0; WORDS[i].word != NULL; i++) {
        jc_size n = (jc_size)strlen(WORDS[i].word);
        jc_size k;
        for (k = 0; k < n; k++) {
            unsigned char a = (unsigned char)p[k];
            unsigned char b = (unsigned char)WORDS[i].word[k];
            if (a >= 'A' && a <= 'Z') {
                a = (unsigned char)(a - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (k == n && p[n] == ':') {
            p += n + 1;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (status_out != NULL) {
                *status_out = WORDS[i].status;
            }
            if (found != NULL) {
                *found = 1;
            }
            return p;
        }
    }
    return p;
}

/* Render the current list into a result string. */
static void format_list(struct jc_todo_list *t, struct jc_tool_result *out)
{
    struct jc_sb sb;
    jc_size i;

    jc_sb_init(&sb);
    if (t->items.len == 0) {
        jc_sb_append(&sb, "(todo list is empty)");
    } else {
        jc_sb_append(&sb, "Todos:\n");
        for (i = 0; i < t->items.len; i++) {
            struct jc_todo_item *it =
                (struct jc_todo_item *)jc_vec_at(&t->items, i);
            /* M299: a word, not a box. Padded to a fixed width so the state
             * reads as a column and the tasks line up -- "in-progress" is the
             * longest at 11. */
            jc_sb_append_fmt(&sb, "%-12s ", jc_todo_status_word(it->status));
            jc_sb_append(&sb, it->content != NULL ? it->content : "");
            jc_sb_append_char(&sb, '\n');
        }
    }
    out->content = jc_sb_finish(&sb);
    out->is_error = 0;
    jc_sb_free(&sb);
}

static cJSON *todowrite_schema(void)
{
    cJSON *s = tu_schema_begin();
    cJSON *props = cJSON_GetObjectItem(s, "properties");
    cJSON *req = cJSON_GetObjectItem(s, "required");
    cJSON *todos = cJSON_CreateObject();
    cJSON *items = cJSON_CreateObject();
    cJSON *iprops = cJSON_CreateObject();
    cJSON *content = cJSON_CreateObject();
    cJSON *status = cJSON_CreateObject();
    cJSON *enumv = cJSON_CreateArray();
    cJSON *ireq = cJSON_CreateArray();

    cJSON_AddStringToObject(content, "type", "string");
    cJSON_AddStringToObject(status, "type", "string");
    cJSON_AddItemToArray(enumv, cJSON_CreateString("pending"));
    cJSON_AddItemToArray(enumv, cJSON_CreateString("in_progress"));
    cJSON_AddItemToArray(enumv, cJSON_CreateString("completed"));
    cJSON_AddItemToObject(status, "enum", enumv);

    cJSON_AddItemToObject(iprops, "content", content);
    cJSON_AddItemToObject(iprops, "status", status);
    cJSON_AddItemToArray(ireq, cJSON_CreateString("content"));
    cJSON_AddItemToArray(ireq, cJSON_CreateString("status"));

    cJSON_AddStringToObject(items, "type", "object");
    cJSON_AddItemToObject(items, "properties", iprops);
    cJSON_AddItemToObject(items, "required", ireq);

    cJSON_AddStringToObject(todos, "type", "array");
    cJSON_AddStringToObject(todos, "description",
                            "The full todo list; replaces the current one.");
    cJSON_AddItemToObject(todos, "items", items);

    cJSON_AddItemToObject(props, "todos", todos);
    cJSON_AddItemToArray(req, cJSON_CreateString("todos"));
    return s;
}

static jc_status todowrite_run(const cJSON *args, struct jc_tool_result *out,
                               struct jc_app *app)
{
    cJSON *todos = cJSON_GetObjectItem(args, "todos");
    cJSON *parsed = NULL; /* owned copy if `todos` arrived stringified */
    cJSON *e;
    struct jc_todo_list *t = app->todos;

    if (t == NULL) {
        /* No live session holds a list (a subcommand with no conversation).
         * Unreachable from the agent loop, which always runs inside one. */
        tu_err(out, "error: no session to hold a task list");
        return JC_OK;
    }

    /* Tolerate a stringified array: some models serialize a nested-JSON arg as a
     * string (todos: "[{...}]") instead of a real array. Parse it and use the
     * array rather than rejecting an otherwise-valid call (grounded: zigodot
     * dogfood, todo_write failed 28/28 this way). */
    if (cJSON_IsString(todos) && todos->valuestring != NULL) {
        parsed = cJSON_Parse(todos->valuestring);
        if (cJSON_IsArray(parsed)) {
            todos = parsed;
        }
    }
    if (!cJSON_IsArray(todos)) {
        if (parsed != NULL) {
            cJSON_Delete(parsed);
        }
        tu_err(out, "error: 'todos' must be an array");
        return JC_OK;
    }

    /* Replace the whole list. M199: the previous items' content was strdup'd
     * onto the session arena, so every revision of the list leaked its
     * predecessor -- a model that rewrites a 10-item list 200 times in a run
     * retained 2000 strings. The content is malloc-owned now and freed here.
     * M606: the clear bumps the list's `gen`, which is what makes the next
     * session save write the new list. */
    jc_todo_clear(t);
    e = todos->child;
    while (e != NULL) {
        struct jc_todo_item it;
        cJSON *jc = cJSON_GetObjectItem(e, "content");
        cJSON *js = cJSON_GetObjectItem(e, "status");
        const char *content = (cJSON_IsString(jc)) ? jc->valuestring : "";
        const char *status = (cJSON_IsString(js)) ? js->valuestring : NULL;
        int marked = 0;
        int marked_status = JC_TODO_PENDING;
        const char *text;

        /* M299: normalise a marker the model wrote INTO the content -- `- [ ] foo`,
         * `complete: foo`. It will do that out of habit for a long time, and the
         * alternative is a list whose text carries a state that contradicts the
         * `status` field. Tolerant, not strict.
         *
         * An in-content marker WINS over the status field when they disagree,
         * because it is the thing the model just wrote in prose and therefore the
         * more likely intent -- and because leaving both would render "pending:
         * [x] foo", which is exactly the drift this milestone is about. */
        text = jc_todo_strip_marker(content, &marked_status, &marked);
        it.content = jc_strdup(text != NULL ? text : "");       /* M199 */
        it.status = marked ? marked_status : jc_todo_status_from_wire(status);
        jc_vec_push(&t->items, &it);
        e = e->next;
    }

    format_list(t, out);
    if (parsed != NULL) {
        cJSON_Delete(parsed);
    }
    return JC_OK;
}

static cJSON *todoread_schema(void)
{
    return tu_schema_begin();
}

static jc_status todoread_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    (void)args;
    if (app->todos == NULL) {
        tu_err(out, "error: no session to hold a task list");
        return JC_OK;
    }
    format_list(app->todos, out);
    return JC_OK;
}

static const struct jc_tool TODOWRITE_TOOL = {
    "todowrite",
    "Create or update the task list for the current work. Pass the full list "
    "each time (it replaces the previous one); mark items pending, in_progress, "
    "or completed. Put each task in `content` as a plain sentence and its state "
    "in `status` -- no checkboxes like \"- [ ] task\" in the content: the list "
    "renders as \"pending: task\" / \"in-progress: task\" / \"complete: task\", "
    "and a state edited in place is a state that stops being true. Saved with "
    "the session; restored on resume.",
    todowrite_schema,
    1, /* readonly: no fs/exec, just agent state */
    todowrite_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    1 /* main_agent_only: the list belongs to the user's session (M436) */
};

static const struct jc_tool TODOREAD_TOOL = {
    "todoread",
    "Read the current task list.",
    todoread_schema,
    1, /* readonly */
    todoread_run,
    NULL, NULL, NULL, /* not a dynamic (MCP) tool */
    1 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_todowrite(void)
{
    return &TODOWRITE_TOOL;
}

const struct jc_tool *jc_tool_todoread(void)
{
    return &TODOREAD_TOOL;
}
