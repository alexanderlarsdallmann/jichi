/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_packages.c - the M115 package catalog + recommend-prompt (pure). */

#include "jc_test.h"
#include "jc_packages.h"
#include "jc_str.h"

#include <string.h>

void test_packages(void)
{
    struct jc_sb sb;

    /* the catalog lists known presets + packs */
    jc_sb_init(&sb);
    jc_packages_render_catalog(&sb);
    JC_CHECK(sb.data != NULL);
    JC_CHECK(strstr(sb.data, "developer") != NULL);   /* a preset */
    JC_CHECK(strstr(sb.data, "c-cli") != NULL);        /* a pack */
    JC_CHECK(strstr(sb.data, "presets") != NULL);
    jc_sb_free(&sb);

    /* the recommend prompt embeds the catalog + the project summary + an ask */
    jc_sb_init(&sb);
    jc_packages_recommend_prompt("files: main.zig, build.zig", &sb);
    JC_CHECK(strstr(sb.data, "Recommend ONE preset") != NULL);
    JC_CHECK(strstr(sb.data, "zig-cli") != NULL);      /* catalog embedded */
    JC_CHECK(strstr(sb.data, "build.zig") != NULL);    /* summary embedded */
    jc_sb_free(&sb);

    /* NULL summary is safe */
    jc_sb_init(&sb);
    jc_packages_recommend_prompt(NULL, &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "unknown") != NULL);
    jc_sb_free(&sb);
}
