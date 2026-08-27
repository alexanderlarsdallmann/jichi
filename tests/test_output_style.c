/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_output_style.c - custom output styles: parse + select + load (M28). */

#include "jc_test.h"
#include "jc_output_style.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_parse(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_output_style o;

    if (a == NULL) {
        return;
    }
    jc_output_style_parse(
        "---\ndescription: Terse answers\n---\nBe concise. Bullet points only.\n",
        "concise", a, &o);
    JC_CHECK_STR(o.name, "concise");
    JC_CHECK_STR(o.description, "Terse answers");
    JC_CHECK(strstr(o.body, "Be concise") != NULL);

    /* No frontmatter: description NULL, whole text is the body. */
    jc_output_style_parse("Just the body.\n", "plain", a, &o);
    JC_CHECK_STR(o.name, "plain");
    JC_CHECK(o.description == NULL);
    JC_CHECK(strstr(o.body, "Just the body") != NULL);

    jc_arena_free(a);
}

static void test_load_and_select(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_output_style_set set;
    char base[256];
    char dir[400];
    char path[512];
    long pid = (long)getpid();
    const struct jc_output_style *o;
    struct jc_sb sb;

    if (a == NULL) {
        return;
    }
    /* base = /tmp/jichi_ostyle_<pid> ; styles live in base/.jichi/output-styles */
    {
        const char *pfx = jc_test_tmp("jichi_ostyle_");
        char num[32];
        int nn = 0;
        long v = pid;
        size_t pl = strlen(pfx);
        int i;
        if (v == 0) num[nn++] = '0';
        while (v > 0 && nn < 30) { num[nn++] = (char)('0' + v % 10); v /= 10; }
        memcpy(base, pfx, pl);
        for (i = 0; i < nn; i++) base[pl + i] = num[nn - 1 - i];
        base[pl + nn] = '\0';
    }
    jc_snprintf(dir, sizeof(dir), "%s/.jichi/output-styles", base);
    if (jc_mkdir_p(dir) != JC_OK) {
        jc_arena_free(a);
        return;
    }
    jc_snprintf(path, sizeof(path), "%s/concise.md", dir);
    jc_write_file(path, "---\ndescription: Terse\n---\nBe brief.\n", 38);
    jc_snprintf(path, sizeof(path), "%s/explain.md", dir);
    jc_write_file(path, "Explain thoroughly.\n", 20);

    jc_output_style_set_init(&set);
    jc_output_style_load(&set, base, a);
    JC_CHECK(set.styles.len == 2);

    o = jc_output_style_find(&set, "concise");
    JC_CHECK(o != NULL);
    JC_CHECK(jc_output_style_active(&set) == NULL); /* none active yet */

    JC_CHECK(jc_output_style_set_active(&set, "concise") == 1);
    o = jc_output_style_active(&set);
    JC_CHECK(o != NULL && strcmp(o->name, "concise") == 0);

    /* Unknown style: not activated, active unchanged. */
    JC_CHECK(jc_output_style_set_active(&set, "nope") == 0);
    JC_CHECK(jc_output_style_active(&set) != NULL);

    /* Clear. */
    JC_CHECK(jc_output_style_set_active(&set, NULL) == 1);
    JC_CHECK(jc_output_style_active(&set) == NULL);

    /* Render marks the active one with '*'. */
    jc_output_style_set_active(&set, "concise");
    jc_sb_init(&sb);
    jc_output_style_render_list(&set, &sb);
    JC_CHECK(strstr(sb.data, "* concise") != NULL);
    JC_CHECK(strstr(sb.data, "explain") != NULL);
    jc_sb_free(&sb);

    jc_output_style_set_free(&set);

    /* Cleanup (best-effort). */
    jc_snprintf(path, sizeof(path), "%s/concise.md", dir);
    remove(path);
    jc_snprintf(path, sizeof(path), "%s/explain.md", dir);
    remove(path);
    remove(dir);
    jc_snprintf(dir, sizeof(dir), "%s/.jichi", base);
    remove(dir);
    remove(base);

    jc_arena_free(a);
}

void test_output_style(void)
{
    test_parse();
    test_load_and_select();
}
