/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_todo.c - todowrite/todoread round trip and formatting. */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_app.h"
#include "jc_todo.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

/* M606: the wire words and the deep copy the session codec relies on. The
 * wire word is what the session file holds and what the todowrite schema's
 * enum names; the parser is lenient (a file on disk, a model's argument), the
 * printer is exact. The copy backs /fork and must be deep, or two sessions
 * would free one string. */
static void test_wire_and_copy(void)
{
    struct jc_todo_list a, b;
    struct jc_todo_item it;
    int st;

    JC_CHECK_STR(jc_todo_status_wire(JC_TODO_DONE), "completed");
    JC_CHECK_STR(jc_todo_status_wire(JC_TODO_IN_PROGRESS), "in_progress");
    JC_CHECK_STR(jc_todo_status_wire(JC_TODO_PENDING), "pending");
    JC_CHECK_STR(jc_todo_status_wire(42), "pending");
    /* every wire word parses back to the status that printed it */
    for (st = JC_TODO_PENDING; st <= JC_TODO_DONE; st++) {
        JC_CHECK(jc_todo_status_from_wire(jc_todo_status_wire(st)) == st);
    }
    JC_CHECK(jc_todo_status_from_wire("done") == JC_TODO_DONE);
    JC_CHECK(jc_todo_status_from_wire("complete") == JC_TODO_DONE);
    JC_CHECK(jc_todo_status_from_wire("in-progress") == JC_TODO_IN_PROGRESS);
    JC_CHECK(jc_todo_status_from_wire("doing") == JC_TODO_IN_PROGRESS);
    JC_CHECK(jc_todo_status_from_wire("whatever") == JC_TODO_PENDING);
    JC_CHECK(jc_todo_status_from_wire("") == JC_TODO_PENDING);
    JC_CHECK(jc_todo_status_from_wire(NULL) == JC_TODO_PENDING);

    jc_todo_init(&a);
    jc_todo_init(&b);
    JC_CHECK(a.gen == 0);
    it.content = jc_strdup("x");
    it.status = JC_TODO_DONE;
    jc_vec_push(&a.items, &it);
    JC_CHECK(jc_todo_copy(&b, &a) == JC_OK);
    JC_CHECK(b.items.len == 1);
    JC_CHECK(b.gen == 1); /* a copied-into list reads as changed */
    if (b.items.len == 1 && a.items.len == 1) {
        struct jc_todo_item *ia = (struct jc_todo_item *)jc_vec_at(&a.items, 0);
        struct jc_todo_item *ib = (struct jc_todo_item *)jc_vec_at(&b.items, 0);
        JC_CHECK_STR(ib->content, "x");
        JC_CHECK(ib->status == JC_TODO_DONE);
        JC_CHECK(ib->content != ia->content); /* deep, not shared */
    }
    jc_todo_clear(&a);
    JC_CHECK(a.items.len == 0);
    JC_CHECK(a.gen == 1);        /* a clear is a replacement */
    JC_CHECK(b.items.len == 1);  /* ...of a's list only */
    jc_todo_free(&a);
    jc_todo_free(&b);
}

/* M299: state as a word, not a box. Reported from use -- a model writes `- [ ]`
 * lists and then does not go back to flip them to `- [x]`, so the list quietly
 * stops being true. Mutating a two-character cell in place is a fiddly edit;
 * writing a sentence with its state in front of it is not. */
static void test_status_word(void)
{
    JC_CHECK_STR(jc_todo_status_word(JC_TODO_PENDING), "pending");
    JC_CHECK_STR(jc_todo_status_word(JC_TODO_IN_PROGRESS), "in-progress");
    JC_CHECK_STR(jc_todo_status_word(JC_TODO_DONE), "complete");
    /* Never NULL, even for a value outside the enum -- this feeds a "%s". */
    JC_CHECK(jc_todo_status_word(99) != NULL);
    JC_CHECK(jc_todo_status_word(-1) != NULL);
}

