/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_embed.c - offline parser tests for the embeddings/rerank clients.
 * These exercise only the response parsers; no sockets are opened. */

#include "jc_test.h"
#include "jc_embed.h"
#include "jc_rerank.h"
#include <stdlib.h>

static void test_embed_parse(void)
{
    /* Two 3-dim vectors, returned out of order (index 1 before index 0). */
    const char *json =
        "{ \"data\": ["
        "  { \"index\": 1, \"embedding\": [1.0, 1.5, 2.0] },"
        "  { \"index\": 0, \"embedding\": [0.0, 0.5, 1.0] } ],"
        "  \"usage\": { \"prompt_tokens\": 4 } }";
    float *v = NULL;
    int dim = 0;

    JC_CHECK(jc_embed_parse(json, 2, &v, &dim) == JC_OK);
    JC_CHECK(dim == 3);
    if (v != NULL) {
        /* Row 0 (index 0) must be the second item's embedding. */
        JC_CHECK(v[0] == 0.0f && v[1] == 0.5f && v[2] == 1.0f);
        JC_CHECK(v[3] == 1.0f && v[4] == 1.5f && v[5] == 2.0f);
    }
    free(v);

    /* Wrong count is rejected. */
    v = NULL;
    JC_CHECK(jc_embed_parse(json, 3, &v, &dim) == JC_ERR_PARSE);
    JC_CHECK(v == NULL);
}

static void test_rerank_parse_data(void)
{
    /* OpenAI/vLLM shape: "data" + "relevance_score". */
    const char *json =
        "{ \"object\": \"list\", \"data\": ["
        "  { \"index\": 0, \"relevance_score\": 0.20 },"
        "  { \"index\": 2, \"relevance_score\": 0.90 },"
        "  { \"index\": 1, \"relevance_score\": 0.50 } ] }";
    double s[3];

    JC_CHECK(jc_rerank_parse(json, 3, s) == JC_OK);
    JC_CHECK_NEAR(s[0], 0.20);
    JC_CHECK(s[1] == 0.50);
    JC_CHECK_NEAR(s[2], 0.90);
}

static void test_rerank_parse_results(void)
{
    /* Cohere shape: "results" + "relevance_score"; also test "score" fallback. */
    const char *json =
        "{ \"results\": ["
        "  { \"index\": 1, \"score\": 0.75 },"
        "  { \"index\": 0, \"relevance_score\": 0.10 } ] }";
    double s[2];

    JC_CHECK(jc_rerank_parse(json, 2, s) == JC_OK);
    JC_CHECK_NEAR(s[0], 0.10);
    JC_CHECK(s[1] == 0.75);

    /* No recognised array => parse error, scores zeroed. */
    JC_CHECK(jc_rerank_parse("{ \"nope\": 1 }", 2, s) == JC_ERR_PARSE);
    JC_CHECK(s[0] == 0.0 && s[1] == 0.0);
}

void test_embed(void)
{
    test_embed_parse();
    test_rerank_parse_data();
    test_rerank_parse_results();
}
