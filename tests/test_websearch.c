/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_websearch.c - web_search result parser (M27). Pure, offline. */

#include "jc_test.h"
#include "jc_tool.h"
#include "jc_str.h"

#include <string.h>

void test_websearch(void)
{
    struct jc_sb sb;

    /* Tavily-style: results[] with title/url/content. */
    jc_sb_init(&sb);
    {
        const char *json =
            "{\"results\":["
            "{\"title\":\"First\",\"url\":\"http://a\",\"content\":\"alpha\"},"
            "{\"title\":\"Second\",\"url\":\"http://b\",\"content\":\"beta\"}"
            "]}";
        int n = jc_websearch_format(json, 5, &sb);
        JC_CHECK(n == 2);
        JC_CHECK(strstr(sb.data, "First") != NULL);
        JC_CHECK(strstr(sb.data, "http://a") != NULL);
        JC_CHECK(strstr(sb.data, "alpha") != NULL);
        JC_CHECK(strstr(sb.data, "Second") != NULL);
    }
    jc_sb_free(&sb);

    /* SerpAPI-style: data[] with name/link/snippet (alternate keys). */
    jc_sb_init(&sb);
    {
        const char *json =
            "{\"data\":[{\"name\":\"Doc\",\"link\":\"http://x\","
            "\"snippet\":\"gamma\"}]}";
        int n = jc_websearch_format(json, 5, &sb);
        JC_CHECK(n == 1);
        JC_CHECK(strstr(sb.data, "Doc") != NULL);
        JC_CHECK(strstr(sb.data, "http://x") != NULL);
        JC_CHECK(strstr(sb.data, "gamma") != NULL);
    }
    jc_sb_free(&sb);

    /* max caps the number rendered. */
    jc_sb_init(&sb);
    {
        const char *json =
            "{\"results\":[{\"title\":\"a\"},{\"title\":\"b\"},"
            "{\"title\":\"c\"}]}";
        int n = jc_websearch_format(json, 2, &sb);
        JC_CHECK(n == 2);
        JC_CHECK(strstr(sb.data, "c\n") == NULL); /* third not rendered */
    }
    jc_sb_free(&sb);

    /* Empty results array -> 0 + "(no results)". */
    jc_sb_init(&sb);
    {
        int n = jc_websearch_format("{\"results\":[]}", 5, &sb);
        JC_CHECK(n == 0);
        JC_CHECK(strstr(sb.data, "no results") != NULL);
    }
    jc_sb_free(&sb);

    /* Malformed / no results array -> -1. */
    jc_sb_init(&sb);
    JC_CHECK(jc_websearch_format("not json", 5, &sb) == -1);
    JC_CHECK(jc_websearch_format("{\"x\":1}", 5, &sb) == -1);
    JC_CHECK(jc_websearch_format(NULL, 5, &sb) == -1);
    jc_sb_free(&sb);
}
