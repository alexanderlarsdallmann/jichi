/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_json.c - exercises the in-tree cJSON-API implementation and the jc_json
 * wrapper. NOT vendored: src/json/cJSON.{c,h} is original code (M171). */

#include <limits.h>

#include "jc_test.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include <stdlib.h>
#include <string.h>

void test_json_print_exact(void);  /* M1076: run from test_main.c */
void test_json_trailing_garbage(void); /* M1078: run from test_main.c */

/* M1089: called from test_json's body (no new test_main.c entry -- that file
 * is shared with another milestone). RED before the fix: it references the
 * new cJSON_jc_*_max_nodes symbols, which do not exist yet, so this TU fails
 * to link. */
static void test_json_node_cap(void);

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
    if (JC_REQUIRE(nested != NULL)) { /* a guard (M729) */
        JC_CHECK_STR(jc_json_get_str(nested, "k", "?"), "v");
    }

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
    if (JC_REQUIRE(reparse != NULL)) { /* a guard (M729) */
        JC_CHECK_STR(jc_json_get_str(reparse, "name", "?"), "jichi");
    }
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
        if (JC_REQUIRE(s != NULL)) { /* a guard (M729) */
            JC_CHECK(strstr(s, "\"role\":\"user\"") != NULL);
            JC_CHECK(strstr(s, "\"items\":[\"x\",\"y\"]") != NULL);
        }
        free(s);
        cJSON_Delete(o);
    }

    /* Malformed input returns NULL, not a crash. */
    JC_CHECK(jc_json_parse("{ broken") == NULL);
    JC_CHECK(jc_json_parse("[1,2,") == NULL);

    /* M1089: the per-parse node-count cap (A-06). Runs inside test_json so no
     * new test_main.c entry is needed. */
    test_json_node_cap();

    /* M1113 (A-13): duplicate object keys -- the accessor is LAST-WRITE-WINS.
     *
     * The parser keeps every member it sees (parse_object appends with no
     * dedup -- a parse-time replace would cost O(n^2) per object, the M1089
     * node-cap era), so {"x":1,"x":2} yields TWO children. The policy is
     * therefore enforced at the ONE accessor every jc_json_get_* routes
     * through (jc_json.c:22/31/63/159/169/192):
     * cJSON_GetObjectItemCaseSensitive returns the LAST matching member, not
     * the first -- matching most JSON consumers and servers. RFC 8259 leaves
     * the duplicate policy to the consumer (the finding's "last-write-wins
     * expected"). RED before the fix: leg 1's GetObjectItem returns the value
     * 1, not 2, and leg 2's accessor returns the string, not the number.
     *
     * Boundary: jichi-built objects (the cJSON_AddItemToObject path, which is
     * deliberately untouched) always carry unique keys, so legs 3/4 are
     * no-ops on them -- leg 3 is the control that pins that. */
    {
        cJSON *dup;
        dup = jc_json_parse("{\"x\":1,\"x\":2}");
        if (JC_REQUIRE(dup != NULL)) {
            /* Leg 1: the accessor reads the LAST value. */
            JC_CHECK(cJSON_GetObjectItem(dup, "x")->valuedouble == 2.0);
            JC_CHECK(jc_json_get_num(dup, "x", -1.0) == 2.0);
            cJSON_Delete(dup);
        }
        /* Leg 2: mixed types -- the last member's type wins the read. */
        dup = jc_json_parse("{\"y\":\"a\",\"y\":3}");
        if (JC_REQUIRE(dup != NULL)) {
            JC_CHECK(cJSON_GetObjectItem(dup, "y")->valuedouble == 3.0);
            JC_CHECK(jc_json_get_num(dup, "y", -1.0) == 3.0);
            cJSON_Delete(dup);
        }
        /* Leg 3: the no-dup control -- unique keys are unaffected (this is a
         * no-op for every jichi-built object). */
        dup = jc_json_parse("{\"z\":7}");
        if (JC_REQUIRE(dup != NULL)) {
            JC_CHECK(cJSON_GetObjectItem(dup, "z")->valuedouble == 7.0);
            JC_CHECK(jc_json_get_num(dup, "z", -1.0) == 7.0);
            cJSON_Delete(dup);
        }
        /* Leg 4: the policy is per-object and recurses into children. */
        dup = jc_json_parse("{\"o\":{\"k\":1,\"k\":2}}");
        if (JC_REQUIRE(dup != NULL)) {
            cJSON *inner = cJSON_GetObjectItem(dup, "o");
            JC_CHECK(inner != NULL &&
                     cJSON_GetObjectItem(inner, "k")->valuedouble == 2.0);
            cJSON_Delete(dup);
        }
    }
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

    /* M866: a number BUILT in jichi clamps the same way -- cJSON_CreateNumber kept
     * the bare cast after the parser was fixed, until M861's test built 5e9 and
     * batch 4's make ci (UBSan) stopped on it. */
    x = cJSON_CreateNumber(5e9);
    if (JC_REQUIRE(x != NULL)) {
        JC_CHECK(x->valueint == INT_MAX);
        JC_CHECK(x->valuedouble > 4.9e9);
        cJSON_Delete(x);
    }
    x = cJSON_CreateNumber(-5e9);
    if (JC_REQUIRE(x != NULL)) {
        JC_CHECK(x->valueint == INT_MIN);
        cJSON_Delete(x);
    }
    x = cJSON_CreateNumber(42.0);
    if (JC_REQUIRE(x != NULL)) {
        JC_CHECK(x->valueint == 42);
        cJSON_Delete(x);
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

/* M1076 (A-09 / L-04): the JSON printer is EXACT, because jichi reprints JSON
 * it did not write.
 *
 * The old pb_put_number printed integral |d| <= 1e15 with
 * sprintf(tmp, "%ld", (long)d) and everything else with %g (6 significant
 * digits). Both legs were lossy: %g mangled 50.5871234 into 50.5871 and
 * 1760684400.125 into 1.76068e+09 (measured on the LP64 head, run 0091), and
 * on ILP32 (long is 32-bit) (long)d of 3e9 is out-of-range undefined
 * behaviour that printed -2147483648 and tripped
 * -fsanitize=float-cast-overflow. config set reprints the whole config,
 * Anthropic tool inputs are reparsed and reprinted, and protocol builders
 * AddNumber from millisecond timestamps, so a rounded reprint is a silently
 * corrupted wire value.
 *
 * This test pins the exactness contract: parse a numeric literal, print it
 * back, and the printed string must be the literal's EXACT value -- re-parsing
 * it yields the identical double. Where the literal itself is the exact
 * value's own spelling (exact=1), the printed string equals the literal
 * byte-for-byte. Red on the unfixed tree on the lossy %g cases; the -m32
 * garbage case (3000000000) is proven on a 32-bit scratch build (no ci stage
 * builds -m32, L-09). 9007199254740991 is 2^53-1, the largest exactly
 * printable integral; 1234567890123456789 is not representable at all, so its
 * printed form is the exact value's shortest round-trip, not the literal. */
void test_json_print_exact(void)
{
    struct { const char *lit; int exact; } cases[] = {
        { "50.5871234", 1 },
        { "1760684400.125", 1 },
        { "9007199254740991", 1 },
        /* M1076-b (review nit 1): 2^53, the only integer that crosses the
         * pb_put_number branch (|d| < 2^53 -> %.0f, else %.15g/%.17g). Measured
         * in the M1076-b report: glibc %.15g of 2^53 is
         * "9.00719925474099e+15", which does NOT round-trip to the double, so the
         * printer falls to %.17g = "9007199254740992" -- the exact spelling. The
         * reviewer assumed exact=0; the measurement says exact=1, so the printed
         * string equals the literal byte-for-byte. This row pins the crossing
         * value; it is output-benign to a one-bit flip of the boundary (both
         * legs agree at 2^53) -- recorded in the report. */
        { "9007199254740992", 1 },
        { "3000000000", 1 },
        { "1234567890123456789", 0 },
        { "0.1234567891", 1 },
        /* M1076-b (review nit 2): a sub-1.0 value exercising %.15g's
         * negative-exponent form. glibc %.15g of 1e-7 is "1e-07" (two-digit
         * exponent, the C99 minimum), which round-trips, so exact=1. Portability
         * caveat (glibc-measured here; two-digit exponent is the standard form)
         * is in the report. */
        { "1e-07", 1 },
        { "-2147483648.0", 0 }
    };
    unsigned i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const char *lit = cases[i].lit;
        cJSON *root = cJSON_Parse(lit);
        double d;
        char *printed;
        cJSON *re;
        if (!JC_REQUIRE(root != NULL)) continue;
        d = root->valuedouble;
        printed = cJSON_PrintUnformatted(root);
        if (JC_REQUIRE(printed != NULL)) {
            /* A bare number: PrintUnformatted gives the number itself. */
            re = cJSON_Parse(printed);
            if (JC_REQUIRE(re != NULL)) {
                /* The EXACT value: the printed text reads back to the same double. */
                JC_CHECK(re->valuedouble == d);
                if (cases[i].exact) {
                    JC_CHECK_STR(printed, lit);
                }
            }
            cJSON_Delete(re);
        }
        free(printed);
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

/* M725: a string costs its own length, not the rest of the document.
 * parse_string sized every buffer to the REST OF THE INPUT ("worst case") and
 * kept it that size as the node's key or value, so a document's memory grew as
 * strings x remaining bytes: an embeddings reply of ~1.5 MB with ~260 strings
 * held ~190 MB, the peak massif found in `jichi index`. The document here has 400
 * short strings ahead of a 64 KB tail, so the old sizing charges each string at
 * least 64 KB -- over 25 MB in all -- where their own spans are a few KB. */
void test_json_string_alloc(void)
{
    struct jc_sb doc;
    char item[48];
    size_t before, spent;
    cJSON *root;
    int i;

    jc_sb_init(&doc);
    jc_sb_append(&doc, "{\"items\":[");
    for (i = 0; i < 200; i++) {
        jc_snprintf(item, sizeof(item), "%s{\"k%03d\":\"v%03d\"}", i ? "," : "", i, i);
        jc_sb_append(&doc, item);
    }
    jc_sb_append(&doc, "],\"tail\":\"");
    for (i = 0; i < 64 * 1024; i++) {
        jc_sb_append(&doc, "x");
    }
    jc_sb_append(&doc, "\"}");
    JC_CHECK(doc.data != NULL);
    if (doc.data == NULL) {
        return;
    }
    before = cJSON_jc_string_bytes();
    root = jc_json_parse(doc.data);
    spent = cJSON_jc_string_bytes() - before;
    JC_CHECK(root != NULL);
    /* 402 strings of a few bytes, and a 64 KB one: well under twice the document. */
    JC_CHECK(spent < 2 * doc.len);
    JC_CHECK(spent > (size_t)(64 * 1024));  /* the counter is counting */
    {
        cJSON *tail = root != NULL ? cJSON_GetObjectItem(root, "tail") : NULL;
        JC_CHECK(cJSON_IsString(tail) && strlen(tail->valuestring) == 64 * 1024);
    }
    cJSON_Delete(root);
    jc_sb_free(&doc);
}

/* A-02 (M1077): the untrusted-JSON double -> integer clamps.
 *
 * The defect they exist for: a model or server can send a JSON number the
 * target type cannot hold ({"limit":1e10}), and casting that double to int/long
 * is undefined behaviour -- UBSan aborts under float-cast-overflow, and
 * (int)1e10 on x86-64 silently reads as INT_MIN. The clamp returns the range
 * bound instead, so the value is defined and, for the out-of-range case, the
 * largest the type can express.
 *
 * Rules asserted below: in-range doubles (2^53 and below) pass EXACTLY; +Inf
 * -> max; -Inf -> min; NaN -> 0; and a value just past a bound clamps to that
 * bound. The end-to-end leg parses {"limit":1e10} and clamps what
 * jc_json_get_num returns -- the finding's measured value, mock-free. */
void test_json_clamp_helpers(void)
{
    /* In-range passthrough: a number the sender could actually write is exact. */
    JC_CHECK(jc_json_clamp_int(42.0) == 42);
    /* 64-bit arm: the original check (5000000000L is in-range). 32-bit arm:
     * 5000000000.0 overflows a 32-bit long, so the clamp returns LONG_MAX. */
#if LONG_MAX > 2147483647L
    JC_CHECK(jc_json_clamp_long(5000000000.0) == 5000000000L);
#else
    JC_CHECK(jc_json_clamp_long(5000000000.0) == LONG_MAX);
#endif

    /* Both-boundary clamps, signed. 1e10 > INT_MAX, -1e10 < INT_MIN. */
    JC_CHECK(jc_json_clamp_int(1e10) == INT_MAX);
    JC_CHECK(jc_json_clamp_int(-1e10) == INT_MIN);
    JC_CHECK(jc_json_clamp_long(1e100) == LONG_MAX);

    /* NaN is not a number at all -> 0, on every width. */
    JC_CHECK(jc_json_clamp_int(0.0 / 0.0) == 0);
    JC_CHECK(jc_json_clamp_long(0.0 / 0.0) == 0);
    JC_CHECK(jc_json_clamp_uint(0.0 / 0.0) == 0);
    JC_CHECK(jc_json_clamp_size(0.0 / 0.0) == 0);

    /* The unsigned pair: -Inf and every negative clamp to the min (0). */
    JC_CHECK(jc_json_clamp_uint(-1e300) == 0);
    JC_CHECK(jc_json_clamp_size(-1e300) == 0);
    JC_CHECK(jc_json_clamp_uint(-1.0) == 0);
    /* And +Inf / an out-of-range positive clamp to the max. */
    JC_CHECK(jc_json_clamp_uint(1e10) == UINT_MAX);
    JC_CHECK(jc_json_clamp_size(1e100) == (jc_size)~(jc_size)0);

    /* A value just inside a bound still passes; just outside clamps. */
    JC_CHECK(jc_json_clamp_uint(4294967295.0) == UINT_MAX); /* 2^32-1 exactly */
    JC_CHECK(jc_json_clamp_uint(4294967296.0) == UINT_MAX); /* 2^32 -> clamp */

    /* End-to-end: parse the finding's own value and read it through the clamp,
     * no mock -- a model sending {"limit":1e10} to tu_arg_int. */
    {
        cJSON *root = jc_json_parse("{\"limit\":1e10}");
        if (JC_REQUIRE(root != NULL)) {
            JC_CHECK(jc_json_clamp_int(jc_json_get_num(root, "limit", -1.0)) == INT_MAX);
            JC_CHECK(jc_json_get_num(root, "limit", -1.0) > 1e9); /* the double is intact */
            cJSON_Delete(root);
        }
    }
}

/* M1078 (finding A-08): cJSON_Parse must REFUSE trailing garbage after the
 * root value. The parser used to return parse_value's result after c.ok
 * without checking that the rest of the input is whitespace, so
 * {"a":1}} -- all four of the shapes the finding measured on head bd00035b
 * -- came back ACCEPTED (returning the first value). A document with
 * anything after the root is now refused.
 *
 * The clean-documents half is the load-bearing control: refusing trailing
 * garbage is worthless if it also refuses a legitimate document whose only
 * tail is whitespace ({"a":1} followed by spaces/newline, [1,2], {"x":1}),
 * so those must stay accepted, with the right root. */
void test_json_trailing_garbage(void)
{
    /* The four confirmed garbage cases (A-08, run 0091): all must be NULL. */
    JC_CHECK(cJSON_Parse("{\"a\":1}}") == NULL);
    JC_CHECK(cJSON_Parse("{\"a\":1}{\"b\":2}") == NULL);
    JC_CHECK(cJSON_Parse("{\"path\":\"x\"} garbage") == NULL);
    JC_CHECK(cJSON_Parse("[1,2]]") == NULL);
    /* The jc_json_parse wrapper must refuse them too. */
    JC_CHECK(jc_json_parse("{\"a\":1}}") == NULL);
    JC_CHECK(jc_json_parse("[1,2]]") == NULL);

    /* A clean document with only trailing whitespace must still parse, with
     * the right root. */
    {
        cJSON *root = jc_json_parse("{\"a\":1}   \n");
        if (JC_REQUIRE(root != NULL)) {
            JC_CHECK(root->type == cJSON_Object);
            JC_CHECK(jc_json_get_num(root, "a", -1.0) == 1.0);
            cJSON_Delete(root);
        }
    }
    {
        cJSON *root = jc_json_parse("[1,2]");
        if (JC_REQUIRE(root != NULL)) {
            JC_CHECK(root->type == cJSON_Array);
            JC_CHECK(cJSON_GetArraySize(root) == 2);
            cJSON_Delete(root);
        }
    }
    {
        cJSON *root = jc_json_parse("{\"x\":1}");
        if (JC_REQUIRE(root != NULL)) {
            JC_CHECK(root->type == cJSON_Object);
            JC_CHECK(jc_json_get_num(root, "x", -1.0) == 1.0);
            cJSON_Delete(root);
        }
    }

    /* SV:0758 (2026-10-29 07:58): the supervisor measured the riskiest
     * neighbour (tool-call argument strings, this drive's own runs 0077-0091:
     * 2308 strings, 0 with a valid root + trailing content) and named the two
     * must-still-parse tails the whitespace skip decides -- a CRLF config line
     * and a jsonl line read with its newline. Both stay accepted. */
    {
        cJSON *root = jc_json_parse("{\"a\":1}\r\n");
        if (JC_REQUIRE(root != NULL)) {
            JC_CHECK(root->type == cJSON_Object);
            JC_CHECK(jc_json_get_num(root, "a", -1.0) == 1.0);
            cJSON_Delete(root);
        }
    }
    {
        cJSON *root = jc_json_parse("{\"a\":1}\n\n");
        if (JC_REQUIRE(root != NULL)) {
            JC_CHECK(root->type == cJSON_Object);
            JC_CHECK(jc_json_get_num(root, "a", -1.0) == 1.0);
            cJSON_Delete(root);
        }
    }
}

/* M1089 (A-06): the parser caps the NODE COUNT of one parse. It already capped
 * nesting (M472) and the input is byte-capped per SSE field, but nothing capped
 * how many 64-byte nodes the parse allocates: a dense array of small numbers is
 * one node per element, so a large-but-valid reply expands ~40x into tree
 * memory while it is parsed (the lead confirmed on head: 1.9 MB of numbers ->
 * ~78 MB peak RSS). The cap, JC_JSON_MAX_NODES (default 1,000,000,
 * runtime-lowerable), refuses a parse that would need more nodes and frees
 * everything it had already built, so the caller gets NULL -- the
 * error-as-a-value shape -- not a partial tree.
 *
 * RED before the fix: this helper references cJSON_jc_get_max_nodes and
 * cJSON_jc_set_max_nodes, which the pre-fix parser does not define, so the test
 * binary fails to LINK (undefined symbols) -- that link failure is the red.
 * After the fix the behavioural checks (r == NULL under the cap, r != NULL after
 * restore) prove the cap actually refuses and restores, not merely that the
 * symbols now exist. */
static void test_json_node_cap(void)
{
    size_t old;
    char *a2000;
    char *a3000;
    int i;
    cJSON *r;

    old = cJSON_jc_get_max_nodes();
    JC_CHECK(old >= 1000);   /* sanity: the default cap is 1,000,000 nodes */

    /* Build '[0,0,...]' with 2000 elements -- 2001 nodes in all (the array
     * plus one number per element). No sprintf: plain char* in a loop. */
    a2000 = (char *)malloc((size_t)2000 * 3 + 8);
    if (JC_REQUIRE(a2000 != NULL)) {
        char *w = a2000;
        *w++ = '[';
        for (i = 0; i < 2000; i++) {
            if (i > 0) *w++ = ',';
            *w++ = '0';
        }
        *w++ = ']';
        *w = '\0';

        /* 2001 nodes against a cap of 1024: the cap must refuse it. */
        cJSON_jc_set_max_nodes(1024);
        r = cJSON_Parse(a2000);
        JC_CHECK(r == NULL);     /* refused by the cap, and we are still alive */
        cJSON_Delete(r);

        /* Restore: 2001 nodes is far below the default, so it parses again --
         * this half proves the restore works, not just that the cap is set. */
        cJSON_jc_set_max_nodes(old);
        r = cJSON_Parse(a2000);
        JC_CHECK(r != NULL);
        if (JC_REQUIRE(r != NULL)) {
            JC_CHECK(cJSON_GetArraySize(r) == 2000);
            cJSON_Delete(r);
        }
        free(a2000);
    }

    /* Second case: a 3000-element array (3001 nodes) against a cap of 2500
     * refuses, then restores and parses. Kept cheap -- no new symbols. */
    a3000 = (char *)malloc((size_t)3000 * 3 + 8);
    if (JC_REQUIRE(a3000 != NULL)) {
        char *w = a3000;
        *w++ = '[';
        for (i = 0; i < 3000; i++) {
            if (i > 0) *w++ = ',';
            *w++ = '0';
        }
        *w++ = ']';
        *w = '\0';

        cJSON_jc_set_max_nodes(2500);
        r = cJSON_Parse(a3000);
        JC_CHECK(r == NULL);     /* 3001 > 2500: refused */
        cJSON_Delete(r);

        cJSON_jc_set_max_nodes(old);
        r = cJSON_Parse(a3000);
        JC_CHECK(r != NULL);
        if (JC_REQUIRE(r != NULL)) {
            JC_CHECK(cJSON_GetArraySize(r) == 3000);
            cJSON_Delete(r);
        }
        free(a3000);
    }
}
