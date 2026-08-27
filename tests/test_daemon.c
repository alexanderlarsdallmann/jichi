/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_daemon.c - the warm-process daemon request codec. */

#include "jc_test.h"
#include "jc_daemon.h"
#include "jc_mem.h"
#include <stdlib.h>
#include <string.h>

static void test_prompt_roundtrip(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char *line = jc_daemon_build_prompt("fix the bug", jc_test_tmp("proj"), "auto", 2);
    struct jc_daemon_req req;

    JC_CHECK(line != NULL);
    /* The wire line ends in a newline. */
    JC_CHECK(line[strlen(line) - 1] == '\n');
    JC_CHECK(jc_daemon_parse_request(line, &req, a) == JC_OK);
    JC_CHECK(req.type == JC_DREQ_PROMPT);
    JC_CHECK_STR(req.prompt, "fix the bug");
    JC_CHECK_STR(req.cwd, jc_test_tmp("proj"));
    JC_CHECK_STR(req.mode, "auto");
    JC_CHECK(req.fmt == 2);   /* M431g: 2 = jsonl */

    free(line);
    jc_arena_free(a);
}

/* M431g: the format field carries THREE values, not a boolean. It was
 * text-or-jsonl, so `--output json` over --connect was silently served as TEXT --
 * the single-object contract EMBEDDING.md calls Stable was unavailable over the
 * daemon. Additive on the wire: a new VALUE, so an old client still parses. */
static void test_format_three_values(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_daemon_req req;
    char *line;

    /* json survives the round trip -- the case that was impossible before. */
    line = jc_daemon_build_prompt("q", NULL, NULL, 1);
    JC_CHECK(line != NULL && strstr(line, "\"format\":\"json\"") != NULL);
    JC_CHECK(jc_daemon_parse_request(line, &req, a) == JC_OK);
    JC_CHECK(req.fmt == 1);
    free(line);

    /* and the other two still render the strings the old client sent. */
    line = jc_daemon_build_prompt("q", NULL, NULL, 0);
    JC_CHECK(line != NULL && strstr(line, "\"format\":\"text\"") != NULL);
    free(line);
    line = jc_daemon_build_prompt("q", NULL, NULL, 2);
    JC_CHECK(line != NULL && strstr(line, "\"format\":\"jsonl\"") != NULL);
    free(line);

    /* BACKWARD COMPATIBILITY, the reason this is additive: a request written by a
     * pre-M431g client parses to the same code it always did. */
    JC_CHECK(jc_daemon_parse_request(
        "{\"v\":1,\"type\":\"prompt\",\"prompt\":\"q\",\"format\":\"jsonl\"}\n",
        &req, a) == JC_OK && req.fmt == 2);
    JC_CHECK(jc_daemon_parse_request(
        "{\"v\":1,\"type\":\"prompt\",\"prompt\":\"q\",\"format\":\"text\"}\n",
        &req, a) == JC_OK && req.fmt == 0);
    /* An unknown value is TEXT rather than an error: a daemon must not refuse a
     * request because a newer client named a format it does not know. */
    JC_CHECK(jc_daemon_parse_request(
        "{\"v\":1,\"type\":\"prompt\",\"prompt\":\"q\",\"format\":\"future\"}\n",
        &req, a) == JC_OK && req.fmt == 0);

    jc_arena_free(a);
}

static void test_text_default(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char *line = jc_daemon_build_prompt("hi", NULL, NULL, 0);
    struct jc_daemon_req req;

    JC_CHECK(jc_daemon_parse_request(line, &req, a) == JC_OK);
    JC_CHECK(req.fmt == 0);   /* absent/unknown format => text */
    JC_CHECK(req.cwd == NULL);
    JC_CHECK(req.mode == NULL);

    free(line);
    jc_arena_free(a);
}

