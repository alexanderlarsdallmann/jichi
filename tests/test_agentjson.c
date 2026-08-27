/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_agentjson.c - structured headless output builders (M63). */

#include "jc_test.h"
#include <string.h>
#include "jc_utf8.h"
#include "jc_agentjson.h"
#include "cJSON.h"

#include <stdlib.h>


/* M439: the jsonl preview cuts on a UTF-8 boundary.
 *
 * `tool_result.preview` was built with jc_snprintf into a 513-byte buffer. That is
 * not UTF-8 aware, so any cut landing inside a multi-byte character put INVALID
 * UTF-8 on a JSON line -- on a surface docs/EMBEDDING.md calls stable and tells
 * consumers to parse. A strict reader may reject the whole line, so a truncated
 * preview became a LOST EVENT.
 *
 * The cases below are chosen for where the cut lands relative to a sequence, since
 * that is the only thing that varies: cleanly between characters, and one/two bytes
 * into a three-byte one. */
static void test_agentjson_preview(void)
{
    char out[16];
    char big[600];
    jc_size i;

    /* --- degenerate inputs a caller can actually produce -------------------- */
    out[0] = 'x';
    jc_agentjson_preview(NULL, out, sizeof out);
    JC_CHECK(out[0] == '\0');
    jc_agentjson_preview("", out, sizeof out);
    JC_CHECK(out[0] == '\0');
    out[0] = 'x';
    jc_agentjson_preview("abc", out, 0);       /* cap 0 writes nothing at all */
    JC_CHECK(out[0] == 'x');
    jc_agentjson_preview("abc", NULL, sizeof out);   /* must not crash */

    /* --- shorter than the cap: copied verbatim ----------------------------- */
    jc_agentjson_preview("hello", out, sizeof out);
    JC_CHECK(strcmp(out, "hello") == 0);

    /* --- ASCII longer than the cap: cut at cap-1 --------------------------- */
    jc_agentjson_preview("0123456789abcdefghij", out, sizeof out);
    JC_CHECK(strlen(out) == sizeof out - 1);
    JC_CHECK(strcmp(out, "0123456789abcde") == 0);

    /* --- the bug: a 3-byte character straddling the cut --------------------
     * "…" is E2 80 A6. With cap 16 the copy may take 15 bytes. Five ellipses are
     * 15 bytes exactly, so a sixth must be dropped WHOLE rather than clipped. */
    jc_agentjson_preview("\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6"
                         "\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6",
                         out, sizeof out);
    JC_CHECK(strlen(out) == 15);          /* five whole characters */
    JC_CHECK(jc_utf8_valid(out, strlen(out)));

    /* One byte earlier: 14 bytes available, so the fifth character does not fit
     * and the result must be four whole ones (12 bytes) -- NOT 14 with a split
     * tail, which is what a byte cut produced. */
    jc_agentjson_preview("\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6"
                         "\xe2\x80\xa6\xe2\x80\xa6", out, 15);
    JC_CHECK(strlen(out) == 12);
    JC_CHECK(jc_utf8_valid(out, strlen(out)));

    /* Two bytes earlier, so the cut lands one byte into the fifth character. */
    jc_agentjson_preview("\xe2\x80\xa6\xe2\x80\xa6\xe2\x80\xa6"
                         "\xe2\x80\xa6\xe2\x80\xa6", out, 14);
    JC_CHECK(strlen(out) == 12);
    JC_CHECK(jc_utf8_valid(out, strlen(out)));

    /* --- the real shape: 512 bytes of 3-byte characters -------------------
     * The production cap. 513/3 is not an integer, so the naive cut is guaranteed
     * to split a character -- which is why this was not a rare edge case but the
     * NORMAL outcome for any non-ASCII tool result long enough to truncate. */
    for (i = 0; i + 3 <= sizeof big - 1; i += 3) {
        big[i] = '\xe2'; big[i + 1] = '\x80'; big[i + 2] = '\xa6';
    }
    big[i] = '\0';
    {
        char prev[513];
        jc_agentjson_preview(big, prev, sizeof prev);
        JC_CHECK(strlen(prev) == 510);            /* 170 whole characters */
        JC_CHECK(jc_utf8_valid(prev, strlen(prev)));
    }
}

