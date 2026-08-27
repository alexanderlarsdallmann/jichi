/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_imagegen.c - pure image-generation body builder + response parser (M32). */

#include "jc_test.h"
#include "jc_imagegen.h"
#include "jc_json.h"

#include <stdlib.h>
#include <string.h>

static void test_build_body(void)
{
    char *body = jc_imagegen_build_body("dall-e-3", "a red cube",
                                        "1024x1024", "png");
    cJSON *root;
    JC_CHECK(body != NULL);
    root = jc_json_parse(body);
    JC_CHECK(root != NULL);
    JC_CHECK_STR(jc_json_get_str(root, "model", ""), "dall-e-3");
    JC_CHECK_STR(jc_json_get_str(root, "prompt", ""), "a red cube");
    JC_CHECK_STR(jc_json_get_str(root, "response_format", ""), "b64_json");
    JC_CHECK_STR(jc_json_get_str(root, "size", ""), "1024x1024");
    JC_CHECK_STR(jc_json_get_str(root, "output_format", ""), "png");
    JC_CHECK(jc_json_get_num(root, "n", 0.0) == 1.0);
    cJSON_Delete(root);
    free(body);

    /* Optional fields omitted when NULL. */
    body = jc_imagegen_build_body("m", "p", NULL, NULL);
    root = jc_json_parse(body);
    JC_CHECK(jc_json_get_obj(root, "size") == NULL);
    JC_CHECK(jc_json_get_obj(root, "output_format") == NULL);
    JC_CHECK(jc_json_get_obj(root, "ref_images") == NULL);
    cJSON_Delete(root);
    free(body);
}

static void test_build_body_ex(void)
{
    const char *refs[2];
    char *plain;
    char *with_refs;
    cJSON *root;
    cJSON *arr;

    /* With no ref_images, _ex is byte-identical to the plain builder (so the
     * text-to-image request never changes). */
    plain = jc_imagegen_build_body("m", "p", "512x512", "png");
    with_refs = jc_imagegen_build_body_ex("m", "p", "512x512", "png", NULL, 0);
    JC_CHECK(plain != NULL && with_refs != NULL);
    JC_CHECK_STR(with_refs, plain);
    free(plain);
    free(with_refs);

    /* ref_images present => a JSON array of the sources is emitted. */
    refs[0] = "data:image/png;base64,AAAA";
    refs[1] = "https://example.com/b.png";
    with_refs = jc_imagegen_build_body_ex("kontext", "make it blue", NULL,
                                          "png", refs, 2);
    JC_CHECK(with_refs != NULL);
    root = jc_json_parse(with_refs);
    arr = jc_json_get_obj(root, "ref_images");
    JC_CHECK(cJSON_IsArray(arr) && cJSON_GetArraySize(arr) == 2);
    JC_CHECK_STR(jc_json_get_str(root, "prompt", ""), "make it blue");
    cJSON_Delete(root);
    free(with_refs);
}

static void test_parse(void)
{
    unsigned char *bytes = NULL;
    jc_size len = 99;
    char *url = (char *)1;

    /* b64_json "aGVsbG8=" decodes to "hello". */
    JC_CHECK(jc_imagegen_parse("{\"data\":[{\"b64_json\":\"aGVsbG8=\"}]}",
                               &bytes, &len, &url) == JC_OK);
    JC_CHECK(bytes != NULL && len == 5 && memcmp(bytes, "hello", 5) == 0);
    JC_CHECK(url == NULL);
    free(bytes);

    /* url-only response: no bytes, url handed back for the caller to fetch. */
    bytes = (unsigned char *)1;
    url = NULL;
    JC_CHECK(jc_imagegen_parse("{\"data\":[{\"url\":\"http://x/y.png\"}]}",
                               &bytes, &len, &url) == JC_OK);
    JC_CHECK(bytes == NULL);
    JC_CHECK_STR(url, "http://x/y.png");
    free(url);

    /* Malformed / empty data => parse error. */
    JC_CHECK(jc_imagegen_parse("{\"data\":[]}", &bytes, &len, &url)
             == JC_ERR_PARSE);
    JC_CHECK(jc_imagegen_parse("{}", &bytes, &len, &url) == JC_ERR_PARSE);
    JC_CHECK(jc_imagegen_parse("not json", &bytes, &len, &url) == JC_ERR_PARSE);
}

void test_imagegen(void)
{
    test_build_body();
    test_build_body_ex();
    test_parse();
}
