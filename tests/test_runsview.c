/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_runsview.c - the pure envelope-journal summarizer (M158). */

#include "jc_test.h"
#include "jc_runsview.h"

#include <string.h>

static void test_full_run(void)
{
    /* A complete bounded run: start, a red then green verify, end ok. */
    static const char *J =
        "{\"ts\":100,\"run\":\"r-1\",\"event\":\"start\","
        "\"budget_tokens\":400000,\"verify\":\"make test\"}\n"
        "{\"ts\":110,\"run\":\"r-1\",\"event\":\"verify\",\"exit\":2,"
        "\"failed\":3}\n"
        "{\"ts\":140,\"run\":\"r-1\",\"event\":\"verify\",\"exit\":0}\n"
        "{\"ts\":150,\"run\":\"r-1\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"rolled_back\":false,\"tokens_used\":52300,\"tool_calls\":34}\n";
    struct jc_run_summary s;
    struct jc_sb out;

    JC_CHECK(jc_runsview_parse(J, &s) == 0);
    JC_CHECK(strcmp(s.run, "r-1") == 0);
    JC_CHECK(strcmp(s.outcome, "ok") == 0);
    JC_CHECK(s.rolled_back == 0);
    JC_CHECK(s.tokens_used == 52300.0);
    JC_CHECK(s.tool_calls == 34);
    JC_CHECK(s.verify_pass == 1 && s.verify_fail == 1);
    JC_CHECK(s.ts_first == 100.0 && s.ts_last == 150.0);
    JC_CHECK(s.events == 4);

    jc_sb_init(&out);
    jc_runsview_render_header(&out);
    jc_runsview_render_row(&s, &out);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "RUN") != NULL);
        JC_CHECK(strstr(out.data, "r-1") != NULL);
        JC_CHECK(strstr(out.data, "ok") != NULL);
        JC_CHECK(strstr(out.data, "52.3k") != NULL);
        JC_CHECK(strstr(out.data, "1/1") != NULL);
    }
    jc_sb_free(&out);
}

static void test_budget_rollback_truncated(void)
{
    /* A budget-stopped, rolled-back run with an out-of-scope flag. */
    /* Assembled at runtime: the concatenated literal would exceed the C90
     * 509-char limit. */
    struct jc_sb j;
    struct jc_run_summary s;
    struct jc_sb out;

    jc_sb_init(&j);
    jc_sb_append(&j,
        "{\"ts\":200,\"run\":\"r-2\",\"event\":\"start\"}\n"
        "{\"ts\":205,\"run\":\"r-2\",\"event\":\"control\","
        "\"cmd\":\"inject\",\"text\":\"narrow the scope\"}\n"
        "{\"ts\":206,\"run\":\"r-2\",\"event\":\"control\","
        "\"cmd\":\"pause\"}\n"
        "{\"ts\":207,\"run\":\"r-2\",\"event\":\"control\","
        "\"cmd\":\"inject\",\"text\":\"report and stop\"}\n");
    jc_sb_append(&j,
        "{\"ts\":208,\"run\":\"r-2\",\"event\":\"ask\","
        "\"question\":\"which format?\",\"answered\":true}\n"
        "{\"ts\":209,\"run\":\"r-2\",\"event\":\"ask\","
        "\"question\":\"overwrite the old export?\",\"answered\":false}\n");
    jc_sb_append(&j,
        "{\"ts\":210,\"run\":\"r-2\",\"event\":\"budget\",\"kind\":\"tokens\","
        "\"starved\":true}\n"
        "{\"ts\":211,\"run\":\"r-2\",\"event\":\"out_of_scope\",\"paths\":2}\n"
        "{\"ts\":212,\"run\":\"r-2\",\"event\":\"rollback\"}\n"
        "{\"ts\":213,\"run\":\"r-2\",\"event\":\"end\","
        "\"outcome\":\"budget_exhausted\",\"rolled_back\":true,"
        "\"tokens_used\":400001,\"tool_calls\":80}\n");

    JC_CHECK(jc_runsview_parse(j.data, &s) == 0);
    JC_CHECK(strcmp(s.outcome, "budget_exhausted") == 0);
    JC_CHECK(s.rolled_back == 1);
    JC_CHECK(strcmp(s.budget_kind, "tokens") == 0);
    JC_CHECK(s.starved == 1);
    JC_CHECK(s.out_of_scope == 2);
    JC_CHECK(s.rollbacks == 1);
    JC_CHECK(s.steered == 2); /* M161: injects only, not the pause */
    JC_CHECK(s.asks_unanswered == 1); /* M359: unanswered only -- the
                                       * answered ask must NOT count */

    jc_sb_init(&out);
    jc_runsview_render_row(&s, &out);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "rolled_back") != NULL);
        JC_CHECK(strstr(out.data, "budget=tokens") != NULL);
        JC_CHECK(strstr(out.data, "starved") != NULL);
        JC_CHECK(strstr(out.data, "out_of_scope=2") != NULL);
        JC_CHECK(strstr(out.data, "steered=2") != NULL);
        JC_CHECK(strstr(out.data, "unanswered=1") != NULL); /* M359 */
    }
    jc_sb_free(&out);
    jc_sb_free(&j);

    /* A journal with no `end` (crash / in flight) reports outcome "?". */
    {
        static const char *K =
            "{\"ts\":300,\"run\":\"r-3\",\"event\":\"start\"}\n";
        JC_CHECK(jc_runsview_parse(K, &s) == 0);
        JC_CHECK(strcmp(s.outcome, "?") == 0);
        JC_CHECK(s.rolled_back == -1);
    }

    /* Degenerate inputs. */
    JC_CHECK(jc_runsview_parse("", &s) == -1);
    JC_CHECK(jc_runsview_parse("garbage\n\n", &s) == -1);
    JC_CHECK(jc_runsview_parse(NULL, &s) == -1);
}

