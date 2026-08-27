/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_message.c - exercises the chat message model. */

#include "jc_test.h"
#include "jc_message.h"
#include "jc_str.h"
#include <string.h>

void test_message(void)
{
    struct jc_history h;
    struct jc_message *u;
    struct jc_message *a;
    struct jc_message *t;

    jc_history_init(&h);

    u = jc_history_add(&h, JC_ROLE_USER, "hello");
    if (JC_REQUIRE(u != NULL)) {
        JC_CHECK(u->role == JC_ROLE_USER);
        JC_CHECK_STR(u->content, "hello");
    }

    a = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(a, "call_1", "read_file", "{\"path\":\"x\"}");
    jc_msg_add_tool_call(a, "call_2", "list_files", "{}");
    JC_CHECK(jc_msg_tool_call_count(a) == 2);
    JC_CHECK_STR(jc_msg_tool_call_at(a, 0)->name, "read_file");
    JC_CHECK_STR(jc_msg_tool_call_at(a, 1)->id, "call_2");

    t = jc_history_add_tool_result(&h, "call_1", "file contents", 0);
    JC_CHECK(t->role == JC_ROLE_TOOL);
    JC_CHECK_STR(t->tool_call_id, "call_1");
    JC_CHECK(t->is_error == 0);

    JC_CHECK(jc_history_len(&h) == 3);
    JC_CHECK_STR(jc_role_str(JC_ROLE_ASSISTANT), "assistant");

    /* set_content replaces. */
    jc_msg_set_content(a, "done");
    JC_CHECK_STR(a->content, "done");

    /* truncate drops the tail (used by stall escalation to discard an
     * incomplete turn). len >= length is a no-op; otherwise it shortens. */
    jc_history_truncate(&h, 5);          /* >= len: no-op */
    JC_CHECK(jc_history_len(&h) == 3);
    jc_history_truncate(&h, 2);          /* drop the trailing tool result */
    JC_CHECK(jc_history_len(&h) == 2);
    JC_CHECK(jc_history_get(&h, 0)->role == JC_ROLE_USER);
    JC_CHECK(jc_history_get(&h, 1)->role == JC_ROLE_ASSISTANT);
    jc_history_truncate(&h, 0);          /* drop everything */
    JC_CHECK(jc_history_len(&h) == 0);

    jc_history_free(&h);
}

/* M364: the wire-shape validator -- the contract both serializers rely on,
 * previously folklore across ~69 mutation sites. One VALID complex history
 * (calls, results, an injected user note, an empty-answer assistant, a
 * trailing placeholder) must pass with zero; then every corruption class is
 * planted one at a time and must be named. The deliberate non-checks
 * (interior placeholders, consecutive users) are asserted as NON-violations,
 * because a validator that flags legal history teaches people to ignore it. */
void test_history_check(void)
{
    struct jc_history h;
    struct jc_message *a;
    struct jc_sb sb;

    /* --- a rich VALID history: 0 violations -------------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "looking");
    jc_msg_add_tool_call(a, "c1", "read_file", "{\"path\":\"a\"}");
    jc_msg_add_tool_call(a, "c2", "search_code", "{\"query\":\"x\"}");
    jc_history_add_tool_result(&h, "c2", "hits", 0);   /* order may differ */
    jc_history_add_tool_result(&h, "c1", "bytes", 1);  /* an error result  */
    jc_history_add(&h, JC_ROLE_USER, "[context] note");/* injected note    */
    jc_history_add(&h, JC_ROLE_ASSISTANT, "");         /* empty answer     */
    jc_history_add(&h, JC_ROLE_USER, "go on");
    jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);       /* placeholder      */
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 0);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- unanswered call --------------------------------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "c1", "read_file", "{}");
    jc_msg_add_tool_call(a, "c2", "read_file", "{}");
    jc_history_add_tool_result(&h, "c1", "ok", 0);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "next turn");
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 1);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "unanswered tool call") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "'c2'") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- double-answered call ---------------------------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "c1", "read_file", "{}");
    jc_history_add_tool_result(&h, "c1", "ok", 0);
    jc_history_add_tool_result(&h, "c1", "ok again", 0);
    jc_sb_init(&sb);
    /* the duplicate is BOTH a double answer and, from the second result's
     * side, a legal claim -- the double-answer line is the finding */
    JC_CHECK(jc_history_check(&h, &sb) >= 1);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "answered more than once") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- orphan result: no assistant round at all --------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    jc_history_add_tool_result(&h, "ghost", "ok", 0);
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 1);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "orphan tool result") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- result claiming an id its round never declared ---------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "c1", "read_file", "{}");
    jc_history_add_tool_result(&h, "c1", "ok", 0);
    jc_history_add_tool_result(&h, "cX", "stray", 0);
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 1);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "answers no call in its round") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- a result with no id ------------------------------------------------ */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
    jc_msg_add_tool_call(a, "c1", "read_file", "{}");
    jc_history_add_tool_result(&h, "c1", "ok", 0);
    jc_history_add_tool_result(&h, NULL, "lost", 0);
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 1);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "no tool_call_id") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- first message not a user turn -------------------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_ASSISTANT, "hello");
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 1);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "not a user turn") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- empty user message -------------------------------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "");
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 1);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "empty user message") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* --- the deliberate NON-checks stay legal -------------------------------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);   /* interior placeholder */
    jc_history_add(&h, JC_ROLE_USER, "again");     /* consecutive users OK */
    jc_history_add(&h, JC_ROLE_USER, "note");
    JC_CHECK(jc_history_check(&h, NULL) == 0);     /* NULL out: count only */
    jc_history_free(&h);

    /* --- bounded samples: many violations, 8 lines + the remainder ---------- */
    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "task");
    {
        int i;
        for (i = 0; i < 12; i++) {
            jc_history_add(&h, JC_ROLE_USER, "");
        }
    }
    jc_sb_init(&sb);
    JC_CHECK(jc_history_check(&h, &sb) == 12);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "(+4 more)") != NULL);
    jc_sb_free(&sb);
    jc_history_free(&h);

    /* NULL history: zero, no crash. */
    JC_CHECK(jc_history_check(NULL, NULL) == 0);
}
