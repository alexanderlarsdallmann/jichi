/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_toolprobe.c - the M167 doctor --live classifier (pure core). */

#include "jc_test.h"
#include "jc_toolprobe.h"

#include <string.h>

static void test_classify(void)
{
    /* A native call NAMING THE PROBE TOOL is the only "native" verdict. */
    JC_CHECK(jc_toolprobe_classify(1, JC_TOOLPROBE_TOOL, NULL) ==
             JC_TOOLPROBE_NATIVE);
    JC_CHECK(jc_toolprobe_classify(1, JC_TOOLPROBE_TOOL, "here you go") ==
             JC_TOOLPROBE_NATIVE);

    /* A call to something ELSE did not follow the instruction. Scoring that as
     * working would be the hollow-gate mistake: the probe would pass while the
     * model ignored the only tool it was given. */
    JC_CHECK(jc_toolprobe_classify(1, "read_file", NULL) == JC_TOOLPROBE_NONE);

    /* Described but not invoked -- the M147 prose shape. Naming the probe tool
     * is high-precision: the name is namespaced and appears nowhere else. */
    JC_CHECK(jc_toolprobe_classify(0, NULL,
        "I'll call jichi_probe_echo with text \"ping\".") == JC_TOOLPROBE_TEXT);
    JC_CHECK(jc_toolprobe_classify(0, NULL,
        "```json\n{\"name\": \"JICHI_PROBE_ECHO\"}\n```") == JC_TOOLPROBE_TEXT);

    /* Nothing useful at all -- the M166 signature. */
    JC_CHECK(jc_toolprobe_classify(0, NULL, NULL) == JC_TOOLPROBE_NONE);
    JC_CHECK(jc_toolprobe_classify(0, NULL, "") == JC_TOOLPROBE_NONE);
    JC_CHECK(jc_toolprobe_classify(0, NULL, "Sure, how can I help?") ==
             JC_TOOLPROBE_NONE);
    /* A NULL call_name with ncalls>0 must not be read as native. */
    JC_CHECK(jc_toolprobe_classify(2, NULL, NULL) == JC_TOOLPROBE_NONE);

    JC_CHECK(strcmp(jc_toolprobe_verdict_str(JC_TOOLPROBE_NATIVE),
                    "native") == 0);
    JC_CHECK(strcmp(jc_toolprobe_verdict_str(JC_TOOLPROBE_TEXT), "text") == 0);
    JC_CHECK(strcmp(jc_toolprobe_verdict_str(JC_TOOLPROBE_NONE), "none") == 0);

    /* There is no "text" setting today (the protocol is unbuilt), so a TEXT
     * observation maps to the "none" handling the M147 nudge already covers. */
    JC_CHECK(strcmp(jc_toolprobe_suggested_setting(JC_TOOLPROBE_NATIVE),
                    "native") == 0);
    JC_CHECK(strcmp(jc_toolprobe_suggested_setting(JC_TOOLPROBE_TEXT),
                    "none") == 0);
    JC_CHECK(strcmp(jc_toolprobe_suggested_setting(JC_TOOLPROBE_NONE),
                    "none") == 0);
}

/* The advice must never tell an operator to degrade a capable model in order to
 * work around a bug in jichi. Before M166 was found, the existing warning said
 * only "the model may lack native tool-call support" -- following that would
 * have set toolCalling:"none" on a model that worked perfectly. */
static void test_advice_ordering(void)
{
    const char *a;

    a = jc_toolprobe_advice(JC_TOOLPROBE_NONE, "native");
    JC_CHECK(a != NULL);
    /* Names the request as the first suspect... */
    JC_CHECK(strstr(a, "request") != NULL);
    /* ...tells the operator how to check it... */
    JC_CHECK(strstr(a, "replay") != NULL);
    /* ...and explicitly warns against the tempting wrong fix. */
    JC_CHECK(strstr(a, "hide a bug") != NULL);

    /* An unset toolCalling defaults to native, so it gets the same advice. */
    JC_CHECK(strcmp(jc_toolprobe_advice(JC_TOOLPROBE_NONE, NULL), a) == 0);
    JC_CHECK(strcmp(jc_toolprobe_advice(JC_TOOLPROBE_NONE, ""), a) == 0);

    /* Configured "none" and observed none is expected, not alarming. */
    a = jc_toolprobe_advice(JC_TOOLPROBE_NONE, "none");
    JC_CHECK(strstr(a, "as expected") != NULL);

    /* Configured "none" but the model DOES call natively: say so, so the
     * operator can lift a needless override. */
    a = jc_toolprobe_advice(JC_TOOLPROBE_NATIVE, "none");
    JC_CHECK(strstr(a, "DOES call tools natively") != NULL);

    a = jc_toolprobe_advice(JC_TOOLPROBE_NATIVE, "native");
    JC_CHECK(strstr(a, "confirmed") != NULL);

    /* Text with native configured: mentions the nudge and still puts checking
     * the request ahead of changing the setting. */
    a = jc_toolprobe_advice(JC_TOOLPROBE_TEXT, "native");
    JC_CHECK(strstr(a, "nudge") != NULL);
    JC_CHECK(strstr(a, "well-formed") != NULL);
}

/* Only configured-native + observed-none is a doctor FAIL: it means the agent
 * loop cannot work at all. The softer mismatches are warnings. */
static void test_failure_gate(void)
{
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_NONE, "native") == 1);
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_NONE, NULL) == 1);
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_NONE, "") == 1);
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_NONE, "none") == 0);
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_TEXT, "native") == 0);
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_NATIVE, "native") == 0);
    JC_CHECK(jc_toolprobe_is_failure(JC_TOOLPROBE_NATIVE, "none") == 0);
}

void test_toolprobe(void)
{
    test_classify();
    test_advice_ordering();
    test_failure_gate();
}
