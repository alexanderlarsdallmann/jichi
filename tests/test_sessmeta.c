/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sessmeta.c - the tree-free listing scanner (M202).
 *
 * The scanner replaces cJSON_Parse in jc_session_list, so its failure mode
 * matters as much as its success: when it cannot be sure, it must return 0 so the
 * caller falls back to a real parse rather than showing the user a half-read
 * listing.
 */

#include "jc_test.h"
#include "jc_sessmeta.h"
#include <string.h>
#include <stdio.h>

static int scan(const char *s, struct jc_sessmeta *m)
{
    return jc_sessmeta_scan(s, (jc_size)strlen(s), m);
}

void test_sessmeta(void)
{
    struct jc_sessmeta m;

    /* --- the exact shape jc_session_save writes ---------------------------- */
    JC_CHECK(scan(
        "{\"sessionId\":\"abc-123\",\"title\":\"list the files\","
        "\"alias\":\"wip\",\"workspaceDirectory\":\"/tmp/ws\",\"mode\":\"chat\","
        "\"history\":[{\"role\":\"user\",\"content\":\"q\"},"
        "{\"role\":\"assistant\",\"content\":\"a\"}]}", &m) == 1);
    JC_CHECK_STR(m.id, "abc-123");
    JC_CHECK_STR(m.title, "list the files");
    JC_CHECK_STR(m.alias, "wip");
    JC_CHECK_STR(m.workspace, "/tmp/ws");
    JC_CHECK(m.nmsgs == 2);
    JC_CHECK(m.has_id && m.has_title && m.has_alias && m.has_workspace);

    /* Optional fields absent (a fresh session has no title or alias). */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"workspaceDirectory\":\".\","
                  "\"mode\":\"chat\",\"history\":[]}", &m) == 1);
    JC_CHECK(m.has_id && !m.has_title && !m.has_alias && m.has_workspace);
    JC_CHECK(m.nmsgs == 0);
    JC_CHECK_STR(m.title, "");

    /* --- THE case the count exists for ------------------------------------ */
    /* A message whose CONTENT contains the literal `"role":` must not inflate
     * the count. A substring count would say 3 here; the scanner says 2, because
     * it knows it is inside a string. */
    JC_CHECK(scan(
        "{\"sessionId\":\"x\",\"workspaceDirectory\":\".\",\"history\":["
        "{\"role\":\"user\",\"content\":\"see {\\\"role\\\":\\\"user\\\"} here\"},"
        "{\"role\":\"assistant\",\"content\":\"ok\"}]}", &m) == 1);
    JC_CHECK(m.nmsgs == 2);

    /* A brace inside content must not shift depth either. */
    JC_CHECK(scan(
        "{\"sessionId\":\"x\",\"workspaceDirectory\":\".\",\"history\":["
        "{\"role\":\"user\",\"content\":\"{{{ [[[ }}}\"}]}", &m) == 1);
    JC_CHECK(m.nmsgs == 1);

    /* Nested structure inside a message (toolCalls) is not a message. */
    JC_CHECK(scan(
        "{\"sessionId\":\"x\",\"workspaceDirectory\":\".\",\"history\":["
        "{\"role\":\"assistant\",\"toolCalls\":[{\"id\":\"c1\",\"name\":\"ls\","
        "\"arguments\":\"{}\"}]},"
        "{\"role\":\"tool\",\"toolCallId\":\"c1\",\"content\":\"out\"}]}",
        &m) == 1);
    JC_CHECK(m.nmsgs == 2);

    /* --- escapes in captured values --------------------------------------- */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"title\":\"a\\\"b\\\\c\\nd\\te\","
                  "\"workspaceDirectory\":\".\",\"history\":[]}", &m) == 1);
    JC_CHECK_STR(m.title, "a\"b\\c\nd\te");

    /* \uXXXX below 0x80 becomes that byte; above becomes UTF-8. */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"title\":\"A\\u0042C\","
                  "\"workspaceDirectory\":\".\",\"history\":[]}", &m) == 1);
    JC_CHECK_STR(m.title, "ABC");
    JC_CHECK(scan("{\"sessionId\":\"x\",\"title\":\"caf\\u00e9\","
                  "\"workspaceDirectory\":\".\",\"history\":[]}", &m) == 1);
    JC_CHECK_STR(m.title, "caf\303\251");

    /* --- pretty-printed input (a hand-edited or pre-M140 file) ------------- */
    JC_CHECK(scan(
        "{\n  \"sessionId\": \"p1\",\n  \"title\": \"pretty\",\n"
        "  \"workspaceDirectory\": \"/w\",\n  \"history\": [\n"
        "    { \"role\": \"user\", \"content\": \"hi\" }\n  ]\n}\n", &m) == 1);
    JC_CHECK_STR(m.id, "p1");
    JC_CHECK_STR(m.title, "pretty");
    JC_CHECK(m.nmsgs == 1);

    /* Numbers and booleans among the keys must not derail it. */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"n\":42,\"b\":true,\"z\":null,"
                  "\"workspaceDirectory\":\".\",\"history\":[]}", &m) == 1);
    JC_CHECK_STR(m.id, "x");

    /* --- the failure contract: when unsure, say so ------------------------- */
    /* Truncated mid-object. */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"history\":[{\"role\":\"user\"", &m) == 0);
    /* Unterminated string. */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"title\":\"oops", &m) == 0);
    /* Not an object. */
    JC_CHECK(scan("[1,2,3]", &m) == 0);
    JC_CHECK(scan("\"just a string\"", &m) == 0);
    /* No history at all: not one of ours, so fall back rather than guess. */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"workspaceDirectory\":\".\"}", &m) == 0);
    /* Empty and degenerate inputs. */
    JC_CHECK(scan("", &m) == 0);
    JC_CHECK(scan("{}", &m) == 0);
    JC_CHECK(jc_sessmeta_scan(NULL, 0, &m) == 0);
    JC_CHECK(jc_sessmeta_scan("{}", 2, NULL) == 0);
    /* An illegal escape is a fallback, not a mangled value. */
    JC_CHECK(scan("{\"sessionId\":\"x\",\"title\":\"bad\\q\","
                  "\"workspaceDirectory\":\".\",\"history\":[]}", &m) == 0);

    /* --- truncation of an over-long value is NOT a failure ---------------- */
    {
        char buf[4096];
        char big[2000];
        memset(big, 'w', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        /* A workspace longer than JC_SESSMETA_WS: truncated, still trustworthy,
         * because these values are display/compare-only. */
        JC_CHECK((int)strlen(big) > JC_SESSMETA_WS);
        sprintf(buf, "{\"sessionId\":\"x\",\"workspaceDirectory\":\"%s\","
                     "\"history\":[]}", big);
        JC_CHECK(scan(buf, &m) == 1);
        JC_CHECK(m.has_workspace);
        JC_CHECK(strlen(m.workspace) == (size_t)(JC_SESSMETA_WS - 1));
    }

    /* --- len is honoured independently of NUL ----------------------------- */
    {
        const char *s = "{\"sessionId\":\"x\",\"workspaceDirectory\":\".\","
                        "\"history\":[]}GARBAGE";
        JC_CHECK(jc_sessmeta_scan(s, (jc_size)(strlen(s) - 7), &m) == 1);
        JC_CHECK_STR(m.id, "x");
        /* Cut short, it must fail rather than report a partial reading. */
        JC_CHECK(jc_sessmeta_scan(s, 20, &m) == 0);
    }
}