void test_agentjson(void)
{
    cJSON *o;

    /* Event: stamped with v + type. */
    o = jc_agentjson_event("tool_call");
    JC_CHECK(o != NULL);
    JC_CHECK(cJSON_GetObjectItem(o, "v")->valuedouble == 1.0);
    JC_CHECK_STR(cJSON_GetObjectItem(o, "type")->valuestring, "tool_call");
    cJSON_Delete(o);

    /* Result: full shape, no error, session id present, work kept. */
    o = jc_agentjson_result("hi", "m", "sid", NULL, 10.0, 5.0, 0.01, 2, 0,
                            "done", 1, 0, NULL, NULL, NULL);
    JC_CHECK_STR(cJSON_GetObjectItem(o, "type")->valuestring, "done");
    JC_CHECK_STR(cJSON_GetObjectItem(o, "text")->valuestring, "hi");
    JC_CHECK_STR(cJSON_GetObjectItem(o, "session_id")->valuestring, "sid");
    JC_CHECK(cJSON_GetObjectItem(cJSON_GetObjectItem(o, "tokens"),
                                 "input")->valuedouble == 10.0);
    JC_CHECK(cJSON_GetObjectItem(o, "cost")->valuedouble > 0.0);
    JC_CHECK(cJSON_GetObjectItem(o, "tool_calls")->valuedouble == 2.0);
    JC_CHECK_STR(cJSON_GetObjectItem(o, "stop_reason")->valuestring, "done");
    JC_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(o, "work_kept")));
    JC_CHECK(cJSON_GetObjectItem(o, "error") == NULL);
    JC_CHECK(!cJSON_IsTrue(cJSON_GetObjectItem(o, "aborted")));
    cJSON_Delete(o);

    /* M92-S1: a budget stop that rolled back reports work_kept:false. */
    o = jc_agentjson_result("", "m", "s", NULL, 0.0, 0.0, 0.0, 0, 0,
                            "budget", 0, 0, NULL, NULL, NULL);
    JC_CHECK(!cJSON_IsTrue(cJSON_GetObjectItem(o, "work_kept")));
    JC_CHECK_STR(cJSON_GetObjectItem(o, "stop_reason")->valuestring, "budget");
    cJSON_Delete(o);

    /* Result: error object present, session id omitted when NULL/empty. */
    o = jc_agentjson_result("", "m", NULL, NULL, 0.0, 0.0, 0.0, 0, 0,
                            "error", 0, 5, "error", "boom", NULL);
    JC_CHECK(cJSON_GetObjectItem(o, "session_id") == NULL);
    {
        cJSON *e = cJSON_GetObjectItem(o, "error");
        JC_CHECK(e != NULL);
        JC_CHECK(cJSON_GetObjectItem(e, "code")->valuedouble == 5.0);
        JC_CHECK_STR(cJSON_GetObjectItem(e, "type")->valuestring, "error");
        JC_CHECK_STR(cJSON_GetObjectItem(e, "message")->valuestring, "boom");
    }
    cJSON_Delete(o);

    /* aborted flag round-trips. */
    o = jc_agentjson_result("x", "m", "s", NULL, 0.0, 0.0, 0.0, 0, 1,
                            "interrupted", 1, 0, NULL, NULL, NULL);
    JC_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(o, "aborted")));
    JC_CHECK_STR(cJSON_GetObjectItem(o, "stop_reason")->valuestring,
                 "interrupted");
    cJSON_Delete(o);

    /* M97: tool-category classifier. */
    JC_CHECK(jc_agent_tool_category("read_file") == JC_TOOLCAT_READ);
    JC_CHECK(jc_agent_tool_category("search_code") == JC_TOOLCAT_READ);
    JC_CHECK(jc_agent_tool_category("find_references") == JC_TOOLCAT_READ);
    JC_CHECK(jc_agent_tool_category("edit_file") == JC_TOOLCAT_WRITE);
    JC_CHECK(jc_agent_tool_category("apply_patch") == JC_TOOLCAT_WRITE);
    JC_CHECK(jc_agent_tool_category("run_terminal_command") == JC_TOOLCAT_SHELL);
    JC_CHECK(jc_agent_tool_category("run_tests") == JC_TOOLCAT_SHELL);
    JC_CHECK(jc_agent_tool_category("remember") == JC_TOOLCAT_OTHER);
    JC_CHECK(jc_agent_tool_category(NULL) == JC_TOOLCAT_OTHER);

    /* M97: run economics surfaced when econ is non-NULL. */
    {
        struct jc_agent_econ econ;
        cJSON *tools;
        /* memset FIRST, then assign. Field-by-field initialisation of a struct
         * that later grows is how this test came to read uninitialised stack
         * memory: M443 added deg_unanswered/deg_approval/deg_privilege to
         * jc_agent_econ and nothing here was updated, so jc_agentjson_result
         * decided whether to emit `degraded` at all from whatever was on the
         * stack -- 6 reads, which is exactly what valgrind reported. The suite
         * passed the whole time, because no check asserted either outcome.
         * Found while running the unit suite under valgrind for the Termux row
         * (M459); the C89 rules forbid designated initializers, so memset is
         * the way to make adding a field safe by default. */
        memset(&econ, 0, sizeof econ);
        econ.starved = 1;
        econ.budget_kind = "tokens";
        econ.budget_used = 1500000.0;
        econ.budget_limit = 1500000.0;
        econ.peak_input = 209729.0;
        econ.cache_read = 0.0;
        econ.cache_write = 0.0;
        econ.reads = 40;
        econ.writes = 0;
        econ.shells = 2;
        econ.other_tools = 1;
        o = jc_agentjson_result("", "m", "s", NULL, 100.0, 5.0, 0.0, 43, 0,
                                "budget", 1, 0, NULL, NULL, &econ);
        JC_CHECK(cJSON_IsTrue(cJSON_GetObjectItem(o, "starved")));
        JC_CHECK_STR(cJSON_GetObjectItem(o, "budget_kind")->valuestring, "tokens");
        JC_CHECK(cJSON_GetObjectItem(o, "peak_input")->valuedouble == 209729.0);
        JC_CHECK(cJSON_GetObjectItem(cJSON_GetObjectItem(o, "budget"),
                                     "limit")->valuedouble == 1500000.0);
        /* M443's contract: presence IS the flag, so with no degraded
         * decisions the key must be ABSENT -- the half that uninitialised
         * memory was silently deciding. */
        JC_CHECK(cJSON_GetObjectItem(o, "degraded") == NULL);
        tools = cJSON_GetObjectItem(o, "tools");
        JC_CHECK(cJSON_GetObjectItem(tools, "read")->valuedouble == 40.0);
        JC_CHECK(cJSON_GetObjectItem(tools, "shell")->valuedouble == 2.0);
        cJSON_Delete(o);
    }

    /* M443's counters, the other half of the same contract. Never covered
     * until M459: the only test that reached this code left the three fields
     * uninitialised, so `degraded` appeared or not by accident and neither
     * outcome was asserted. Each count is emitted ONLY when non-zero, so a
     * zero field must leave its key out while its siblings stay. */
    {
        struct jc_agent_econ econ;
        cJSON *d;
        memset(&econ, 0, sizeof econ);
        econ.deg_unanswered = 3;
        econ.deg_privilege = 1;   /* deg_approval deliberately left at 0 */
        o = jc_agentjson_result("", "m", "s", NULL, 0.0, 0.0, 0.0, 0, 0,
                                "done", 0, 0, NULL, NULL, &econ);
        d = cJSON_GetObjectItem(o, "degraded");
        JC_CHECK(d != NULL);
        if (d != NULL) {
            JC_CHECK(cJSON_GetObjectItem(d, "unanswered")->valuedouble == 3.0);
            JC_CHECK(cJSON_GetObjectItem(d, "privilege_refused")->valuedouble
                     == 1.0);
            JC_CHECK(cJSON_GetObjectItem(d, "approval_unavailable") == NULL);
        }
        cJSON_Delete(o);
    }
    test_agentjson_preview();
}
