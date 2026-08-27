/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_json.c - exercises the in-tree cJSON-API implementation and the jc_json
 * wrapper. NOT vendored: src/json/cJSON.{c,h} is original code (M171). */

#include <limits.h>

#include "jc_test.h"
#include "jc_json.h"
#include <stdlib.h>
#include <string.h>

void test_json(void)
{
    const char *src =
        "{ \"name\": \"jichi\", \"count\": 7, \"ok\": true, "
        "\"nested\": { \"k\": \"v\" }, "
        "\"list\": [1, 2, 3], "
        "\"esc\": \"a\\nb\\t\\\"c\\\"\" }";
    cJSON *root;
    cJSON *list;
    cJSON *nested;
    char *printed;
    cJSON *reparse;

    root = jc_json_parse(src);
    JC_CHECK(root != NULL);
    if (root == NULL) {
        return;
    }

    JC_CHECK_STR(jc_json_get_str(root, "name", "?"), "jichi");
    JC_CHECK(jc_json_get_num(root, "count", -1.0) == 7.0);
    JC_CHECK(jc_json_get_bool(root, "ok", 0) == 1);
    JC_CHECK(jc_json_get_num(root, "missing", 99.0) == 99.0);

    nested = jc_json_get_obj(root, "nested");
    JC_CHECK(nested != NULL);
    JC_CHECK_STR(jc_json_get_str(nested, "k", "?"), "v");

    list = jc_json_get_obj(root, "list");
    JC_CHECK(cJSON_IsArray(list));
    JC_CHECK(cJSON_GetArraySize(list) == 3);
    JC_CHECK(cJSON_GetArrayItem(list, 1)->valuedouble == 2.0);

    /* Escape decoding. */
    JC_CHECK_STR(jc_json_get_str(root, "esc", "?"), "a\nb\t\"c\"");

    /* Round-trip: print then reparse and re-read a field. */
    printed = jc_json_print(root);
    JC_CHECK(printed != NULL);
    reparse = jc_json_parse(printed);
    JC_CHECK(reparse != NULL);
    JC_CHECK_STR(jc_json_get_str(reparse, "name", "?"), "jichi");
    free(printed);
    cJSON_Delete(reparse);

    cJSON_Delete(root);

    /* Build a document programmatically. */
    {
        cJSON *o = cJSON_CreateObject();
        cJSON *arr;
        char *s;
        cJSON_AddStringToObject(o, "role", "user");
        cJSON_AddNumberToObject(o, "n", 3);
        cJSON_AddBoolToObject(o, "flag", 1);
        arr = cJSON_AddArrayToObject(o, "items");
        cJSON_AddItemToArray(arr, cJSON_CreateString("x"));
        cJSON_AddItemToArray(arr, cJSON_CreateString("y"));
        s = cJSON_PrintUnformatted(o);
        JC_CHECK(s != NULL);
        JC_CHECK(strstr(s, "\"role\":\"user\"") != NULL);
        JC_CHECK(strstr(s, "\"items\":[\"x\",\"y\"]") != NULL);
        free(s);
        cJSON_Delete(o);
    }

    /* Malformed input returns NULL, not a crash. */
    JC_CHECK(jc_json_parse("{ broken") == NULL);
    JC_CHECK(jc_json_parse("[1,2,") == NULL);
}

/* A JSON number outside int's range must CLAMP, not cast.
 *
 * `(int)val` for val outside int is undefined behaviour, and jichi parses JSON
 * it did not write -- model responses, fetched pages, MCP results. Found by the
 * M469 architecture sweep on armeb (zig cc traps UB by default, so it aborted
 * mid-suite) and reproduced on x86-64 under `clang -fsanitize=undefined`. The
 * unit corpus had never fed the parser such a number, which is why make ci's
 * sanitizer pass had never seen it.
 *
 * Without the clamp this test FAILS rather than merely warning: on x86-64 the
 * undefined cvttsd2si yields INT_MIN for every out-of-range value, so 1e300
 * reads as -2147483648 where INT_MAX is required. */
