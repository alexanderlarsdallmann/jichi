/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_neterr.c - the media-failure message names a way forward (M500).
 *
 * The defect these pin: `generate_audio` reported one sentence -- "audio
 * generation request failed" -- for a 500, a 401 and an unreachable host alike.
 * Measured against a gateway answering HTTP 500, a model retried three argument
 * variations (428 output tokens, 27 s) and then advised fixing a correct API
 * key. The status class is the only thing that separates "retry differently"
 * from "stop, it is the server". */
#include "jc_test.h"
#include "jc_neterr.h"

#include <string.h>

static int has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

static void test_server_failure_says_stop(void)
{
    char b[300];
    jc_neterr_render(b, sizeof b, "audio generation", 500, "/audio/speech");
    JC_CHECK(has(b, "HTTP 500"));
    JC_CHECK(has(b, "audio generation"));
    JC_CHECK(has(b, "/audio/speech"));
    /* The load-bearing half: a 5xx must tell the reader the ARGUMENTS are not
     * the problem, or it retries them. */
    JC_CHECK(has(b, "SERVER failure"));
    JC_CHECK(has(b, "will not change it"));
}

static void test_classes_differ(void)
{
    char a[300], b[300], c[300], d[300], e[300];
    jc_neterr_render(a, sizeof a, "transcription", 401, "/x");
    jc_neterr_render(b, sizeof b, "transcription", 404, "/x");
    jc_neterr_render(c, sizeof c, "transcription", 429, "/x");
    jc_neterr_render(d, sizeof d, "transcription", 400, "/x");
    jc_neterr_render(e, sizeof e, "transcription", 503, "/x");
    JC_CHECK(has(a, "API key"));
    JC_CHECK(has(b, "apiBase"));
    JC_CHECK(has(c, "rate-limiting"));
    JC_CHECK(has(d, "arguments"));
    JC_CHECK(has(e, "SERVER failure"));
    /* Five distinct actions, not five spellings of one: a message that says the
     * same thing for every status is the defect this file exists for. */
    JC_CHECK(strcmp(a, b) != 0);
    JC_CHECK(strcmp(b, c) != 0);
    JC_CHECK(strcmp(c, d) != 0);
    JC_CHECK(strcmp(d, e) != 0);
    JC_CHECK(strcmp(a, e) != 0);
}

static void test_no_response_is_its_own_case(void)
{
    char b[300];
    /* 0 means the transport never got a reply -- a wrong apiBase, DNS, TLS.
     * Reporting that as "HTTP 0" would be a lie about what happened. */
    jc_neterr_render(b, sizeof b, "audio generation", 0, "/audio/speech");
    JC_CHECK(has(b, "no response"));
    JC_CHECK(!has(b, "HTTP 0"));
    JC_CHECK(has(b, "endpoint"));
}

static void test_bounds_and_nulls(void)
{
    char tiny[8];
    JC_CHECK(jc_neterr_render(NULL, 100, "x", 500, "/y") == NULL);
    /* cap 0 must not write; a caller passing a zero cap gets its buffer back
     * untouched rather than a one-byte overrun. */
    tiny[0] = 'Z';
    jc_neterr_render(tiny, 0, "x", 500, "/y");
    JC_CHECK(tiny[0] == 'Z');
    jc_neterr_render(tiny, sizeof tiny, "x", 500, "/y");
    JC_CHECK(strlen(tiny) < sizeof tiny);
    /* A NULL endpoint is legal (some callers have no path to name) and a NULL
     * operation falls back rather than printing "(null)". */
    {
        char b[300];
        jc_neterr_render(b, sizeof b, "audio generation", 500, NULL);
        JC_CHECK(has(b, "HTTP 500"));
        jc_neterr_render(b, sizeof b, NULL, 500, "/z");
        JC_CHECK(has(b, "the request"));
    }
}

void test_neterr(void)
{
    test_server_failure_says_stop();
    test_classes_differ();
    test_no_response_is_its_own_case();
    test_bounds_and_nulls();
}
