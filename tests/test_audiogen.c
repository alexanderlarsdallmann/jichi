/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_audiogen.c - pure TTS body builder + format helper (M32). */

#include "jc_test.h"
#include "jc_audiogen.h"
#include "jc_json.h"

#include <stdlib.h>

static void test_build_body(void)
{
    char *body = jc_audiogen_build_body("tts-1", "hello there", "alloy", "mp3");
    cJSON *root;
    JC_CHECK(body != NULL);
    root = jc_json_parse(body);
    JC_CHECK(root != NULL);
    JC_CHECK_STR(jc_json_get_str(root, "model", ""), "tts-1");
    JC_CHECK_STR(jc_json_get_str(root, "input", ""), "hello there");
    JC_CHECK_STR(jc_json_get_str(root, "voice", ""), "alloy");
    JC_CHECK_STR(jc_json_get_str(root, "response_format", ""), "mp3");
    cJSON_Delete(root);
    free(body);

    /* voice omitted when NULL. */
    body = jc_audiogen_build_body("m", "hi", NULL, "wav");
    root = jc_json_parse(body);
    JC_CHECK(jc_json_get_obj(root, "voice") == NULL);
    JC_CHECK_STR(jc_json_get_str(root, "response_format", ""), "wav");
    cJSON_Delete(root);
    free(body);
}

static void test_format(void)
{
    JC_CHECK_STR(jc_audiogen_format("a.mp3"), "mp3");
    JC_CHECK_STR(jc_audiogen_format("dir/SPEECH.MP3"), "mp3");
    JC_CHECK_STR(jc_audiogen_format("a.wav"), "wav");
    JC_CHECK_STR(jc_audiogen_format("a.opus"), "opus");
    JC_CHECK_STR(jc_audiogen_format("a.aac"), "aac");
    JC_CHECK_STR(jc_audiogen_format("a.flac"), "flac");
    JC_CHECK_STR(jc_audiogen_format("a.pcm"), "pcm");
    JC_CHECK(jc_audiogen_format("a.png") == NULL);
    JC_CHECK(jc_audiogen_format("noext") == NULL);
    JC_CHECK(jc_audiogen_format(NULL) == NULL);
}

void test_audiogen(void)
{
    test_build_body();
    test_format();
}