void test_json_number_range(void)
{
    cJSON *root;
    cJSON *x;

    root = jc_json_parse("{\"big\":1e300,\"small\":-1e300,\"huge\":99999999999999999999}");
    if (JC_REQUIRE(root != NULL)) {
        x = cJSON_GetObjectItem(root, "big");
        if (JC_REQUIRE(x != NULL)) {
            JC_CHECK(x->valueint == INT_MAX);
            JC_CHECK(x->valuedouble > 1e299);   /* the double is kept intact */
        }
        x = cJSON_GetObjectItem(root, "small");
        if (JC_REQUIRE(x != NULL))
            JC_CHECK(x->valueint == INT_MIN);
        x = cJSON_GetObjectItem(root, "huge");
        if (JC_REQUIRE(x != NULL))
            JC_CHECK(x->valueint == INT_MAX);
        cJSON_Delete(root);
    }

    /* and an in-range number is untouched */
    root = jc_json_parse("{\"n\":42}");
    if (JC_REQUIRE(root != NULL)) {
        x = cJSON_GetObjectItem(root, "n");
        if (JC_REQUIRE(x != NULL))
            JC_CHECK(x->valueint == 42);
        cJSON_Delete(root);
    }
}

/* M472: nesting depth is bounded, because the INPUT chooses it.
 *
 * Without the bound this test does not fail -- it CRASHES the runner, which is
 * the point. Measured before the fix: SIGSEGV at ~75,000 levels (150 KB) on an
 * 8 MB stack, ~6,000 (12 KB) at 512 KB, ~2,000 (4 KB) at 128 KB. The deep case
 * below is 2,000 levels: comfortably refused now, and chosen to be the depth
 * that killed the smallest-stack row this project ships to, so a regression
 * reproduces the real failure rather than a symbolic one.
 *
 * Note what the two halves assert together: refusing deep input is worthless if
 * it also refuses legitimate input, so the accept case sits just under the limit
 * and the reject case just over it. */
void test_json_depth_limit(void)
{
    char *deep;
    char *at_limit;
    char *over_limit;
    cJSON *root;
    int i;
    const int DEEP = 2000;
    /* JC_JSON_MAX_DEPTH is 256 and private to cJSON.c; these mirror it. If it
     * changes, these two checks are what notice. */
    const int LIMIT = 256;

    /* 1. A pathological document is refused rather than crashing the process. */
    deep = (char *)malloc((size_t)DEEP * 2 + 1);
    if (JC_REQUIRE(deep != NULL)) {
        for (i = 0; i < DEEP; i++)      deep[i] = '[';
        for (i = 0; i < DEEP; i++)      deep[DEEP + i] = ']';
        deep[DEEP * 2] = '\0';
        root = jc_json_parse(deep);
        JC_CHECK(root == NULL);         /* refused, and we are still alive */
        cJSON_Delete(root);
        free(deep);
    }

    /* Objects recurse through the same dispatch, so they are bounded too -- the
     * guard is in parse_value, not in parse_array. */
    deep = (char *)malloc((size_t)DEEP * 6 + 8);
    if (JC_REQUIRE(deep != NULL)) {
        char *w = deep;
        for (i = 0; i < DEEP; i++) { memcpy(w, "{\"a\":", 5); w += 5; }
        memcpy(w, "1", 1); w += 1;
        for (i = 0; i < DEEP; i++) { *w++ = '}'; }
        *w = '\0';
        root = jc_json_parse(deep);
        JC_CHECK(root == NULL);
        cJSON_Delete(root);
        free(deep);
    }

    /* 2. ...and the limit is not so tight that real documents are refused.
     * LIMIT nested arrays is exactly at the bound and must still parse. */
    at_limit = (char *)malloc((size_t)LIMIT * 2 + 1);
    if (JC_REQUIRE(at_limit != NULL)) {
        for (i = 0; i < LIMIT; i++) at_limit[i] = '[';
        for (i = 0; i < LIMIT; i++) at_limit[LIMIT + i] = ']';
        at_limit[LIMIT * 2] = '\0';
        root = jc_json_parse(at_limit);
        JC_CHECK(root != NULL);         /* at the bound: accepted */
        cJSON_Delete(root);
        free(at_limit);
    }

    over_limit = (char *)malloc((size_t)(LIMIT + 1) * 2 + 1);
    if (JC_REQUIRE(over_limit != NULL)) {
        for (i = 0; i < LIMIT + 1; i++) over_limit[i] = '[';
        for (i = 0; i < LIMIT + 1; i++) over_limit[LIMIT + 1 + i] = ']';
        over_limit[(LIMIT + 1) * 2] = '\0';
        root = jc_json_parse(over_limit);
        JC_CHECK(root == NULL);         /* one past the bound: refused */
        cJSON_Delete(root);
        free(over_limit);
    }

    /* 3. The counter is DECREMENTED on the way out, so many shallow siblings do
     * not accumulate toward the limit. This is the bug a depth counter that only
     * counts up would have, and it would look like "big documents fail". */
    root = jc_json_parse("[[1],[2],[3],[4],[5],[6],[7],[8],[9],[10],"
                         "[11],[12],[13],[14],[15],[16],[17],[18]]");
    JC_CHECK(root != NULL);
    cJSON_Delete(root);

    /* An ordinary nested document is unaffected. */
    root = jc_json_parse("{\"a\":[1,2,{\"b\":{\"c\":[{\"d\":true}]}}],\"e\":null}");
    JC_CHECK(root != NULL);
    cJSON_Delete(root);
}

