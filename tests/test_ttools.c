/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_ttools.c - the pure cores of the test-only helper tools
 * (tests/tools: mockmodel/ptydrive/jsonq) that back the Python-free smoke
 * tier (tests/smoke/, M209).
 *
 * The mm_http_feed cases are the permanent encoding of the M201 /
 * ANECDOTES #18 lesson: a request reader must complete on Content-Length,
 * never on a pause in the byte stream. Each case feeds the parser in
 * adversarial splits and asserts COMPLETE arrives exactly once the full
 * declared body has -- a naive break-on-first-boundary reader fails the
 * late-body case (verified red before this landed). */

#include "jc_test.h"
#include "tools/mm_core.h"
#include "tools/pd_core.h"
#include "tools/jq_core.h"
#include "tools/tt.h"
#include "cJSON.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

/* --- mm: script parse + select ------------------------------------------- */

static void tt_mm_script(void)
{
    struct mm_script s;
    char err[128];
    const struct mm_rule *r;
    static const char *good =
        "# comment\n"
        "wire openai\n"
        "\n"
        "rule\n"
        "  count 1\n"
        "  tool read_file {\"path\":\"note.txt\"}\n"
        "rule\n"
        "  match \"\\\"role\\\":\\\"tool\\\"\"\n"
        "  text DONE_ANSWER_42\n"
        "  usage 30 7\n"
        "rule\n"
        "  status 500\n"
        "  body {\"error\":\"unexpected\"}\n";

    JC_CHECK(mm_script_parse(good, &s, err, sizeof(err)) == 0);
    JC_CHECK(s.nrules == 3);
    JC_CHECK(s.rules[0].action == MM_ACT_TOOL);
    JC_CHECK(s.rules[0].count == 1);
    JC_CHECK_STR(s.rules[0].arg1, "read_file");
    JC_CHECK_STR(s.rules[0].arg2, "{\"path\":\"note.txt\"}");
    JC_CHECK(s.rules[1].action == MM_ACT_TEXT);
    JC_CHECK(s.rules[1].nmatch == 1);
    JC_CHECK_STR(s.rules[1].match[0], "\"role\":\"tool\"");
    JC_CHECK(s.rules[1].usage_in == 30 && s.rules[1].usage_out == 7);
    JC_CHECK(s.rules[2].action == MM_ACT_STATUS);
    JC_CHECK(s.rules[2].status == 500);
    JC_CHECK_STR(s.rules[2].arg1, "{\"error\":\"unexpected\"}");

    /* selection: count wins on request 1, match on request 2, else default */
    r = mm_select(&s, "anything", 8, 1);
    JC_CHECK(r == &s.rules[0]);
    r = mm_select(&s, "x \"role\":\"tool\" y", 17, 2);
    JC_CHECK(r == &s.rules[1]);
    r = mm_select(&s, "nothing matches", 15, 2);
    JC_CHECK(r == &s.rules[2]);
    mm_script_free(&s);

    /* first match wins: two catch-alls -> the earlier one */
    JC_CHECK(mm_script_parse("rule\n text A\nrule\n text B\n",
                             &s, err, sizeof(err)) == 0);
    r = mm_select(&s, "", 0, 1);
    if (JC_REQUIRE(r != NULL && r->action == MM_ACT_TEXT)) {
        JC_CHECK_STR(r->arg1, "A");
    }
    mm_script_free(&s);

    /* several match lines AND together */
    JC_CHECK(mm_script_parse(
        "rule\n match \"aa\"\n match \"bb\"\n text X\n",
        &s, err, sizeof(err)) == 0);
    JC_CHECK(mm_select(&s, "aa only", 7, 1) == NULL);
    JC_CHECK(mm_select(&s, "aa and bb", 9, 1) != NULL);
    mm_script_free(&s);

    /* nomatch (M216): the substring must be ABSENT; ANDs with match */
    JC_CHECK(mm_script_parse(
        "rule\n match \"aa\"\n nomatch \"zz\"\n text X\n",
        &s, err, sizeof(err)) == 0);
    JC_CHECK(s.rules[0].match_neg[0] == 0);
    JC_CHECK(s.rules[0].match_neg[1] == 1);
    JC_CHECK(mm_select(&s, "aa here", 7, 1) != NULL);   /* aa, no zz */
    JC_CHECK(mm_select(&s, "aa and zz", 9, 1) == NULL); /* zz present */
    JC_CHECK(mm_select(&s, "no marker", 9, 1) == NULL); /* no aa */
    mm_script_free(&s);

    /* the embed action (M213): parsed with its word list; the pure
     * count is case-insensitive and non-overlapping (Python str.count) */
    JC_CHECK(mm_script_parse("rule\n embed hooks state\n",
                             &s, err, sizeof(err)) == 0);
    JC_CHECK(s.rules[0].action == MM_ACT_EMBED);
    JC_CHECK_STR(s.rules[0].arg1, "hooks state");
    mm_script_free(&s);
    JC_CHECK(mm_script_parse("rule\n embed \n", &s, err,
                             sizeof(err)) == -1);
    JC_CHECK(mm_count_ci("Hooks use hooks; HOOKS!", "hooks") == 3);
    JC_CHECK(mm_count_ci("aaaa", "aa") == 2);      /* non-overlapping */
    JC_CHECK(mm_count_ci("banana", "zork") == 0);
    JC_CHECK(mm_count_ci("x", "") == 0);

    /* body-file (M212): parsed only after status; binary-safe rendering */
    JC_CHECK(mm_script_parse("rule\n status 200\n body-file /tmp/x.bin\n",
                             &s, err, sizeof(err)) == 0);
    JC_CHECK_STR(s.rules[0].body_file, "/tmp/x.bin");
    mm_script_free(&s);
    JC_CHECK(mm_script_parse("rule\n body-file /tmp/x.bin\n text A\n",
                             &s, err, sizeof(err)) == -1);
    {
        static const char bin[] = "ID3\0\0\377\373hello";
        char *resp = NULL;
        size_t rlen = 0;
        JC_CHECK(mm_render_status_body(200, bin, sizeof(bin) - 1,
                                       &resp, &rlen) == 0);
        JC_CHECK(strstr(resp, "HTTP/1.1 200") == resp);
        JC_CHECK(strstr(resp, "Content-Length: 12") != NULL);
        /* the NUL-containing body is appended verbatim */
        {
            const char *he = strstr(resp, "\r\n\r\n");
            JC_CHECK(he != NULL &&
                     rlen == (size_t)(he + 4 - resp) + 12);
            JC_CHECK(he != NULL && memcmp(he + 4, bin, 12) == 0);
        }
        free(resp);
    }

    /* location (M472): what makes a 3xx expressible in this tier at all. Same
     * shape as body-file above -- only after status, and it must round-trip. */
    JC_CHECK(mm_script_parse("rule\n status 302\n location /v2/chat\n body {}\n",
                             &s, err, sizeof(err)) == 0);
    JC_CHECK(s.rules[0].status == 302);
    JC_CHECK_STR(s.rules[0].location, "/v2/chat");
    mm_script_free(&s);
    /* Rejected before an action, like body/body-file: a Location with no status
     * would silently do nothing, and a mock that quietly ignores a directive is
     * how a driver passes for the wrong reason. */
    JC_CHECK(mm_script_parse("rule\n location /v2/chat\n text A\n",
                             &s, err, sizeof(err)) == -1);
    /* Absent by default -- a plain `status` reply must not grow a header. */
    JC_CHECK(mm_script_parse("rule\n status 500\n body {}\n",
                             &s, err, sizeof(err)) == 0);
    JC_CHECK(s.rules[0].location == NULL);
    mm_script_free(&s);

    /* the larger predicate (M212): routes by body size */
    JC_CHECK(mm_script_parse(
        "rule\n larger 10\n status 400\n body big\nrule\n text small\n",
        &s, err, sizeof(err)) == 0);
    JC_CHECK(s.rules[0].larger == 10);
    r = mm_select(&s, "12345678901", 11, 1);       /* > 10 bytes */
    JC_CHECK(r != NULL && r->action == MM_ACT_STATUS);
    r = mm_select(&s, "1234567890", 10, 1);        /* exactly 10: not > */
    JC_CHECK(r != NULL && r->action == MM_ACT_TEXT);
    mm_script_free(&s);
    JC_CHECK(mm_script_parse("rule\n larger 0\n text A\n",
                             &s, err, sizeof(err)) == -1);

    /* the delay modifier (M211): parsed per rule, rejected when < 1ms */
    JC_CHECK(mm_script_parse("rule\n delay 800\n text A\nrule\n text B\n",
                             &s, err, sizeof(err)) == 0);
    JC_CHECK(s.rules[0].delay_ms == 800);
    JC_CHECK(s.rules[1].delay_ms == 0);
    mm_script_free(&s);
    JC_CHECK(mm_script_parse("rule\n delay 0\n text A\n",
                             &s, err, sizeof(err)) == -1);

    /* errors carry line numbers */
    JC_CHECK(mm_script_parse("rule\n bogus x\n text A\n",
                             &s, err, sizeof(err)) == -1);
    JC_CHECK(strstr(err, "line 2") != NULL);
    JC_CHECK(mm_script_parse("wire anthropic\nrule\n text A\n",
                             &s, err, sizeof(err)) == -1);
    JC_CHECK(strstr(err, "anthropic") != NULL);
    JC_CHECK(mm_script_parse("rule\n count 1\n", &s, err,
                             sizeof(err)) == -1);   /* no action */
    JC_CHECK(mm_script_parse("text A\n", &s, err,
                             sizeof(err)) == -1);   /* outside a rule */
    JC_CHECK(mm_script_parse("rule\n text A\n text B\n",
                             &s, err, sizeof(err)) == -1); /* two actions */
    JC_CHECK(mm_script_parse("", &s, err, sizeof(err)) == -1);
    /* error exits with an IN-PROGRESS rule holding allocations: these two
     * leaked before mm_parse_fail existed (the ci ASan gate caught the
     * first as a 2-byte leak: "A\0"); the leak assertions live in the
     * sanitizer, so keeping the inputs here keeps the gate armed */
    JC_CHECK(mm_script_parse("rule\n match \"x\"\nrule\n text A\n",
                             &s, err, sizeof(err)) == -1);
    JC_CHECK(mm_script_parse("rule\n tool read_file {\"a\":1}\n bogus\n",
                             &s, err, sizeof(err)) == -1);
}

