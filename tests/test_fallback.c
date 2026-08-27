/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_fallback.c - the pure model-fallback chain resolver
 * (jc_config_fallback_chain) and the "fallback" config field. The reachability
 * probe (jc_net_reachable) and live substitution are verified end-to-end. */

#include "jc_test.h"
#include "jc_config.h"
#include "jc_net.h"
#include "jc_vec.h"
#include "jc_mem.h"
#include "jc_platform.h"

#include <stdio.h>

static void write_file_str(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fputs(s, f);
        fclose(f);
    }
}

static void test_chain(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config cfg;
    const char *path = jc_test_tmp("jichi_test_fallback.json");
    unsigned char reach[3];
    int out = -1;

    write_file_str(path,
        "{ \"models\": ["
        "{\"provider\":\"openai\",\"name\":\"a\",\"model\":\"m1\","
        "\"fallback\":\"b\"},"
        "{\"provider\":\"openai\",\"name\":\"b\",\"model\":\"m2\","
        "\"fallback\":\"c\"},"
        "{\"provider\":\"openai\",\"name\":\"c\",\"model\":\"m3\"}] }");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(jc_config_model_count(&cfg) == 3);

    /* a,b down; c up => chain a->b->c lands on c (index 2). */
    reach[0] = 0; reach[1] = 0; reach[2] = 1;
    JC_CHECK(jc_config_fallback_chain(&cfg, 0, reach, &out) == 1);
    JC_CHECK(out == 2);

    /* a up => stays on a. */
    reach[0] = 1; reach[1] = 0; reach[2] = 0;
    JC_CHECK(jc_config_fallback_chain(&cfg, 0, reach, &out) == 1);
    JC_CHECK(out == 0);

    /* all down => dead end, returns the original index with 0. */
    reach[0] = 0; reach[1] = 0; reach[2] = 0;
    out = -1;
    JC_CHECK(jc_config_fallback_chain(&cfg, 0, reach, &out) == 0);
    JC_CHECK(out == 0);

    /* a model with no fallback, itself down => dead end at itself. */
    JC_CHECK(jc_config_fallback_chain(&cfg, 2, reach, &out) == 0);
    JC_CHECK(out == 2);

    /* NULL reachability => everything reachable => identity. */
    JC_CHECK(jc_config_fallback_chain(&cfg, 1, NULL, &out) == 1);
    JC_CHECK(out == 1);

    /* parse: the fallback selector round-trips on the model. */
    {
        struct jc_model_cfg *m0 = jc_config_model_at(&cfg, 0);
        if (JC_REQUIRE(m0 != NULL && m0->fallback != NULL)) {
            JC_CHECK_STR(m0->fallback, "b");
        }
        JC_CHECK(jc_config_model_at(&cfg, 2)->fallback == NULL);
    }

    jc_config_free(&cfg);
    remove(path);
    jc_arena_free(a);
}

static void test_cycle(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config cfg;
    const char *path = jc_test_tmp("jichi_test_fallback_cycle.json");
    unsigned char reach[2];
    int out = -1;

    write_file_str(path,
        "{ \"models\": ["
        "{\"provider\":\"openai\",\"name\":\"x\",\"model\":\"mx\","
        "\"fallback\":\"y\"},"
        "{\"provider\":\"openai\",\"name\":\"y\",\"model\":\"my\","
        "\"fallback\":\"x\"}] }");
    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);

    /* x<->y cycle, both down: must terminate (hop-bounded), return 0. */
    reach[0] = 0; reach[1] = 0;
    JC_CHECK(jc_config_fallback_chain(&cfg, 0, reach, &out) == 0);
    JC_CHECK(out == 0);

    /* cycle but y up: resolves to y. */
    reach[0] = 0; reach[1] = 1;
    JC_CHECK(jc_config_fallback_chain(&cfg, 0, reach, &out) == 1);
    JC_CHECK(out == 1);

    jc_config_free(&cfg);
    remove(path);
    jc_arena_free(a);
}

