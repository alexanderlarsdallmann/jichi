/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_embed.c - offline parser tests for the embeddings/rerank clients.
 * These exercise only the response parsers; no sockets are opened. */

#include "jc_test.h"
#include "jc_embed.h"
#include "jc_rerank.h"
#include "jc_str.h"
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

static char *join_chunks(const char *const *chunks)
{
    struct jc_sb sb;
    int i;
    jc_sb_init(&sb);
    for (i = 0; chunks[i] != NULL; i++) {
        jc_sb_append(&sb, chunks[i]);
    }
    return jc_sb_finish(&sb);
}

/* M725: the parser on a real reply, and the two defects it had. */
static void test_embed_parse_recorded(void)
{
    /* Recorded 2026-09-24 from LM Studio, text-embedding-nomic-embed-text-v1.5:
     * formatting, key order and values verbatim, cut to 12 of its 768
     * dimensions (C89 caps a string literal at 509 bytes). Its item carries
     * "object": "embedding" -- the key's name, as a VALUE. */
    static const char *const recorded[] = {
        "{\n  \"object\": \"list\",\n  \"data\": [\n    {\n      \"object\": \"embedding\",\n      \"embedding\": [\n        -0.015835333615541458,\n        0.025248516350984573,\n        -0.14677849411964417,\n        -0.001659492845647037,\n        0.03665942698717117,\n        -0.03420616686344147,\n        0.02749898098409176,\n        -0.000491093669552356,\n        0.00338152726180",
        "85146,\n        0.009787906892597675,\n        -0.08366472274065018,\n        0.016268106177449226\n      ],\n      \"index\": 0\n    }\n  ],\n  \"model\": \"text-embedding-nomic-embed-text-v1.5\",\n  \"usage\": {\n    \"prompt_tokens\": 0,\n    \"total_tokens\": 0\n  }\n}",
        NULL
    };
    char *rec = join_chunks(recorded);
    float *v = NULL;
    int dim = 0;

    JC_CHECK(rec != NULL && jc_embed_parse(rec, 1, &v, &dim) == JC_OK);
    JC_CHECK(dim == 12);
    if (v != NULL) {
        JC_CHECK(v[0] == (float)-0.015835333615541458);
        JC_CHECK(v[11] == (float)0.016268106177449226);
    }
    free(v);
    free(rec);

    /* No "index" at all: encounter order. */
    v = NULL;
    JC_CHECK(jc_embed_parse("{\"data\":[{\"embedding\":[1,2]},{\"embedding\":[3,4]}]}",
                            2, &v, &dim) == JC_OK && dim == 2);
    if (v != NULL) {
        JC_CHECK(v[0] == 1.0f && v[3] == 4.0f);
    }
    free(v);

    /* A duplicate "index" wrote one row twice and returned the other
     * uninitialized -- whatever malloc held. Refused now. */
    v = NULL;
    JC_CHECK(jc_embed_parse("{\"data\":[{\"index\":0,\"embedding\":[1]},"
                            "{\"index\":0,\"embedding\":[2]}]}", 2, &v, &dim)
             == JC_ERR_PARSE && v == NULL);
    /* "index":1e300 reached an undefined (int) cast; it is out of range now. */
    JC_CHECK(jc_embed_parse("{\"data\":[{\"index\":1e300,\"embedding\":[1]}]}",
                            1, &v, &dim) == JC_ERR_PARSE && v == NULL);
    /* Ragged dimensions are refused, as before. */
    JC_CHECK(jc_embed_parse("{\"data\":[{\"embedding\":[1,2]},{\"embedding\":[3]}]}",
                            2, &v, &dim) == JC_ERR_PARSE && v == NULL);
}

void test_embed(void)
{
    test_embed_parse();
    test_embed_parse_recorded();
    test_rerank_parse_data();
    test_rerank_parse_results();
}