/* --- mm: response rendering ---------------------------------------------- */

/* The rendered head must declare exactly the body it carries. */
static void tt_check_content_length(const char *resp, size_t len)
{
    const char *cl = strstr(resp, "Content-Length: ");
    const char *he = strstr(resp, "\r\n\r\n");
    long declared;
    size_t body_len;

    JC_CHECK(cl != NULL && he != NULL);
    if (cl == NULL || he == NULL)
        return;
    declared = strtol(cl + 16, NULL, 10);
    body_len = len - (size_t)(he + 4 - resp);
    JC_CHECK((long)body_len == declared);
}

static void tt_mm_render(void)
{
    struct mm_script s;
    char err[128];
    char *resp = NULL;
    size_t rlen = 0;

    JC_CHECK(mm_script_parse(
        "rule\n text hi \"there\"\n usage 21 6\n"
        "rule\n count 2\n tool read_file {\"path\":\"a.txt\"}\n"
        "rule\n count 3\n status 400\n body {\"e\":1}\n"
        "rule\n count 4\n stall header\n"
        "rule\n count 5\n stall mid\n",
        &s, err, sizeof(err)) == 0);

    /* text: role/content delta, stop chunk with usage, [DONE], and a
     * JSON-escaped payload -- the exact Python-mock shape. */
    JC_CHECK(mm_render_response(&s.rules[0], &resp, &rlen) == 0);
    JC_CHECK(strstr(resp, "HTTP/1.1 200 OK") == resp);
    JC_CHECK(strstr(resp, "Content-Type: text/event-stream") != NULL);
    JC_CHECK(strstr(resp, "\"content\":\"hi \\\"there\\\"\"") != NULL);
    JC_CHECK(strstr(resp, "\"finish_reason\":\"stop\"") != NULL);
    JC_CHECK(strstr(resp, "\"prompt_tokens\":21") != NULL);
    JC_CHECK(strstr(resp, "\"completion_tokens\":6") != NULL);
    JC_CHECK(strstr(resp, "data: [DONE]\n\n") != NULL);
    tt_check_content_length(resp, rlen);
    free(resp);

    /* tool: function name + arguments as an escaped JSON string */
    JC_CHECK(mm_render_response(&s.rules[1], &resp, &rlen) == 0);
    JC_CHECK(strstr(resp, "\"name\":\"read_file\"") != NULL);
    JC_CHECK(strstr(resp,
        "\"arguments\":\"{\\\"path\\\":\\\"a.txt\\\"}\"") != NULL);
    JC_CHECK(strstr(resp, "\"finish_reason\":\"tool_calls\"") != NULL);
    tt_check_content_length(resp, rlen);
    free(resp);

    /* status: plain reply, correct code + body */
    JC_CHECK(mm_render_response(&s.rules[2], &resp, &rlen) == 0);
    JC_CHECK(strstr(resp, "HTTP/1.1 400") == resp);
    JC_CHECK(strstr(resp, "{\"e\":1}") != NULL);
    tt_check_content_length(resp, rlen);
    free(resp);

    /* stall header: SSE head, NO Content-Length (the peer must wait) */
    JC_CHECK(mm_render_response(&s.rules[3], &resp, &rlen) == 0);
    JC_CHECK(strstr(resp, "Content-Length") == NULL);
    JC_CHECK(strstr(resp, "text/event-stream") != NULL);
    free(resp);

    /* stall mid: head + one delta, then the shell holds */
    JC_CHECK(mm_render_response(&s.rules[4], &resp, &rlen) == 0);
    JC_CHECK(strstr(resp, "Content-Length") == NULL);
    JC_CHECK(strstr(resp, "\"content\":\"partial\"") != NULL);
    JC_CHECK(strstr(resp, "[DONE]") == NULL);
    free(resp);

    mm_script_free(&s);
}