static void test_ctl_and_errors(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char *ping = jc_daemon_build_ctl("ping");
    struct jc_daemon_req req;

    JC_CHECK(jc_daemon_parse_request(ping, &req, a) == JC_OK);
    JC_CHECK(req.type == JC_DREQ_PING);
    free(ping);

    /* A prompt-type with no prompt is invalid. */
    JC_CHECK(jc_daemon_parse_request("{\"type\":\"prompt\"}", &req, a) ==
             JC_ERR_INVALID);
    /* Non-JSON is a parse error. */
    JC_CHECK(jc_daemon_parse_request("not json", &req, a) == JC_ERR_PARSE);
    /* An unknown type still parses (server rejects it). */
    JC_CHECK(jc_daemon_parse_request("{\"type\":\"bogus\"}", &req, a) == JC_OK);
    JC_CHECK(req.type == JC_DREQ_UNKNOWN);
    /* Default type is "prompt" when omitted. */
    JC_CHECK(jc_daemon_parse_request("{\"prompt\":\"x\"}", &req, a) == JC_OK);
    JC_CHECK(req.type == JC_DREQ_PROMPT);

    JC_CHECK_STR(jc_daemon_type_name(JC_DREQ_SHUTDOWN), "shutdown");

    jc_arena_free(a);
}

/* M528: `hello` -- capability discovery, and the one honest answer to "what
 * authenticates me?". It is ADDITIVE and NOT mandatory: docs/EMBEDDING.md
 * declares the bare `prompt`/`ping`/`shutdown` shapes Stable, so requiring a
 * handshake would break a promise. A client that sends one gets to know the
 * protocol version, the limits, and -- the point -- exactly which mechanism is
 * guarding the socket it just connected to. */
static void test_hello(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_daemon_req req;
    struct jc_daemon_hello h;
    char *line;

    /* The verb parses, and does not disturb the legacy verbs. */
    JC_CHECK(jc_daemon_parse_request("{\"type\":\"hello\"}", &req, a) == JC_OK);
    JC_CHECK(req.type == JC_DREQ_HELLO);
    JC_CHECK_STR(jc_daemon_type_name(JC_DREQ_HELLO), "hello");

    /* A client can build one, and it round-trips. */
    line = jc_daemon_build_hello("test-client");
    if (JC_REQUIRE(line != NULL)) {
        JC_CHECK(jc_daemon_parse_request(line, &req, a) == JC_OK);
        JC_CHECK(req.type == JC_DREQ_HELLO);
        /* Newline-framed like every other request. */
        JC_CHECK(strchr(line, '\n') != NULL);
        free(line);
    }

    /* The reply states the posture rather than implying it. */
    memset(&h, 0, sizeof(h));
    h.agent = "jichi 9.9.9";
    h.max_line = 1048576L;
    h.workers = 4;
    h.uid = 1234UL;
    h.mode_verified = 1;
    h.peercred = 0;
    line = jc_daemon_build_hello_ok(&h);
    if (JC_REQUIRE(line != NULL)) {
        JC_CHECK(strstr(line, "\"type\":\"hello.ok\"") != NULL);
        JC_CHECK(strstr(line, "jichi 9.9.9") != NULL);
        JC_CHECK(strstr(line, "\"session\"") != NULL);
        /* M529: a caller must learn the assignment verbs exist from the
         * handshake, not by trying one and reading an error. */
        JC_CHECK(strstr(line, "\"assignment\"") != NULL);
        JC_CHECK(strstr(line, "1048576") != NULL);           /* limits present */
        /* and NOT a limit nobody enforces */
        JC_CHECK(strstr(line, "maxPromptBytes") == NULL);
        JC_CHECK(strstr(line, "\"uid\":1234") != NULL);      /* resolved id    */
        /* The honest fields: the mode WAS verified, peer credentials are NOT
         * checked. A caller must be able to tell those apart. */
        JC_CHECK(strstr(line, "\"modeVerified\":true") != NULL);
        JC_CHECK(strstr(line, "\"peercred\":false") != NULL);
        JC_CHECK(strstr(line, "\"mechanism\":\"socket-mode\"") != NULL);
        JC_CHECK(strchr(line, '\n') != NULL);
        free(line);
    }

    /* And it must not claim verification it did not do. */
    h.mode_verified = 0;
    line = jc_daemon_build_hello_ok(&h);
    if (JC_REQUIRE(line != NULL)) {
        JC_CHECK(strstr(line, "\"modeVerified\":false") != NULL);
        free(line);
    }
    jc_arena_free(a);
}

void test_daemon(void)
{
    test_hello();
    test_format_three_values();
    test_prompt_roundtrip();
    test_text_default();
    test_ctl_and_errors();
}
