/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_transcribe.c - pure transcription parser + audio media-type (M33). */

#include "jc_test.h"
#include "jc_transcribe.h"
#include "jc_audio.h"

#include <stdlib.h>

static void test_parse(void)
{
    char *text = NULL;

    /* Plain {"text":...} shape. */
    JC_CHECK(jc_transcribe_parse("{\"text\":\"hello world\"}", &text) == JC_OK);
    JC_CHECK_STR(text, "hello world");
    free(text);

    /* Verbose-json carries a top-level "text" too. */
    text = NULL;
    JC_CHECK(jc_transcribe_parse(
        "{\"task\":\"transcribe\",\"text\":\"hi there\",\"segments\":[]}",
        &text) == JC_OK);
    JC_CHECK_STR(text, "hi there");
    free(text);

    /* Missing text / malformed => parse error, *out left NULL. */
    text = (char *)1;
    JC_CHECK(jc_transcribe_parse("{}", &text) == JC_ERR_PARSE);
    JC_CHECK(text == NULL);
    JC_CHECK(jc_transcribe_parse("not json", &text) == JC_ERR_PARSE);
}

static void test_media_type(void)
{
    JC_CHECK_STR(jc_audio_media_type("a.mp3"), "audio/mpeg");
    JC_CHECK_STR(jc_audio_media_type("dir/CLIP.MP3"), "audio/mpeg");
    JC_CHECK_STR(jc_audio_media_type("a.mpga"), "audio/mpeg");
    JC_CHECK_STR(jc_audio_media_type("a.wav"), "audio/wav");
    JC_CHECK_STR(jc_audio_media_type("a.m4a"), "audio/mp4");
    JC_CHECK_STR(jc_audio_media_type("a.mp4"), "audio/mp4");
    JC_CHECK_STR(jc_audio_media_type("a.flac"), "audio/flac");
    JC_CHECK_STR(jc_audio_media_type("a.ogg"), "audio/ogg");
    JC_CHECK_STR(jc_audio_media_type("a.webm"), "audio/webm");
    JC_CHECK(jc_audio_media_type("notes.txt") == NULL);
    JC_CHECK(jc_audio_media_type("a.png") == NULL);
    JC_CHECK(jc_audio_media_type("noext") == NULL);
    JC_CHECK(jc_audio_media_type(NULL) == NULL);
}

void test_transcribe(void)
{
    test_parse();
    test_media_type();
}
