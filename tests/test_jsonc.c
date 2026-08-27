/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_jsonc.c - JSONC stripping + source-format detection. */

#include "jc_test.h"
#include "jc_jsonc.h"
#include "jc_convert.h"
#include "jc_json.h"
#include "jc_mem.h"
#include <stdlib.h>
#include <string.h>

static void test_jsonc_line_comment(void)
{
    struct jc_arena *a = jc_arena_new(0);
    const char *src =
        "{\n"
        "  // a leading comment\n"
        "  \"a\": 1, // trailing comment\n"
        "  \"b\": 2\n"
        "}\n";
    char *out = jc_jsonc_strip(src, a);
    cJSON *root = jc_json_parse(out);

    JC_CHECK(root != NULL);
    JC_CHECK(jc_json_get_num(root, "a", -1.0) == 1.0);
    JC_CHECK(jc_json_get_num(root, "b", -1.0) == 2.0);

    cJSON_Delete(root);
    jc_arena_free(a);
}

static void test_jsonc_block_comment(void)
{
    struct jc_arena *a = jc_arena_new(0);
    const char *src =
        "{ /* block\n comment */ \"x\": /* inline */ 42 }";
    char *out = jc_jsonc_strip(src, a);
    cJSON *root = jc_json_parse(out);

    JC_CHECK(root != NULL);
    JC_CHECK(jc_json_get_num(root, "x", -1.0) == 42.0);

    cJSON_Delete(root);
    jc_arena_free(a);
}

static void test_jsonc_comment_inside_string_preserved(void)
{
    struct jc_arena *a = jc_arena_new(0);
    const char *src =
        "{ \"url\": \"https://x/y\", \"note\": \"a // b /* c */ , d\" }";
    char *out = jc_jsonc_strip(src, a);
    cJSON *root = jc_json_parse(out);

    JC_CHECK(root != NULL);
    JC_CHECK_STR(jc_json_get_str(root, "url", "?"), "https://x/y");
    JC_CHECK_STR(jc_json_get_str(root, "note", "?"), "a // b /* c */ , d");

    cJSON_Delete(root);
    jc_arena_free(a);
}

static void test_jsonc_trailing_commas(void)
{
    struct jc_arena *a = jc_arena_new(0);
    const char *src =
        "{ \"list\": [1, 2, 3,], \"obj\": { \"k\": \"v\", }, }";
    char *out = jc_jsonc_strip(src, a);
    cJSON *root = jc_json_parse(out);
    cJSON *list;

    JC_CHECK(root != NULL);
    list = jc_json_get_obj(root, "list");
    JC_CHECK(cJSON_GetArraySize(list) == 3);
    JC_CHECK_STR(jc_json_get_str(jc_json_get_obj(root, "obj"), "k", "?"), "v");

    cJSON_Delete(root);
    jc_arena_free(a);
}

static void test_jsonc_idempotent_on_clean(void)
{
    struct jc_arena *a = jc_arena_new(0);
    const char *src = "{\"a\":1,\"b\":[2,3]}";
    char *out = jc_jsonc_strip(src, a);
    /* A trailing comma inside a string that looks like ",]" must survive. */
    const char *tricky = "{\"s\":\"x,]\"}";
    char *out2 = jc_jsonc_strip(tricky, a);

    JC_CHECK_STR(out, src);
    JC_CHECK_STR(out2, tricky);

    jc_arena_free(a);
}

static void test_detect_formats(void)
{
    struct jc_arena *a = jc_arena_new(0);
    const char *yaml = "name: x\nversion: 1\nmodels:\n  - model: m\n";
    const char *legacy = "{ \"models\": [ { \"title\": \"t\" } ] }";
    const char *opencode_schema =
        "{ \"$schema\": \"https://opencode.ai/config.json\", \"model\": "
        "\"anthropic/claude\" }";
    const char *opencode_provider =
        "{ // opencode\n \"provider\": { \"anthropic\": {} }, }";
    const char *opencode_model = "{ \"model\": \"openai/gpt-4o\" }";

    JC_CHECK(jc_convert_detect("config.yaml", yaml, a) == JC_SRC_CONTINUE_YAML);
    JC_CHECK(jc_convert_detect(NULL, yaml, a) == JC_SRC_CONTINUE_YAML);
    JC_CHECK(jc_convert_detect("config.json", legacy, a) ==
             JC_SRC_CONTINUE_JSON);
    JC_CHECK(jc_convert_detect(NULL, legacy, a) == JC_SRC_CONTINUE_JSON);
    JC_CHECK(jc_convert_detect("opencode.json", opencode_schema, a) ==
             JC_SRC_OPENCODE);
    JC_CHECK(jc_convert_detect("opencode.jsonc", opencode_provider, a) ==
             JC_SRC_OPENCODE);
    JC_CHECK(jc_convert_detect(NULL, opencode_model, a) == JC_SRC_OPENCODE);

    jc_arena_free(a);
}

void test_jsonc(void)
{
    test_jsonc_line_comment();
    test_jsonc_block_comment();
    test_jsonc_comment_inside_string_preserved();
    test_jsonc_trailing_commas();
    test_jsonc_idempotent_on_clean();
    test_detect_formats();
}
