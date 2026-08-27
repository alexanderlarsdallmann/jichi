/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_promptcache.c - pure prompt-cache breakpoint placement (M31b). */

#include "jc_test.h"
#include "jc_promptcache.h"

static void test_min_tokens(void)
{
    /* M340: the published minimum cacheable block, matched on the model id --
     * the only thing jichi has to go on. */
    /* M341: MEASURED against the wire, not taken from documentation -- the
     * published 1024/2048 figures are wrong for this generation and made M340's
     * check emit a false OK through the very failure it was built for. */
    JC_CHECK(jc_promptcache_min_tokens("claude-haiku-4-5") == 4096);
    JC_CHECK(jc_promptcache_min_tokens("claude-opus-4-5") == 4096);
    JC_CHECK(jc_promptcache_min_tokens("claude-sonnet-4-5") == 1024);

    /* Substring, not prefix: a proxy prepends its own namespace, and that is
     * the shape this was actually found on. */
    JC_CHECK(jc_promptcache_min_tokens("anthropic/claude-haiku-4-5") == 4096);
    JC_CHECK(jc_promptcache_min_tokens("anthropic/claude-sonnet-4-5") == 1024);

    /* The SPECIFIC match wins when an id names both: sonnet is genuinely 1024,
     * and reporting 4096 there would warn about a prefix that caches fine. */
    JC_CHECK(jc_promptcache_min_tokens("sonnet-haiku-mix") == 1024);

    /* Unrecognised returns 0, which means "warn about nothing". A false "this
     * will not cache" would have an operator delete a prefix that was working. */
    JC_CHECK(jc_promptcache_min_tokens("jlu/gemma-4-31b-it") == 0);
    JC_CHECK(jc_promptcache_min_tokens("gpt-4o") == 0);
    JC_CHECK(jc_promptcache_min_tokens("") == 0);
    JC_CHECK(jc_promptcache_min_tokens(NULL) == 0);
}

void test_promptcache(void)
{
    struct jc_promptcache_plan p;

    test_min_tokens();

    /* Disabled: empty plan regardless of message count. */
    jc_promptcache_plan(0, 5, &p);
    JC_CHECK(p.cache_system == 0);
    JC_CHECK(p.msg_index == -1);

    /* Enabled with messages: system cached + breakpoint on the last message. */
    jc_promptcache_plan(1, 5, &p);
    JC_CHECK(p.cache_system == 1);
    JC_CHECK(p.msg_index == 4);

    /* Enabled, single message: tail is index 0. */
    jc_promptcache_plan(1, 1, &p);
    JC_CHECK(p.cache_system == 1);
    JC_CHECK(p.msg_index == 0);

    /* Enabled, no messages: still cache the system block, no message breakpoint. */
    jc_promptcache_plan(1, 0, &p);
    JC_CHECK(p.cache_system == 1);
    JC_CHECK(p.msg_index == -1);

    /* Total breakpoints never exceed the API cap. */
    JC_CHECK(JC_PROMPTCACHE_MAX >= 2);
    jc_promptcache_plan(1, 100, &p);
    JC_CHECK(p.cache_system + (p.msg_index >= 0 ? 1 : 0) <= JC_PROMPTCACHE_MAX);
}