static void test_runs_json(void)
{
    static const char *J =
        "{\"ts\":100,\"run\":\"r-1\",\"event\":\"start\"}\n"
        "{\"ts\":110,\"run\":\"r-1\",\"event\":\"verify\",\"exit\":0}\n"
        "{\"ts\":150,\"run\":\"r-1\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"rolled_back\":false,\"tokens_used\":52300,\"tool_calls\":34}\n";
    struct jc_run_summary s;
    cJSON *o;

    JC_CHECK(jc_runsview_parse(J, &s) == 0);
    o = jc_runsview_json(&s);
    JC_CHECK(o != NULL);
    if (o != NULL) {
        const cJSON *verify = cJSON_GetObjectItem(o, "verify");
        JC_CHECK(strcmp(jc_json_get_str(o, "run", ""), "r-1") == 0);
        JC_CHECK(strcmp(jc_json_get_str(o, "outcome", ""), "ok") == 0);
        JC_CHECK(jc_json_get_bool(o, "rolled_back", 1) == 0);
        JC_CHECK(jc_json_get_num(o, "tokens_used", 0) == 52300.0);
        JC_CHECK(jc_json_get_num(o, "ts", 0) == 100.0);
        JC_CHECK(jc_json_get_num(o, "ts_end", 0) == 150.0);
        JC_CHECK(cJSON_IsObject(verify) &&
                 jc_json_get_num(verify, "pass", -1) == 1.0 &&
                 jc_json_get_num(verify, "fail", -1) == 0.0);
        /* zero/empty optionals omitted */
        JC_CHECK(cJSON_GetObjectItem(o, "budget") == NULL);
        JC_CHECK(cJSON_GetObjectItem(o, "starved") == NULL);
        JC_CHECK(cJSON_GetObjectItem(o, "out_of_scope") == NULL);
        JC_CHECK(cJSON_GetObjectItem(o, "steered") == NULL); /* M161 */
        JC_CHECK(cJSON_GetObjectItem(o, "unanswered") == NULL); /* M359 */
        cJSON_Delete(o);
    }

    /* M161: a steered run carries the count in JSON. */
    {
        static const char *S =
            "{\"ts\":400,\"run\":\"r-4\",\"event\":\"control\","
            "\"cmd\":\"inject\",\"text\":\"x\"}\n"
            "{\"ts\":401,\"run\":\"r-4\",\"event\":\"end\","
            "\"outcome\":\"ok\",\"rolled_back\":false,"
            "\"tokens_used\":10,\"tool_calls\":1}\n";
        JC_CHECK(jc_runsview_parse(S, &s) == 0);
        o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(jc_json_get_num(o, "steered", 0) == 1.0);
            cJSON_Delete(o);
        }
    }

    /* M359: an unattended ask carries the count in JSON; an ANSWERED one
     * alone does not (unanswered is the review signal, and the answered
     * question already shaped the run through the history). */
    {
        static const char *A =
            "{\"ts\":500,\"run\":\"r-5\",\"event\":\"ask\","
            "\"question\":\"q1\",\"answered\":false}\n"
            "{\"ts\":501,\"run\":\"r-5\",\"event\":\"end\","
            "\"outcome\":\"ok\",\"rolled_back\":false,"
            "\"tokens_used\":10,\"tool_calls\":1}\n";
        static const char *B =
            "{\"ts\":510,\"run\":\"r-6\",\"event\":\"ask\","
            "\"question\":\"q2\",\"answered\":true}\n"
            "{\"ts\":511,\"run\":\"r-6\",\"event\":\"end\","
            "\"outcome\":\"ok\",\"rolled_back\":false,"
            "\"tokens_used\":10,\"tool_calls\":1}\n";
        JC_CHECK(jc_runsview_parse(A, &s) == 0);
        o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(jc_json_get_num(o, "unanswered", 0) == 1.0);
            cJSON_Delete(o);
        }
        JC_CHECK(jc_runsview_parse(B, &s) == 0);
        JC_CHECK(s.asks_unanswered == 0);
        o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(cJSON_GetObjectItem(o, "unanswered") == NULL);
            cJSON_Delete(o);
        }
    }

    /* No `end` event => rolled_back is JSON null (unknown), not false. */
    {
        static const char *K =
            "{\"ts\":300,\"run\":\"r-3\",\"event\":\"start\"}\n";
        JC_CHECK(jc_runsview_parse(K, &s) == 0);
        o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(cJSON_IsNull(cJSON_GetObjectItem(o, "rolled_back")));
            JC_CHECK(strcmp(jc_json_get_str(o, "outcome", ""), "?") == 0);
            cJSON_Delete(o);
        }
    }
    JC_CHECK(jc_runsview_json(NULL) == NULL);
}

