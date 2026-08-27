/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_argpath.c - the paths a tool call declares (M501).
 *
 * The defect these pin: M459's explicit-scope exemption tested `write_file` and
 * `edit_file` BY NAME, so `apply_patch` -- whose paths live in `edits[]`, and
 * which is what a model reaches for when making several edits -- was never
 * exempt. A run told exactly which file to change still refused to change it. */
#include "jc_test.h"
#include "jc_argpath.h"
#include "jc_json.h"

#include <string.h>

/* The collected pointers are BORROWED from the tree (the header says so), so a
 * caller that frees the tree before reading them gets freed memory -- which the
 * first draft of this file did, and the suite caught as two garbage strings.
 * Hence the split: `count()` for tests that only check how many, and an explicit
 * parse/compare/free for the tests that read the strings. */
static int count(const char *json, const char **out, int cap)
{
    cJSON *root = jc_json_parse(json);
    int n;
    JC_CHECK(root != NULL);
    n = jc_argpath_collect(root, out, cap);
    cJSON_Delete(root);
    return n;
}

static void test_top_level_path(void)
{
    const char *p[4];
    JC_CHECK(count("{\"path\":\"src/a.c\",\"content\":\"x\"}", p, 4) == 1);
    /* write_file, edit_file, generate_audio, generate_image, record_audio all
     * take this shape -- which is the point of not naming tools. */
    JC_CHECK(count("{\"path\":\"out.mp3\",\"text\":\"hi\"}", p, 4) == 1);
}

static void test_edits_array(void)
{
    const char *p[8];
    cJSON *root = jc_json_parse("{\"edits\":[{\"path\":\"a.c\"},"
                                "{\"path\":\"b.c\"}]}");
    JC_CHECK(root != NULL);
    JC_CHECK(jc_argpath_collect(root, p, 8) == 2);
    JC_CHECK_STR(p[0], "a.c");      /* order preserved: edits apply in order */
    JC_CHECK_STR(p[1], "b.c");
    cJSON_Delete(root);
}

static void test_both_shapes_at_once(void)
{
    const char *p[8];
    /* No shipped tool mixes them, but the collector must not silently drop one
     * if a future one does -- the exemption is all-or-nothing over ALL paths. */
    JC_CHECK(count("{\"path\":\"a.c\",\"edits\":[{\"path\":\"b.c\"}]}",
                     p, 8) == 2);
}

static void test_nothing_declared(void)
{
    const char *p[4];
    /* A shell command declares no path: it must NOT be exempt, so the collector
     * has to report zero rather than something the caller might read as "fine". */
    JC_CHECK(count("{\"command\":\"rm -rf /\"}", p, 4) == 0);
    JC_CHECK(count("{}", p, 4) == 0);
    JC_CHECK(count("{\"path\":\"\"}", p, 4) == 0);
    JC_CHECK(count("{\"path\":123}", p, 4) == 0);
    JC_CHECK(count("{\"edits\":\"not-an-array\"}", p, 4) == 0);
    JC_CHECK(count("{\"edits\":[{\"old_string\":\"x\"},\"junk\"]}", p, 4) == 0);
    JC_CHECK(jc_argpath_collect(NULL, p, 4) == 0);
}

static void test_overflow_fails_closed(void)
{
    const char *p[2];
    /* THE SAFETY PROPERTY. Three paths into room for two must report -1, not 2:
     * an exemption granted on a truncated view could permit a write to a path
     * the operator never scoped. The caller treats -1 as "not exempt". */
    JC_CHECK(count("{\"edits\":[{\"path\":\"a\"},{\"path\":\"b\"},"
                     "{\"path\":\"c\"}]}", p, 2) == -1);
    /* Exactly at the cap is fine -- the boundary is not off by one. */
    JC_CHECK(count("{\"edits\":[{\"path\":\"a\"},{\"path\":\"b\"}]}", p, 2) == 2);
    {
        const char *q[1];
        JC_CHECK(count("{\"path\":\"a\",\"edits\":[{\"path\":\"b\"}]}",
                         q, 1) == -1);
    }
    JC_CHECK(count("{\"path\":\"a\"}", p, 0) == 0);
}

void test_argpath(void)
{
    test_top_level_path();
    test_edits_array();
    test_both_shapes_at_once();
    test_nothing_declared();
    test_overflow_fails_closed();
}
