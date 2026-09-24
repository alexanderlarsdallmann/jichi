/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_harness.c - the harness's own fence: jc_test_rm_rf (M728).
 *
 * The suite removes its fixtures through jc_test_rm_rf, which refuses anything
 * that is not strictly below the fixture directory. It replaced twenty-five
 * `rm -rf %s` command lines, one of which removed the whole TMPDIR when that
 * path was 127 characters long. This file holds the rule itself (the pure
 * predicate) and the walk (a real tree, with a link out of it that must not
 * be followed). It runs first, before any test depends on the helper. */

#include "jc_test.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

static int exists(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

static void test_rm_rf_refuses(void)
{
    const char *td = jc_test_tmpdir();
    size_t tl = strlen(td);
    char p[1024];

    while (tl > 1 && td[tl - 1] == '/') {
        tl--;
    }
    JC_CHECK(jc_test_rm_rf_allowed(NULL) == 0);
    /* The fixture directory itself, however it is spelled. */
    JC_CHECK(jc_test_rm_rf_allowed(td) == 0);
    jc_snprintf(p, sizeof p, "%s/", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    jc_snprintf(p, sizeof p, "%s//", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    /* What a buffer cut short yields: a PREFIX of the fixture directory. */
    if (tl > 2) {
        memcpy(p, td, tl - 1);
        p[tl - 1] = '\0';
        JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    }
    /* Its parent, when it has one. */
    {
        const char *slash = NULL;
        size_t i;
        for (i = 0; i < tl; i++) {
            if (td[i] == '/') {
                slash = td + i;
            }
        }
        if (slash != NULL && slash > td) {
            memcpy(p, td, (size_t)(slash - td));
            p[slash - td] = '\0';
            JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
        }
    }
    /* A sibling that merely shares the prefix ("/tmpx" for "/tmp"). */
    jc_snprintf(p, sizeof p, "%sx", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    /* A step, which could climb back out. */
    jc_snprintf(p, sizeof p, "%s/..", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    jc_snprintf(p, sizeof p, "%s/jichi_x/../..", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    jc_snprintf(p, sizeof p, "%s/./jichi_x", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 0);
    JC_CHECK(jc_test_rm_rf_allowed("/") == 0);

    /* And what it must allow, or no fixture could be removed at all. */
    jc_snprintf(p, sizeof p, "%s/jichi_x", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 1);
    jc_snprintf(p, sizeof p, "%s//jichi_x/y", td);
    JC_CHECK(jc_test_rm_rf_allowed(p) == 1);
    jc_snprintf(p, sizeof p, "%s/..jichi_hidden", td); /* a name, not a step */
    JC_CHECK(jc_test_rm_rf_allowed(p) == 1);
    JC_CHECK(jc_test_rm_rf_allowed(jc_test_tmp("jichi_harness_x")) == 1);
}

static void test_rm_rf_removes(void)
{
    char dir[1024];
    char keep[1024];
    char p[1200];
    FILE *f;

    jc_snprintf(dir, sizeof dir, "%s", jc_test_tmp("jichi_harness_rmrf"));
    jc_snprintf(keep, sizeof keep, "%s", jc_test_tmp("jichi_harness_keep"));
    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(keep);

    jc_snprintf(p, sizeof p, "%s/a/b", dir);
    if (!JC_REQUIRE(jc_mkdir_p(p) == JC_OK)) {
        return;
    }
    jc_snprintf(p, sizeof p, "%s/a/b/f.txt", dir);
    f = fopen(p, "wb");
    if (f != NULL) {
        fputs("x", f);
        fclose(f);
    }
    JC_CHECK(jc_mkdir_p(keep) == JC_OK);
    jc_snprintf(p, sizeof p, "%s/keep.txt", keep);
    f = fopen(p, "wb");
    if (f != NULL) {
        fputs("keep", f);
        fclose(f);
    }
    /* A link out of the tree, to a directory that must survive the removal.
     * Where symlinks are unsupported the rest still runs. */
    jc_snprintf(p, sizeof p, "%s/a/out", dir);
    (void)symlink(keep, p);

    JC_CHECK(jc_test_rm_rf(dir) == 0);
    JC_CHECK(!exists(dir));
    jc_snprintf(p, sizeof p, "%s/keep.txt", keep);
    JC_CHECK(exists(p));                  /* the link was not followed */
    JC_CHECK(jc_test_rm_rf(dir) == 0);    /* an absent path is removed */
    JC_CHECK(jc_test_rm_rf(keep) == 0);
    JC_CHECK(!exists(keep));
}

void test_harness(void)
{
    test_rm_rf_refuses();
    test_rm_rf_removes();
}