/* M290: the build that ran it, from the run-level `start` record. A run journal is
 * what a supervisor triages from, and "which jichi ran this" is unrecoverable once
 * the binary has moved on. */
static void test_run_version(void)
{
    struct jc_run_summary s;
    cJSON *j;

    JC_CHECK(jc_runsview_parse(
        "{\"ts\":100,\"run\":\"r1\",\"event\":\"start\","
        "\"jichi\":\"0.9.0\",\"verify\":\"make test\"}\n"
        "{\"ts\":200,\"run\":\"r1\",\"event\":\"end\","
        "\"outcome\":\"ok\",\"tokens_used\":50,\"tool_calls\":2}\n",
        &s) == 0);
    JC_CHECK_STR(s.jichi, "0.9.0");
    /* And it reaches the machine-readable form a supervisor consumes. */
    j = jc_runsview_json(&s);
    JC_CHECK(j != NULL);
    if (j != NULL) {
        JC_CHECK_STR(jc_json_get_str(j, "jichi", NULL), "0.9.0");
        cJSON_Delete(j);
    }

    /* A pre-M290 journal has no `jichi`: empty, and the JSON omits the key
     * entirely rather than inventing a value. */
    JC_CHECK(jc_runsview_parse(
        "{\"ts\":100,\"run\":\"r2\",\"event\":\"start\"}\n"
        "{\"ts\":200,\"run\":\"r2\",\"event\":\"end\","
        "\"outcome\":\"ok\"}\n", &s) == 0);
    JC_CHECK(s.jichi[0] == '\0');
    j = jc_runsview_json(&s);
    JC_CHECK(j != NULL);
    if (j != NULL) {
        JC_CHECK(cJSON_GetObjectItem(j, "jichi") == NULL);
        cJSON_Delete(j);
    }
}


/* M329: a `post_outcome` event means spend continued after the run's outcome was
 * decided, so the tokens and tool calls on the row are SHORT. The row has to say so
 * -- a number that is quietly wrong is worse than one that is missing. */