static void test_parse_models(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_vec ids;
    jc_vec_init(&ids, sizeof(char *));

    /* OpenAI-style {"data":[{"id":...}]}. */
    JC_CHECK(jc_net_parse_models(
        "{\"data\":[{\"id\":\"gpt-4o\"},{\"id\":\"gpt-4o-mini\"}]}",
        &ids, a) == JC_OK);
    JC_CHECK(ids.len == 2);
    JC_CHECK_STR(JC_VEC_STR(&ids, 0), "gpt-4o");
    JC_CHECK_STR(JC_VEC_STR(&ids, 1), "gpt-4o-mini");

    /* Bare array of objects, and malformed input. */
    jc_vec_free(&ids);
    jc_vec_init(&ids, sizeof(char *));
    JC_CHECK(jc_net_parse_models("[{\"id\":\"m1\"}]", &ids, a) == JC_OK);
    JC_CHECK(ids.len == 1);
    JC_CHECK(jc_net_parse_models("not json", &ids, a) == JC_ERR_PARSE);

    /* The gateway's published context window (/v1/model/info, a LiteLLM
     * extension). Standard /v1/models says nothing about the window, which is
     * why contextLength has always had to be declared by hand -- and why
     * jc_agent.c carries a warning for the case where it was declared wrong.
     * The cases that matter here are not the happy path but what real servers
     * answer when they do NOT have this endpoint. */
    {
        long in = 0;
        long outv = 0;
        /* The shape measured on api.hrz.uni-giessen.de for jlu/qwen3.8-27b. */
        const char *ok =
            "{\"data\":[{\"model_name\":\"jlu/qwen3.8-27b\","
            "\"model_info\":{\"mode\":\"chat\",\"max_input_tokens\":196608,"
            "\"max_output_tokens\":65536}}]}";
        JC_CHECK(jc_net_parse_model_limits(ok, "jlu/qwen3.8-27b", &in, &outv)
                 == JC_OK);
        JC_CHECK(in == 196608);
        JC_CHECK(outv == 65536);

        /* A well-formed table that does not list the model we asked about. */
        in = outv = 0;
        JC_CHECK(jc_net_parse_model_limits(ok, "jlu/other", &in, &outv)
                 == JC_ERR_NOTFOUND);
        JC_CHECK(in == 0);

        /* Listed, but with no limit filled in. This gateway really does serve
         * "max_input_tokens":null for some entries, and null must read as "not
         * published" rather than as a window of zero. */
        JC_CHECK(jc_net_parse_model_limits(
            "{\"data\":[{\"model_name\":\"m\",\"model_info\":"
            "{\"max_input_tokens\":null}}]}", "m", &in, &outv)
                 == JC_ERR_NOTFOUND);

        /* THE case this function exists to get right: LM Studio answers this
         * endpoint with HTTP **200** and an error object (measured against
         * 134.176.150.160:1234), so a 404 is NOT how you recognise a server
         * without the endpoint -- the SHAPE is. Reported as "not a model
         * table", which doctor renders as "this server does not publish model
         * limits" rather than as a fault in the user's config. */
        JC_CHECK(jc_net_parse_model_limits(
            "{\"error\":\"Unexpected endpoint or method. "
            "(GET /v1/model/info)\"}", "m", &in, &outv) == JC_ERR_PARSE);

        JC_CHECK(jc_net_parse_model_limits("not json", "m", &in, &outv)
                 == JC_ERR_PARSE);
        JC_CHECK(jc_net_parse_model_limits(NULL, "m", &in, &outv)
                 == JC_ERR_INVALID);
        JC_CHECK(jc_net_parse_model_limits("{\"data\":[]}", NULL, &in, &outv)
                 == JC_ERR_INVALID);
        /* Both out-params are optional. */
        JC_CHECK(jc_net_parse_model_limits(ok, "jlu/qwen3.8-27b", NULL, NULL)
                 == JC_OK);
    }

    jc_vec_free(&ids);
    jc_arena_free(a);
}

void test_fallback(void)
{
    test_chain();
    test_cycle();
    test_parse_models();
}