static void test_strip_marker(void)
{
    int st, found;
    const char *r;

    /* No marker: the text is returned untouched and `found` says so, so a caller
     * can prefer the status the model passed explicitly. */
    st = JC_TODO_DONE; found = 1;
    r = jc_todo_strip_marker("write the design note", &st, &found);
    JC_CHECK_STR(r, "write the design note");
    JC_CHECK(found == 0);
    JC_CHECK(st == JC_TODO_DONE);          /* untouched */

    /* Checkboxes, the shape actually reported. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("- [ ] measure first", &st, &found);
    JC_CHECK_STR(r, "measure first");
    JC_CHECK(found == 1 && st == JC_TODO_PENDING);

    st = -1; found = 0;
    r = jc_todo_strip_marker("- [x] measured", &st, &found);
    JC_CHECK_STR(r, "measured");
    JC_CHECK(found == 1 && st == JC_TODO_DONE);

    st = -1; found = 0;
    r = jc_todo_strip_marker("- [X] measured", &st, &found);
    JC_CHECK(found == 1 && st == JC_TODO_DONE);

    /* `[~]` is jichi's OWN pre-M299 rendering, so a list round-tripped through an
     * older transcript still reads correctly rather than becoming pending. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("[~] extracting the helper", &st, &found);
    JC_CHECK_STR(r, "extracting the helper");
    JC_CHECK(found == 1 && st == JC_TODO_IN_PROGRESS);

    /* Bullets are optional, and other bullet characters work. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("* [ ] a", &st, &found);
    JC_CHECK_STR(r, "a");
    JC_CHECK(found == 1 && st == JC_TODO_PENDING);

    /* State words with a colon -- what the rendering now produces, so a list read
     * back and re-sent round-trips instead of accumulating prefixes. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("complete: shipped it", &st, &found);
    JC_CHECK_STR(r, "shipped it");
    JC_CHECK(found == 1 && st == JC_TODO_DONE);

    st = -1; found = 0;
    r = jc_todo_strip_marker("in-progress: halfway", &st, &found);
    JC_CHECK_STR(r, "halfway");
    JC_CHECK(found == 1 && st == JC_TODO_IN_PROGRESS);

    st = -1; found = 0;
    r = jc_todo_strip_marker("pending: not yet", &st, &found);
    JC_CHECK(found == 1 && st == JC_TODO_PENDING);

    /* Longest-first matching: "completed" must not be cut to "complete" leaving a
     * stray "d", and "in_progress"/"in progress" are the same state. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("completed: done thing", &st, &found);
    JC_CHECK_STR(r, "done thing");
    JC_CHECK(found == 1 && st == JC_TODO_DONE);

    st = -1; found = 0;
    r = jc_todo_strip_marker("in_progress: x", &st, &found);
    JC_CHECK(found == 1 && st == JC_TODO_IN_PROGRESS);
    st = -1; found = 0;
    r = jc_todo_strip_marker("in progress: x", &st, &found);
    JC_CHECK(found == 1 && st == JC_TODO_IN_PROGRESS);

    /* `incomplete` maps to PENDING on purpose: it says the work is not done, and
     * reading it as in-progress would claim work had STARTED -- a stronger
     * statement than the word makes. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("incomplete: untouched", &st, &found);
    JC_CHECK_STR(r, "untouched");
    JC_CHECK(found == 1 && st == JC_TODO_PENDING);

    /* Case-insensitive, since a model capitalises a sentence by habit. */
    st = -1; found = 0;
    r = jc_todo_strip_marker("Complete: it works", &st, &found);
    JC_CHECK_STR(r, "it works");
    JC_CHECK(found == 1 && st == JC_TODO_DONE);

    /* A colon in ordinary prose is NOT a state marker. */
    st = JC_TODO_DONE; found = 1;
    r = jc_todo_strip_marker("note: check the fence", &st, &found);
    JC_CHECK_STR(r, "note: check the fence");
    JC_CHECK(found == 0);

    /* A word that merely STARTS like a state word is not one either. */
    st = JC_TODO_DONE; found = 1;
    r = jc_todo_strip_marker("completeness of the audit", &st, &found);
    JC_CHECK_STR(r, "completeness of the audit");
    JC_CHECK(found == 0);

    /* Defensive: NULL content yields "" and finds nothing, never a crash -- the
     * result feeds jc_strdup. */
    found = 1;
    r = jc_todo_strip_marker(NULL, &st, &found);
    JC_CHECK_STR(r, "");
    JC_CHECK(found == 0);
    /* NULL out-params are a no-op, not a fault. */
    JC_CHECK(jc_todo_strip_marker("- [x] a", NULL, NULL) != NULL);
}