static void test_post_outcome_flag(void)
{
    static const char *J =
        "{\"ts\":100,\"run\":\"r-9\",\"event\":\"start\"}\n"
        "{\"ts\":150,\"run\":\"r-9\",\"event\":\"end\",\"outcome\":"
        "\"budget_exhausted\",\"tokens_used\":1000000,\"tool_calls\":27}\n"
        "{\"ts\":200,\"run\":\"r-9\",\"event\":\"post_outcome\","
        "\"outcome\":\"budget_exhausted\"}\n";
    struct jc_run_summary s;
    struct jc_sb out;

    JC_CHECK(jc_runsview_parse(J, &s) == 0);
    JC_CHECK(s.post_outcome == 1);
    /* The end event's own totals are unchanged -- the flag is what marks them short. */
    JC_CHECK(s.tokens_used == 1000000.0);
    JC_CHECK(s.tool_calls == 27);

    jc_sb_init(&out);
    jc_runsview_render_row(&s, &out);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "post_outcome") != NULL);
        /* and it says WHY it matters, not just that it happened */
        JC_CHECK(strstr(out.data, "totals_short") != NULL);
    }
    jc_sb_free(&out);

    /* The JSON projection carries it too, so a supervisor can gate on it. */
    {
        cJSON *o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(cJSON_GetObjectItem(o, "post_outcome") != NULL);
            cJSON_Delete(o);
        }
    }

    /* A clean run must NOT carry the flag -- otherwise the check above proves nothing. */
    {
        static const char *K =
            "{\"ts\":100,\"run\":\"r-8\",\"event\":\"start\"}\n"
            "{\"ts\":150,\"run\":\"r-8\",\"event\":\"end\",\"outcome\":\"ok\","
            "\"tokens_used\":500,\"tool_calls\":2}\n";
        struct jc_run_summary t;
        struct jc_sb o2;
        JC_CHECK(jc_runsview_parse(K, &t) == 0);
        JC_CHECK(t.post_outcome == 0);
        jc_sb_init(&o2);
        jc_runsview_render_row(&t, &o2);
        if (o2.data != NULL) {
            JC_CHECK(strstr(o2.data, "post_outcome") == NULL);
        }
        jc_sb_free(&o2);
    }
}

/* M330: a `learn_on_stop` event carries the mentor's token and tool-call cost,
 * so the run's own totals are SHORT and the row must say so. */
static void test_learn_on_stop_cost(void)
{
    /* A run with BOTH post_outcome AND learn_on_stop events: this is what a
     * real run with mentor cost looks like. The mentor's calls are themselves
     * post-outcome calls, so both events appear. The row reads
     *   post_outcome(totals_short) learn_on_stop(752.0k)
     * The flag says the totals are short, the new field says by how much.
     * Repeating "totals_short" would be redundant. */
    static const char *J =
        "{\"ts\":100,\"run\":\"r-ls\",\"event\":\"start\"}\n"
        "{\"ts\":150,\"run\":\"r-ls\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"tokens_used\":223346,\"tool_calls\":10}\n"
        "{\"ts\":160,\"run\":\"r-ls\",\"event\":\"post_outcome\","
        "\"outcome\":\"ok\"}\n"
        "{\"ts\":200,\"run\":\"r-ls\",\"event\":\"learn_on_stop\","
        "\"tokens\":752000,\"tool_calls\":43}\n";
    struct jc_run_summary s;
    struct jc_sb out;

    JC_CHECK(jc_runsview_parse(J, &s) == 0);
    JC_CHECK(s.learn_tokens == 752000.0);
    JC_CHECK(s.learn_calls == 43);
    JC_CHECK(s.post_outcome == 1);
    /* The end event's own totals are unchanged -- the learn_on_stop event
     * carries the delta, and the row must say the totals are SHORT. */
    JC_CHECK(s.tokens_used == 223346.0);
    JC_CHECK(s.tool_calls == 10);

    jc_sb_init(&out);
    jc_runsview_render_row(&s, &out);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        /* Both events must appear */
        JC_CHECK(strstr(out.data, "post_outcome") != NULL);
        JC_CHECK(strstr(out.data, "totals_short") != NULL);
        JC_CHECK(strstr(out.data, "learn_on_stop") != NULL);
    }
    jc_sb_free(&out);

    /* The JSON projection carries it too, so a supervisor can gate on it. */
    {
        cJSON *o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(jc_json_get_num(o, "learn_tokens", -1.0) == 752000.0);
            JC_CHECK(jc_json_get_num(o, "learn_calls", -1) == 43.0);
            JC_CHECK(cJSON_GetObjectItem(o, "post_outcome") != NULL);
            cJSON_Delete(o);
        }
    }

    /* A clean run without learn_on_stop must NOT carry the field. */
    {
        static const char *K =
            "{\"ts\":100,\"run\":\"r-nolearn\",\"event\":\"start\"}\n"
            "{\"ts\":150,\"run\":\"r-nolearn\",\"event\":\"end\",\"outcome\":\"ok\","
            "\"tokens_used\":500,\"tool_calls\":2}\n";
        struct jc_run_summary t;
        struct jc_sb o2;
        cJSON *jo2;
        JC_CHECK(jc_runsview_parse(K, &t) == 0);
        JC_CHECK(t.learn_tokens == 0.0);
        JC_CHECK(t.learn_calls == 0);
        jc_sb_init(&o2);
        jc_runsview_render_row(&t, &o2);
        if (o2.data != NULL) {
            JC_CHECK(strstr(o2.data, "learn_on_stop") == NULL);
        }
        jc_sb_free(&o2);
        jo2 = jc_runsview_json(&t);
        JC_CHECK(jo2 != NULL);
        if (jo2 != NULL) {
            JC_CHECK(cJSON_GetObjectItem(jo2, "learn_tokens") == NULL);
            JC_CHECK(cJSON_GetObjectItem(jo2, "learn_calls") == NULL);
            cJSON_Delete(jo2);
        }
    }
}

