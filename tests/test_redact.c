/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_redact.c - secret redaction core (M24). */

#include "jc_test.h"
#include "jc_log.h"

#include <string.h>

void test_redact(void)
{
    char out[256];
    const char *secrets[2];
    int hits;

    secrets[0] = "sk-ant-SECRETKEY-1234";
    secrets[1] = "OPENAI-TOKEN-abcdef";

    /* A single occurrence is replaced with "***". */
    hits = jc_redact_apply(secrets, 1,
                           "auth: Bearer sk-ant-SECRETKEY-1234 done", out,
                           sizeof(out));
    JC_CHECK(hits == 1);
    JC_CHECK(strstr(out, "sk-ant-SECRETKEY-1234") == NULL);
    JC_CHECK(strstr(out, "***") != NULL);
    JC_CHECK(strstr(out, "auth: Bearer ") != NULL);
    JC_CHECK(strstr(out, " done") != NULL);

    /* Multiple occurrences + multiple secrets all scrubbed. */
    hits = jc_redact_apply(secrets, 2,
        "a sk-ant-SECRETKEY-1234 b OPENAI-TOKEN-abcdef c sk-ant-SECRETKEY-1234",
        out, sizeof(out));
    JC_CHECK(hits == 3);
    JC_CHECK(strstr(out, "SECRETKEY") == NULL);
    JC_CHECK(strstr(out, "OPENAI-TOKEN") == NULL);

    /* Secrets shorter than JC_REDACT_MIN are ignored (too generic). */
    {
        const char *shortsec[1];
        shortsec[0] = "abc"; /* < JC_REDACT_MIN */
        hits = jc_redact_apply(shortsec, 1, "abc def abc", out, sizeof(out));
        JC_CHECK(hits == 0);
        JC_CHECK_STR(out, "abc def abc");
    }

    /* No secret present: text passes through unchanged. */
    hits = jc_redact_apply(secrets, 2, "nothing to see here", out, sizeof(out));
    JC_CHECK(hits == 0);
    JC_CHECK_STR(out, "nothing to see here");

    /* NULL input yields an empty string, no crash. */
    hits = jc_redact_apply(secrets, 2, NULL, out, sizeof(out));
    JC_CHECK(hits == 0);
    JC_CHECK_STR(out, "");

    /* Output is always NUL-terminated within bounds even under a tight cap
     * (strlen stays inside the buffer => no out-of-bounds / uninitialised read). */
    {
        char tiny[5];
        jc_redact_apply(secrets, 2, "sk-ant-SECRETKEY-1234", tiny,
                        sizeof(tiny));
        JC_CHECK(strlen(tiny) < sizeof(tiny));
    }

    /* The registry wrapper scrubs a registered key. */
    JC_CHECK(jc_redact_active() == 0); /* nothing registered yet */
    jc_redact_register("REGISTERED-SECRET-9999");
    JC_CHECK(jc_redact_active() == 1); /* now a content sink knows to scrub */
    hits = jc_redact_secrets("x REGISTERED-SECRET-9999 y", out, sizeof(out));
    JC_CHECK(hits == 1);
    JC_CHECK(strstr(out, "REGISTERED-SECRET-9999") == NULL);
}