void test_todo(void)
{
    /* C89: declarations before statements. This file compiled for months only
     * because its .o was never rebuilt under WERROR=1 -- a header change in M332
     * forced the recompile that surfaced it. */
    struct jc_arena *a;
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    struct jc_todo_list list; /* M606: the SESSION owns the list; app points */

    test_status_word();
    test_strip_marker();
    test_wire_and_copy();
    a = jc_arena_new(0);

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_todo_init(&list);
    app.todos = &list;
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);
    app.tools = &reg;

    /* Write three items with distinct statuses. */
    jc_tool_execute(&reg, "todowrite",
        "{\"todos\":[{\"content\":\"alpha\",\"status\":\"pending\"},"
        "{\"content\":\"beta\",\"status\":\"in_progress\"},"
        "{\"content\":\"gamma\",\"status\":\"completed\"}]}", &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(list.items.len == 3);
    /* M299: the rendering is words, not boxes. These three assertions used to
     * pin "[ ] alpha" / "[~] beta" / "[x] gamma"; they are UPDATED to the new
     * contract rather than deleted, because the rendering is what the model reads
     * back and is therefore load-bearing (the M289 precedent). */
    JC_CHECK(strstr(res.content, "pending") != NULL);
    JC_CHECK(strstr(res.content, "alpha") != NULL);
    JC_CHECK(strstr(res.content, "in-progress") != NULL);
    JC_CHECK(strstr(res.content, "beta") != NULL);
    JC_CHECK(strstr(res.content, "complete") != NULL);
    JC_CHECK(strstr(res.content, "gamma") != NULL);
    /* And no checkbox survives anywhere in the output. */
    JC_CHECK(strstr(res.content, "[ ]") == NULL);
    JC_CHECK(strstr(res.content, "[x]") == NULL);
    JC_CHECK(strstr(res.content, "[~]") == NULL);
    jc_tool_result_free(&res);

    /* todoread returns the same list. */
    jc_tool_execute(&reg, "todoread", "{}", &res, &app);
    JC_CHECK(strstr(res.content, "in-progress") != NULL);
    JC_CHECK(strstr(res.content, "beta") != NULL);
    jc_tool_result_free(&res);

    /* M299: a checkbox written INTO the content is normalised, not preserved --
     * the state moves to the status column and the text keeps only the task. This
     * is the reported failure mode: the model writes a box, never flips it, and
     * the list stops being true. Here the box says done while `status` says
     * pending, and the in-content marker wins because it is what the model just
     * wrote in prose. */
    jc_tool_execute(&reg, "todowrite",
        "{\"todos\":[{\"content\":\"- [x] normalised\",\"status\":\"pending\"}]}",
        &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(strstr(res.content, "complete") != NULL);
    JC_CHECK(strstr(res.content, "normalised") != NULL);
    JC_CHECK(strstr(res.content, "[x]") == NULL);
    jc_tool_result_free(&res);

    /* A second write REPLACES the list (not appends). */
    jc_tool_execute(&reg, "todowrite",
        "{\"todos\":[{\"content\":\"only\",\"status\":\"pending\"}]}",
        &res, &app);
    JC_CHECK(list.items.len == 1);
    JC_CHECK(strstr(res.content, "only") != NULL &&
             strstr(res.content, "alpha") == NULL);
    jc_tool_result_free(&res);

    /* A stringified `todos` array (some models serialize the nested-JSON arg as
     * a string) is tolerated: parsed and applied, not rejected. */
    {
        cJSON *a2 = cJSON_CreateObject();
        char *js;
        cJSON_AddStringToObject(a2, "todos",
            "[{\"content\":\"strfd\",\"status\":\"in_progress\"}]");
        js = cJSON_PrintUnformatted(a2);
        jc_tool_execute(&reg, "todowrite", js != NULL ? js : "{}", &res, &app);
        free(js);
        cJSON_Delete(a2);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(list.items.len == 1);
        JC_CHECK(strstr(res.content, "strfd") != NULL);
        jc_tool_result_free(&res);
    }

    /* Subagents are blocked from the todo tools. */
    app.agent_depth = 1;
    jc_tool_execute(&reg, "todowrite",
        "{\"todos\":[{\"content\":\"x\",\"status\":\"pending\"}]}", &res, &app);
    JC_CHECK(res.is_error == 1);
    jc_tool_result_free(&res);
    app.agent_depth = 0;

    jc_todo_free(&list);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}
