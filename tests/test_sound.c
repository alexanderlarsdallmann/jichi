/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sound.c - the pure sound-tool helpers (M163b). */

#include "jc_test.h"
#include "jc_sound.h"

#include <string.h>

static void test_clamp(void)
{
    /* req<=0 -> effective max; max<=0 -> default 60. */
    JC_CHECK(jc_sound_clamp_seconds(0, 0) == JC_SOUND_RECORD_DEFAULT);
    JC_CHECK(jc_sound_clamp_seconds(-5, 30) == 30);
    JC_CHECK(jc_sound_clamp_seconds(10, 30) == 10);
    JC_CHECK(jc_sound_clamp_seconds(100, 30) == 30);   /* clamped to max */
    JC_CHECK(jc_sound_clamp_seconds(1, 30) == 1);
    /* Hard cap regardless of a huge configured max. */
    JC_CHECK(jc_sound_clamp_seconds(100000, 1000000)
             == JC_SOUND_RECORD_CAP);
}

static void test_name(void)
{
    char buf[64];
    jc_sound_default_name("recording", 1753300000.9, buf, sizeof(buf));
    JC_CHECK(strcmp(buf, "recording-1753300000.wav") == 0);
    jc_sound_default_name(NULL, -3.0, buf, sizeof(buf));
    JC_CHECK(strcmp(buf, "audio-0.wav") == 0);
}

static void test_build_argv(void)
{
    char *args[2];
    char *argv[8];
    int n;

    args[0] = (char *)"-q";
    args[1] = (char *)"-f";

    /* command + 2 args + file, NULL-terminated. */
    n = jc_sound_build_argv("aplay", args, 2, jc_test_tmp("x.wav"), argv, 8);
    JC_CHECK(n == 4);
    JC_CHECK(strcmp(argv[0], "aplay") == 0);
    JC_CHECK(strcmp(argv[1], "-q") == 0);
    JC_CHECK(strcmp(argv[2], "-f") == 0);
    JC_CHECK(strcmp(argv[3], jc_test_tmp("x.wav")) == 0);
    JC_CHECK(argv[4] == NULL);

    /* No file -> just command + args. */
    n = jc_sound_build_argv("aplay", args, 2, NULL, argv, 8);
    JC_CHECK(n == 3 && argv[3] == NULL);

    /* No args. */
    n = jc_sound_build_argv("arecord", NULL, 0, jc_test_tmp("r.wav"), argv, 8);
    JC_CHECK(n == 2 && strcmp(argv[1], jc_test_tmp("r.wav")) == 0);

    /* Overflow guard. */
    JC_CHECK(jc_sound_build_argv("aplay", args, 2, jc_test_tmp("x.wav"), argv, 3) < 0);
    JC_CHECK(jc_sound_build_argv(NULL, args, 2, NULL, argv, 8) < 0);
}

void test_sound(void)
{
    test_clamp();
    test_name();
    test_build_argv();
}
