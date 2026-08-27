/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_retrieve.c - RRF fusion, opts resolution, query-rewrite pure helpers. */

#include "jc_test.h"
#include "jc_retrieve.h"
#include "jc_queryrewrite.h"
#include "jc_config.h"
#include "jc_str.h"

#include <string.h>

void test_retrieve(void)
{
    int a[3];
    int b[3];
    int out[6];
    double sc[6];
    int n;
    struct jc_retrieve_opts opts;

    /* RRF: a doc in both lists outranks docs in only one. */
    a[0] = 1; a[1] = 2; a[2] = 3;
    b[0] = 2; b[1] = 4; b[2] = 5;        /* doc 2 is in both */
    n = jc_rrf_fuse(a, 3, b, 3, 60, out, sc, 6);
    JC_CHECK(n == 5);                    /* union {1,2,3,4,5} */
    JC_CHECK(out[0] == 2);               /* highest fused score */
    JC_CHECK(sc[0] >= sc[1]);            /* descending */

    /* cap truncates after sorting (the best survive). */
    n = jc_rrf_fuse(a, 3, b, 3, 60, out, sc, 2);
    JC_CHECK(n == 2);
    JC_CHECK(out[0] == 2);

    /* Duplicates within one list fold (no double-count, no extra slot). */
    a[0] = 7; a[1] = 7; a[2] = 8;
    n = jc_rrf_fuse(a, 3, NULL, 0, 60, out, sc, 6);
    JC_CHECK(n == 2);

    /* Guards. */
    JC_CHECK(jc_rrf_fuse(NULL, 0, NULL, 0, 60, out, sc, 6) == 0);
    JC_CHECK(jc_rrf_fuse(a, 3, NULL, 0, 60, out, sc, 0) == 0);

    /* opts defaults. */
    jc_retrieve_opts_default(&opts, 9);
    JC_CHECK(opts.top_k == 9);
    JC_CHECK(opts.hybrid == 1);
    JC_CHECK(opts.rrf_k == 60);
    jc_retrieve_opts_default(&opts, 0);
    JC_CHECK(opts.top_k == 5);

    /* opts from config: hybrid tri-state (-1 auto => on, 0 off) + rrf_k. */
    {
        struct jc_config c;
        memset(&c, 0, sizeof(c));
        c.retrieval.hybrid = 0;
        c.retrieval.rrf_k = 0;
        jc_retrieve_opts_from_config(&c, 5, &opts);
        JC_CHECK(opts.hybrid == 0);
        JC_CHECK(opts.rrf_k == 60);      /* 0 => built-in */
        c.retrieval.hybrid = -1;
        c.retrieval.rrf_k = 30;
        jc_retrieve_opts_from_config(&c, 5, &opts);
        JC_CHECK(opts.hybrid == 1);
        JC_CHECK(opts.rrf_k == 30);
    }

    /* Query-rewrite prompt builder embeds the raw query. */
    {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_queryrewrite_prompt("parse a json array", JC_QR_HYDE, &sb);
        JC_CHECK(sb.data != NULL && strstr(sb.data, "parse a json array") != NULL);
        JC_CHECK(strstr(sb.data, "passage") != NULL);
        jc_sb_free(&sb);

        jc_sb_init(&sb);
        jc_queryrewrite_prompt("x", JC_QR_MULTIQUERY, &sb);
        JC_CHECK(sb.data != NULL && strstr(sb.data, "phrasings") != NULL);
        jc_sb_free(&sb);
    }

    /* Query-rewrite cleaner: drop fences + list markers, join with spaces. */
    {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_queryrewrite_clean("```\n- foo bar\n1. baz\n```\nhello", &sb);
        JC_CHECK_STR(sb.data, "foo bar baz hello");
        jc_sb_free(&sb);
    }
}