/* --- mm: the incremental HTTP request parser ------------------------------ */

static void tt_mm_http(void)
{
    struct mm_http h;
    static const char req[] =
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: x\r\n"
        "content-length: 11\r\n"
        "\r\n"
        "hello world";
    size_t i;
    const char *body;
    size_t blen = 0;

    /* 1-byte feeds: COMPLETE exactly at the last body byte, never before.
     * This is the case a naive break-on-boundary reader fails. */
    mm_http_init(&h);
    for (i = 0; i + 1 < sizeof(req) - 1; i++)
        JC_CHECK(mm_http_feed(&h, req + i, 1) == MM_HTTP_NEED_MORE);
    JC_CHECK(mm_http_feed(&h, req + i, 1) == MM_HTTP_COMPLETE);
    body = mm_http_body(&h, &blen);
    JC_CHECK(blen == 11);
    JC_CHECK(body != NULL && memcmp(body, "hello world", 11) == 0);
    mm_http_free(&h);

    /* split exactly at the head/body boundary: the head alone must NOT
     * complete when a Content-Length is declared */
    mm_http_init(&h);
    {
        const char *split = strstr(req, "\r\n\r\n") + 4;
        JC_CHECK(mm_http_feed(&h, req, (size_t)(split - req))
                 == MM_HTTP_NEED_MORE);
        JC_CHECK(mm_http_feed(&h, split, strlen(split))
                 == MM_HTTP_COMPLETE);
    }
    mm_http_free(&h);

    /* a large declared body arriving late: pauses yield NEED_MORE (fed as
     * an empty chunk between the halves, like a recv timeout would) */
    mm_http_init(&h);
    {
        static const char head[] =
            "POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\n";
        JC_CHECK(mm_http_feed(&h, head, sizeof(head) - 1)
                 == MM_HTTP_NEED_MORE);
        JC_CHECK(mm_http_feed(&h, "12345", 5) == MM_HTTP_NEED_MORE);
        JC_CHECK(mm_http_feed(&h, "", 0) == MM_HTTP_NEED_MORE);
        JC_CHECK(mm_http_feed(&h, "67890", 5) == MM_HTTP_COMPLETE);
        mm_http_body(&h, &blen);
        JC_CHECK(blen == 10);
    }
    mm_http_free(&h);

    /* no Content-Length (GET): complete at end-of-head */
    mm_http_init(&h);
    {
        static const char get[] = "GET /x HTTP/1.1\r\nHost: y\r\n\r\n";
        JC_CHECK(mm_http_feed(&h, get, sizeof(get) - 1)
                 == MM_HTTP_COMPLETE);
        mm_http_body(&h, &blen);
        JC_CHECK(blen == 0);
    }
    mm_http_free(&h);

    /* bare \n\n head terminator is tolerated */
    mm_http_init(&h);
    {
        static const char lf[] = "POST / HTTP/1.1\nContent-Length: 2\n\nok";
        JC_CHECK(mm_http_feed(&h, lf, sizeof(lf) - 1) == MM_HTTP_COMPLETE);
    }
    mm_http_free(&h);

    /* chunked transfer-encoding is a loud error, not a silent truncation */
    mm_http_init(&h);
    {
        static const char ch[] =
            "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
        JC_CHECK(mm_http_feed(&h, ch, sizeof(ch) - 1) == MM_HTTP_ERROR);
    }
    mm_http_free(&h);
}