/* M420, born red: the two quality signals a supervisor most needs were written to
 * the journal and read by nobody. A run stuck on one error for ten retries, or one
 * that moved a goalpost, had a `runs` row indistinguishable from a clean run --
 * measured 2026-08-13, when a real attempt's goalpost warning fired TEN times and
 * still reported PASS. */
static void test_quality_signals(void)
{
    /* Split in two: one literal for the whole journal exceeded C89's 509-char
     * minimum (530). Caught by WERROR=1 on the test objects at M421 -- M420
     * gated on `make test` without it, so the violation shipped in a file whose
     * own project rules forbid it. */
    static const char *J1 =
        "{\"ts\":100,\"run\":\"r-11\",\"event\":\"start\","
        "\"ws\":\"/home/u/proj\"}\n"
        "{\"ts\":110,\"run\":\"r-11\",\"event\":\"verify\",\"exit\":1}\n"
        "{\"ts\":111,\"run\":\"r-11\",\"event\":\"verify_stuck\","
        "\"repeat\":2,\"sig\":\"error: no member named foo\"}\n";
    static const char *J2 =
        "{\"ts\":120,\"run\":\"r-11\",\"event\":\"test_assertion_edit\","
        "\"path\":\"tests/t.zig\",\"tool\":\"edit_file\"}\n"
        "{\"ts\":121,\"run\":\"r-11\",\"event\":\"test_assertion_edit\","
        "\"path\":\"tests/t.zig\",\"tool\":\"apply_patch\"}\n"
        "{\"ts\":130,\"run\":\"r-11\",\"event\":\"verify\",\"exit\":0}\n"
        "{\"ts\":150,\"run\":\"r-11\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"tokens_used\":5000,\"tool_calls\":9}\n";
    char J[1024];
    struct jc_run_summary s;
    struct jc_sb out;

    strcpy(J, J1);
    strcat(J, J2);
    JC_CHECK(jc_runsview_parse(J, &s) == 0);
    JC_CHECK(s.verify_stuck == 1);
    JC_CHECK(s.test_edits == 2);
    /* The run's own outcome is untouched: these annotate a green run, they do not
     * rewrite it. Withholding trust is the reader's job, not the parser's. */
    JC_CHECK(strcmp(s.outcome, "ok") == 0);
    /* M420: the journal's `start` now names its workspace, so a multi-project
     * machine can tell whose run this was. */
    JC_CHECK(strstr(s.ws, "/home/u/proj") != NULL);

    jc_sb_init(&out);
    jc_runsview_render_row(&s, &out);
    JC_CHECK(out.data != NULL);
    if (out.data != NULL) {
        JC_CHECK(strstr(out.data, "stuck=1") != NULL);
        JC_CHECK(strstr(out.data, "goalposts=2") != NULL);
    }
    jc_sb_free(&out);

    {
        cJSON *o = jc_runsview_json(&s);
        JC_CHECK(o != NULL);
        if (o != NULL) {
            JC_CHECK(cJSON_GetObjectItem(o, "verify_stuck") != NULL);
            JC_CHECK(cJSON_GetObjectItem(o, "test_edits") != NULL);
            JC_CHECK(cJSON_GetObjectItem(o, "ws") != NULL);
            cJSON_Delete(o);
        }
    }

    /* A clean run carries neither key -- an always-present "goalposts=0" would
     * train a reader to skip the column the one time it matters. */
    {
        static const char *CLEAN =
            "{\"ts\":1,\"run\":\"r-12\",\"event\":\"start\"}\n"
            "{\"ts\":9,\"run\":\"r-12\",\"event\":\"end\","
            "\"outcome\":\"ok\",\"tokens_used\":10,\"tool_calls\":1}\n";
        struct jc_run_summary c;
        struct jc_sb co;
        JC_CHECK(jc_runsview_parse(CLEAN, &c) == 0);
        JC_CHECK(c.verify_stuck == 0 && c.test_edits == 0);
        jc_sb_init(&co);
        jc_runsview_render_row(&c, &co);
        if (co.data != NULL) {
            JC_CHECK(strstr(co.data, "stuck=") == NULL);
            JC_CHECK(strstr(co.data, "goalposts=") == NULL);
        }
        jc_sb_free(&co);
        {
            cJSON *o = jc_runsview_json(&c);
            if (o != NULL) {
                JC_CHECK(cJSON_GetObjectItem(o, "verify_stuck") == NULL);
                JC_CHECK(cJSON_GetObjectItem(o, "test_edits") == NULL);
                cJSON_Delete(o);
            }
        }
    }
    /* M421: a TELEMETRY log is not a run journal, even though it now carries
     * `run` (M420) and uses the same `"event"` key.
     *
     * THE DEFECT. `runs <dir>` globs *.jsonl. Point --journal and --log at one
     * campaign directory -- the natural layout once the join invites reading both
     * -- and every run appeared TWICE: once real, once all-zeroes. Pre-M420 the
     * phantom was labelled with the FILENAME (visibly not a run); stamping `run`
     * on telemetry gave it the real run id, so it became an indistinguishable
     * duplicate. Worse, `constraint` is one of the three event names BOTH sinks
     * use, so the phantom row carried telemetry's count (6) beside the journal's
     * genuine 1.
     *
     * THE TEST. Not a blocklist of telemetry names -- that rots the day telemetry
     * adds one. A positive test: a journal is a file carrying a JOURNAL-EXCLUSIVE
     * event. The three shared names (`constraint`, `route`, `tool_call`) must
     * never qualify a file, which is what the second block below pins. */
    {
        static const char *TELEM =
            "{\"v\":1,\"ts\":1,\"event\":\"turn_start\",\"run\":\"r-13\","
            "\"sid\":\"s\",\"ws\":\"/w\",\"turn\":1,\"depth\":0}\n"
            "{\"v\":1,\"ts\":2,\"event\":\"model_call\",\"run\":\"r-13\","
            "\"in_tok\":100,\"out_tok\":9,\"ok\":true}\n"
            "{\"v\":1,\"ts\":3,\"event\":\"tool_call\",\"run\":\"r-13\","
            "\"name\":\"read_file\",\"ok\":true}\n"
            "{\"v\":1,\"ts\":4,\"event\":\"constraint\",\"run\":\"r-13\","
            "\"adopted\":6}\n"
            "{\"v\":1,\"ts\":5,\"event\":\"turn_end\",\"run\":\"r-13\"}\n";
        struct jc_run_summary t;
        JC_CHECK(jc_runsview_parse(TELEM, &t) == -1);
    }
    {
        /* The shared names ALONE are not a journal either -- this is the block a
         * future "just count any recognised event" simplification would break. */
        static const char *SHARED =
            "{\"ts\":1,\"run\":\"r-14\",\"event\":\"constraint\",\"adopted\":2}\n"
            "{\"ts\":2,\"run\":\"r-14\",\"event\":\"route\"}\n"
            "{\"ts\":3,\"run\":\"r-14\",\"event\":\"tool_call\"}\n";
        struct jc_run_summary t;
        JC_CHECK(jc_runsview_parse(SHARED, &t) == -1);
    }
    {
        /* And the half that must SURVIVE the fix: a run killed before it could
         * write `end` has only `start`, and that row is exactly the diagnostic a
         * supervisor needs (the 22-minute hang that wrote a 0-byte journal has a
         * cousin that writes one line). It must still parse, with outcome "?". */
        static const char *STARTONLY =
            "{\"ts\":7,\"run\":\"r-15\",\"event\":\"start\",\"ws\":\"/w\"}\n";
        struct jc_run_summary t;
        JC_CHECK(jc_runsview_parse(STARTONLY, &t) == 0);
        JC_CHECK(strcmp(t.outcome, "?") == 0);
        JC_CHECK(strcmp(t.ws, "/w") == 0);
    }
}