/* M519: the lenient boolean accessor. Written after finding that fifteen
 * shipped example configs said `"pathFence": 1` and every one of them turned
 * the fence OFF -- the strict accessor saw "not a bool", returned the dflt 0,
 * and the key's presence check had already overridden the tri-state default
 * that means "on in autonomous postures". A config that read as fencing was
 * unfencing, and `doctor` said so plainly ("path fence off") to anyone who
 * looked. These checks are the boundary: forgive an unambiguous encoding,
 * never guess at prose. */
void test_json_bool_lenient(void)
{
    cJSON *root;

    root = jc_json_parse("{\"n1\":1,\"n0\":0,\"n2\":2,\"neg\":-1,"
                         "\"bt\":true,\"bf\":false,"
                         "\"st\":\"true\",\"sf\":\"false\","
                         "\"sT\":\"TRUE\",\"sy\":\"yes\",\"sn\":\"No\","
                         "\"s1\":\"1\",\"s0\":\"0\","
                         "\"prose\":\"true-ish, mostly\",\"obj\":{},"
                         "\"arr\":[],\"nul\":null}");
    if (JC_REQUIRE(root != NULL)) {
        /* The number a human writes for a boolean. This is the whole bug. */
        JC_CHECK(jc_json_get_bool_lenient(root, "n1", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "n0", 1) == 0);
        JC_CHECK(jc_json_get_bool_lenient(root, "n2", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "neg", 0) == 1);

        /* A real bool still behaves exactly as before. */
        JC_CHECK(jc_json_get_bool_lenient(root, "bt", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "bf", 1) == 0);

        /* The unambiguous strings, case-insensitively. */
        JC_CHECK(jc_json_get_bool_lenient(root, "st", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "sf", 1) == 0);
        JC_CHECK(jc_json_get_bool_lenient(root, "sT", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "sy", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "sn", 1) == 0);
        JC_CHECK(jc_json_get_bool_lenient(root, "s1", 0) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "s0", 1) == 0);

        /* Prose and containers are NOT booleans: the caller's default wins,
         * both ways round, so a bad value can never flip a fence on OR off. */
        JC_CHECK(jc_json_get_bool_lenient(root, "prose", 0) == 0);
        JC_CHECK(jc_json_get_bool_lenient(root, "prose", 1) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "obj", 0) == 0);
        JC_CHECK(jc_json_get_bool_lenient(root, "arr", 1) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "nul", 1) == 1);
        JC_CHECK(jc_json_get_bool_lenient(root, "missing", 1) == 1);

        /* And the STRICT accessor is unchanged -- that is the point of having
         * two. A number is still not a bool there, so the 56 keys read from
         * our own wire keep failing loudly rather than quietly forgiving. */
        JC_CHECK(jc_json_get_bool(root, "n1", 0) == 0);
        JC_CHECK(jc_json_get_bool(root, "n0", 1) == 1);
        JC_CHECK(jc_json_get_bool(root, "bt", 0) == 1);

        cJSON_Delete(root);
    }
}