/* --- pd: script parse / unescape / match ---------------------------------- */

static void tt_pd(void)
{
    struct pd_script s;
    char err[128];
    char buf[64];
    size_t n = 0;

    /* unescape */
    JC_CHECK(pd_unescape("a\\r\\n\\t\\\\\\\"\\x41", buf, sizeof(buf),
                         &n) == 0);
    JC_CHECK(n == 7);
    JC_CHECK(memcmp(buf, "a\r\n\t\\\"A", 7) == 0);
    JC_CHECK(pd_unescape("bad\\q", buf, sizeof(buf), &n) == -1);
    JC_CHECK(pd_unescape("bad\\xZZ", buf, sizeof(buf), &n) == -1);
    JC_CHECK(pd_unescape("overflow", buf, 4, &n) == -1);

    /* match across accumulation: the pattern only exists in the whole */
    {
        static const char acc[] = "...he";
        static const char acc2[] = "...hello...";
        JC_CHECK(pd_match(acc, sizeof(acc) - 1, "hello", 5) == NULL);
        JC_CHECK(pd_match(acc2, sizeof(acc2) - 1, "hello", 5) != NULL);
    }

    /* signals */
    JC_CHECK(pd_signal_from_name("TERM") == SIGTERM);
    JC_CHECK(pd_signal_from_name("INT") == SIGINT);
    JC_CHECK(pd_signal_from_name("KILL") == SIGKILL);
    JC_CHECK(pd_signal_from_name("USR1") == -1);

    /* a full script parses into the right commands */
    JC_CHECK(pd_script_parse(
        "# comment\n"
        "expect \"> \" 10\n"
        "send \"/help\\r\"\n"
        "delay 200\n"
        "drain 100\n"
        "winsize 24 120\n"
        "signal TERM\n"
        "waitexit 10\n"
        "assertexit 0\n",
        &s, err, sizeof(err)) == 0);
    JC_CHECK(s.ncmds == 8);
    JC_CHECK(s.cmds[0].kind == PD_CMD_EXPECT && s.cmds[0].a == 10);
    JC_CHECK_STR(s.cmds[0].text, "> ");
    JC_CHECK(s.cmds[1].kind == PD_CMD_SEND && s.cmds[1].text_len == 6);
    JC_CHECK(s.cmds[1].text[5] == '\r');
    JC_CHECK(s.cmds[2].kind == PD_CMD_DELAY && s.cmds[2].a == 200);
    JC_CHECK(s.cmds[3].kind == PD_CMD_DRAIN && s.cmds[3].a == 100);
    JC_CHECK(s.cmds[4].kind == PD_CMD_WINSIZE &&
             s.cmds[4].a == 24 && s.cmds[4].b == 120);
    JC_CHECK(s.cmds[5].kind == PD_CMD_SIGNAL && s.cmds[5].a == SIGTERM);
    JC_CHECK(s.cmds[6].kind == PD_CMD_WAITEXIT && s.cmds[6].a == 10);
    JC_CHECK(s.cmds[7].kind == PD_CMD_ASSERTEXIT && s.cmds[7].a == 0);
    pd_script_free(&s);

    /* waitexit with a default timeout */
    JC_CHECK(pd_script_parse("waitexit\n", &s, err, sizeof(err)) == 0);
    JC_CHECK(s.cmds[0].a == 10);
    pd_script_free(&s);

    /* errors carry line numbers */
    JC_CHECK(pd_script_parse("expect \"a\"\nbogus\n", &s, err,
                             sizeof(err)) == -1);
    JC_CHECK(strstr(err, "line 2") != NULL);
    JC_CHECK(pd_script_parse("send \"unterminated\n", &s, err,
                             sizeof(err)) == -1);
    JC_CHECK(pd_script_parse("expect \"\"\n", &s, err, sizeof(err)) == -1);
    JC_CHECK(pd_script_parse("", &s, err, sizeof(err)) == -1);
}