/* M598: the learn_on_stop event says what the draft would commit. A draft with
 * bytes and no parseable section (`draft_parsed_nothing`:1) renders as
 * `draft=empty` and is reported in the JSON row; a pre-M598 event without the
 * fields is "not measured" (-1), never "empty" -- the presence of the key IS the
 * flag, as with degraded (M431c). */
static void test_learn_draft_empty(void)
{
    static const char *J =
        "{\"ts\":100,\"run\":\"r-de\",\"event\":\"start\"}\n"
        "{\"ts\":150,\"run\":\"r-de\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"tokens_used\":1000,\"tool_calls\":2}\n"
        "{\"ts\":160,\"run\":\"r-de\",\"event\":\"learn_on_stop\","
        "\"tokens\":500,\"tool_calls\":1,\"draft_items\":0,"
        "\"draft_parsed_nothing\":1}\n";
    static const char *K =
        "{\"ts\":100,\"run\":\"r-ok\",\"event\":\"start\"}\n"
        "{\"ts\":150,\"run\":\"r-ok\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"tokens_used\":1000,\"tool_calls\":2}\n"
        "{\"ts\":160,\"run\":\"r-ok\",\"event\":\"learn_on_stop\","
        "\"tokens\":500,\"tool_calls\":1,\"draft_items\":3,"
        "\"draft_parsed_nothing\":0}\n";
    static const char *OLD =
        "{\"ts\":100,\"run\":\"r-old\",\"event\":\"start\"}\n"
        "{\"ts\":150,\"run\":\"r-old\",\"event\":\"end\",\"outcome\":\"ok\","
        "\"tokens_used\":1000,\"tool_calls\":2}\n"
        "{\"ts\":160,\"run\":\"r-old\",\"event\":\"learn_on_stop\","
        "\"tokens\":500,\"tool_calls\":1}\n";
    struct jc_run_summary s;
    struct jc_sb row;
    cJSON *o;
    char *js;

    JC_CHECK(jc_runsview_parse(J, &s) == 0);
    JC_CHECK(s.learn_draft_empty == 1);
    JC_CHECK(s.learn_draft_items == 0);
    jc_sb_init(&row);
    jc_runsview_render_row(&s, &row);
    JC_CHECK(row.data != NULL && strstr(row.data, "draft=empty") != NULL);
    jc_sb_free(&row);
    o = jc_runsview_json(&s);
    JC_CHECK(o != NULL);
    js = (o != NULL) ? cJSON_PrintUnformatted(o) : NULL;
    JC_CHECK(js != NULL && strstr(js, "\"learn_draft_empty\":true") != NULL);
    JC_CHECK(js != NULL && strstr(js, "\"learn_draft_items\":0") != NULL);
    if (js != NULL) { cJSON_free(js); }
    if (o != NULL) { cJSON_Delete(o); }

    /* A draft that parses: no accusation in the row, the count in the JSON. */
    JC_CHECK(jc_runsview_parse(K, &s) == 0);
    JC_CHECK(s.learn_draft_empty == 0 && s.learn_draft_items == 3);
    jc_sb_init(&row);
    jc_runsview_render_row(&s, &row);
    JC_CHECK(row.data != NULL && strstr(row.data, "draft=") == NULL);
    jc_sb_free(&row);

    /* A pre-M598 event: not measured, never "empty". */
    JC_CHECK(jc_runsview_parse(OLD, &s) == 0);
    JC_CHECK(s.learn_draft_empty == 0 && s.learn_draft_items == -1);
    o = jc_runsview_json(&s);
    js = (o != NULL) ? cJSON_PrintUnformatted(o) : NULL;
    JC_CHECK(js != NULL && strstr(js, "learn_draft") == NULL);
    if (js != NULL) { cJSON_free(js); }
    if (o != NULL) { cJSON_Delete(o); }
}

void test_runsview(void)
{
    test_learn_draft_empty();
    test_quality_signals();
    test_learn_on_stop_cost();
    test_post_outcome_flag();
    test_run_version();
    test_full_run();
    test_budget_rollback_truncated();
    test_runs_json();
}
