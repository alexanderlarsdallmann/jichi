/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_cacheaudit.c - prompt-cache audit (M104). */

#include "jc_test.h"
#include "jc_cacheaudit.h"
#include "jc_telemetry.h"
#include "jc_str.h"
#include "jc_vec.h"
#include <string.h>

static void test_hitrate(void)
{
    JC_CHECK(jc_cacheaudit_hitrate(0.0, 100000.0) == 0);
    JC_CHECK(jc_cacheaudit_hitrate(50.0, 50.0) == 50);
    JC_CHECK(jc_cacheaudit_hitrate(90.0, 10.0) == 90);
    JC_CHECK(jc_cacheaudit_hitrate(0.0, 0.0) == -1); /* no volume */
}

static void test_verdict(void)
{
    /* Low volume => no data, regardless of rate. */
    JC_CHECK(jc_cacheaudit_verdict(-1, 0.0) == JC_CACHE_NODATA);
    JC_CHECK(jc_cacheaudit_verdict(0, 500.0) == JC_CACHE_NODATA);
    /* Real volume, no reuse => NONE. */
    JC_CHECK(jc_cacheaudit_verdict(0, 100000.0) == JC_CACHE_NONE);
    JC_CHECK(jc_cacheaudit_verdict(30, 100000.0) == JC_CACHE_PARTIAL);
    JC_CHECK(jc_cacheaudit_verdict(80, 100000.0) == JC_CACHE_GOOD);
    JC_CHECK_STR(jc_cacheaudit_verdict_name(JC_CACHE_NONE), "no cache reported");
}

static void test_render_rebilling(void)
{
    struct jc_telemetry_summary s;
    struct jc_telem_model m;
    struct jc_sb out;

    jc_telemetry_summary_init(&s);
    memset(&m, 0, sizeof(m));
    strcpy(m.name, "jlu/gemma");
    m.calls = 10;
    m.in_tok = 120000.0;   /* all uncached */
    m.cache_read = 0.0;
    jc_vec_push(&s.models, &m);

    jc_sb_init(&out);
    jc_cacheaudit_render(&s, &out);
    /* A zero-reads backend is diagnosed as "no cache reported" -- NOT as
     * "not caching": a server can cache without reporting it (M378; measured
     * 18.6x repeat-prefill latency with nothing on the wire), so the audit
     * may only assert what the wire said, and its advice must point at the
     * latency probe before recommending prefix surgery. */
    JC_CHECK(strstr(out.data, "Prompt-cache audit") != NULL);
    JC_CHECK(strstr(out.data, "no cache reported") != NULL);
    JC_CHECK(strstr(out.data, "re-bills") != NULL);
    JC_CHECK(strstr(out.data, "cache_probe.py") != NULL);
    JC_CHECK(strstr(out.data, "without reporting") != NULL);

    jc_sb_free(&out);
    jc_telemetry_summary_free(&s);
}

void test_cacheaudit(void)
{
    test_hitrate();
    test_verdict();
    test_render_rebilling();
}