/* --- jq: path parse + lookup ---------------------------------------------- */

static void tt_jq(void)
{
    struct jq_path p;
    char err[128];
    cJSON *doc;
    cJSON *hit;

    doc = cJSON_Parse("{\"a\":{\"b\":[10,{\"c\":\"deep\"},30]},"
                      "\"n\":5,\"t\":true,\"z\":null,"
                      "\"arr\":[{\"id\":\"first\"}]}");
    JC_CHECK(doc != NULL);

    /* whole document */
    JC_CHECK(jq_path_parse(".", &p, err, sizeof(err)) == 0);
    JC_CHECK(p.nsteps == 0);
    JC_CHECK(jq_lookup(doc, &p) == doc);
    jq_path_free(&p);

    /* nested key + index + key */
    JC_CHECK(jq_path_parse(".a.b[1].c", &p, err, sizeof(err)) == 0);
    JC_CHECK(p.nsteps == 4);
    hit = jq_lookup(doc, &p);
    JC_CHECK(hit != NULL && cJSON_IsString(hit));
    JC_CHECK(hit != NULL && strcmp(hit->valuestring, "deep") == 0);
    jq_path_free(&p);

    /* index out of bounds / missing key / wrong container type */
    JC_CHECK(jq_path_parse(".a.b[9]", &p, err, sizeof(err)) == 0);
    JC_CHECK(jq_lookup(doc, &p) == NULL);
    jq_path_free(&p);
    JC_CHECK(jq_path_parse(".nope", &p, err, sizeof(err)) == 0);
    JC_CHECK(jq_lookup(doc, &p) == NULL);
    jq_path_free(&p);
    JC_CHECK(jq_path_parse(".n[0]", &p, err, sizeof(err)) == 0);
    JC_CHECK(jq_lookup(doc, &p) == NULL);   /* number is not an array */
    jq_path_free(&p);

    /* .arr[0].id */
    JC_CHECK(jq_path_parse(".arr[0].id", &p, err, sizeof(err)) == 0);
    hit = jq_lookup(doc, &p);
    JC_CHECK(hit != NULL && cJSON_IsString(hit));
    jq_path_free(&p);

    /* type checks */
    JC_CHECK(jq_path_parse(".t", &p, err, sizeof(err)) == 0);
    hit = jq_lookup(doc, &p);
    JC_CHECK(jq_type_matches(hit, "bool") == 1);
    JC_CHECK(jq_type_matches(hit, "string") == 0);
    JC_CHECK(jq_type_matches(hit, "flavour") == -1);
    jq_path_free(&p);

    /* malformed paths */
    JC_CHECK(jq_path_parse("a.b", &p, err, sizeof(err)) == -1);
    JC_CHECK(jq_path_parse("..", &p, err, sizeof(err)) == -1);
    JC_CHECK(jq_path_parse(".a[x]", &p, err, sizeof(err)) == -1);
    JC_CHECK(jq_path_parse(".a[-1]", &p, err, sizeof(err)) == -1);
    JC_CHECK(jq_path_parse(".a[1", &p, err, sizeof(err)) == -1);

    cJSON_Delete(doc);
}

/* The shared deadline multiplier (M273). Every tool with a deadline scales
 * by this; a layer that silently did not is what shot mockmodel out from
 * under a healthy V2f run. The contract worth pinning is the FLOOR: no input
 * may ever shorten a deadline, because a 0 or a typo must degrade to
 * "unscaled", never to "expires immediately". */
static void tt_mult(void)
{
    JC_CHECK(tt_mult_parse("8") == 8);
    JC_CHECK(tt_mult_parse("16") == 16);
    JC_CHECK(tt_mult_parse("1") == 1);
    JC_CHECK(tt_mult_parse(NULL) == 1);      /* unset */
    JC_CHECK(tt_mult_parse("") == 1);        /* set but empty */
    JC_CHECK(tt_mult_parse("0") == 1);       /* never zero out a deadline */
    JC_CHECK(tt_mult_parse("-4") == 1);      /* never negative */
    JC_CHECK(tt_mult_parse("abc") == 1);     /* not a number at all */
    JC_CHECK(tt_mult_parse("4x") == 4);      /* numeric prefix is honoured */
}

void test_ttools(void)
{
    tt_mm_script();
    tt_mm_render();
    tt_mm_http();
    tt_pd();
    tt_jq();
    tt_mult();
}
