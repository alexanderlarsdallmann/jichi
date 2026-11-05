/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_walk.c - unit tests for the shared walker (D-27/D-28/D-29/D-30). */

#include "jc_test.h"
#include "jc_walk.h"
#include "jc_snprintf.h"
#include "jc_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>

/* A simple fixture builder: remove a tree. */
static int rm_tree(const char *base)
{
    char path[2048];
    struct stat st;
    DIR *d;
    struct dirent *de;

    if (lstat(base, &st) != 0) {
        return 0;  /* already gone */
    }
    if (S_ISDIR(st.st_mode)) {
        d = opendir(base);
        if (!d) {
            return -1;
        }
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                continue;
            }
            jc_snprintf(path, sizeof(path), "%s/%s", base, de->d_name);
            if (rm_tree(path) != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
        return rmdir(base);
    }
    return unlink(base);
}

/* Helper: restore cwd or fail. A bare chdir() ignores the return value, which
 * host gcc 13.3 warns on with -Werror=unused-result. If restore fails, every
 * later test runs in the wrong directory -- record it and stop. */
static void restore_cwd_or_fail(const char *oldcwd)
{
    if (chdir(oldcwd) != 0) {
        /* If the restore fails, every later test runs in the wrong
         * directory -- record it and stop, do not keep going. */
        JC_CHECK(0);
    }
}

static void test_walk_ctx_init(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    char path[2048];
    const char *abs_tmpdir;

    /* Create a temporary root directory */
    abs_tmpdir = jc_test_tmp("test_walk_ctx");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Add a nested directory */
    jc_snprintf(path, sizeof(path), "%s/subdir", tmpdir);
    (void)jc_test_rm_rf(path);
    if (!JC_REQUIRE(mkdir(path, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Basic init */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }
    /* realpath may add trailing slashes or resolve differently, so check prefix */
    JC_CHECK(strncmp(ctx.root_canon, tmpdir, strlen(tmpdir)) == 0);
    JC_CHECK(ctx.max_depth == 10);
    JC_CHECK(ctx.max_entries == 1000);
    JC_CHECK(ctx.depth == 0);
    JC_CHECK(ctx.n_entries == 0);
    JC_CHECK(ctx.capped == 0);
    JC_CHECK(ctx.skipped_outside == 0);
    /* Root is no longer seeded in visited - classify adds it on first encounter.
     * This allows classify to properly detect hardlink loops. */
    JC_CHECK(ctx.visited.len == 0);
    jc_walk_ctx_free(&ctx);

    /* Non-existent root should fail */
    JC_CHECK(jc_walk_ctx_init(&ctx, "/nonexistent/path", 10, 1000) == -1);

    /* A file, not a directory, should fail */
    {
        char file[2048];
        FILE *fp;
        jc_snprintf(file, sizeof(file), "%s/testfile", tmpdir);
        fp = fopen(file, "wb");
        if (fp) {
            fprintf(fp, "test\n");
            fclose(fp);
            JC_CHECK(jc_walk_ctx_init(&ctx, file, 10, 1000) == -1);
            (void)remove(file);
        }
    }

    /* Cleanup */
    rm_tree(tmpdir);
}

static void test_walk_classify_basic(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    char dir1[2048];
    char file1[2048];
    enum jc_walk_kind k;
    const char *abs_tmpdir;

    /* Create a temporary root directory */
    abs_tmpdir = jc_test_tmp("test_walk_classify");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create subdirectory and file */
    jc_snprintf(dir1, sizeof(dir1), "%s/dir1", tmpdir);
    (void)jc_test_rm_rf(dir1);
    if (!JC_REQUIRE(mkdir(dir1, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }
    jc_snprintf(file1, sizeof(file1), "%s/file1.c", tmpdir);
    {
        FILE *fp = fopen(file1, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            return;
        }
        fprintf(fp, "int x = 1;\n");
        fclose(fp);
    }

    /* Init context */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Classify the root dir - should be JC_WALK_DIR */
    k = jc_walk_classify(&ctx, tmpdir, 0);
    JC_CHECK(k == JC_WALK_DIR);

    /* Classify a real subdirectory */
    k = jc_walk_classify(&ctx, dir1, 1);
    JC_CHECK(k == JC_WALK_DIR);

    /* Classify a real file */
    k = jc_walk_classify(&ctx, file1, 1);
    JC_CHECK(k == JC_WALK_FILE);

    /* Classify a non-existent file */
    k = jc_walk_classify(&ctx, "/nonexistent", 1);
    JC_CHECK(k == JC_WALK_SKIP);

    jc_walk_ctx_free(&ctx);
    rm_tree(tmpdir);
}

static void test_walk_outside_link(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    char outside_dir[2048];
    char link_path[2048];
    const char *abs_tmpdir;
    const char *abs_outside;
    enum jc_walk_kind k;

    /* Create a temporary workspace root */
    abs_tmpdir = jc_test_tmp("test_walk_outside");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create an outside directory */
    abs_outside = jc_test_tmp("test_walk_outside_secret");
    jc_snprintf(outside_dir, sizeof(outside_dir), "%s", abs_outside);
    (void)jc_test_rm_rf(outside_dir);
    if (!JC_REQUIRE(mkdir(outside_dir, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }
    {
        char outside_file[2048];
        FILE *fp;
        jc_snprintf(outside_file, sizeof(outside_file), "%s/secret.txt", outside_dir);
        fp = fopen(outside_file, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            (void)jc_test_rm_rf(outside_dir);
            return;
        }
        fprintf(fp, "OUTSIDE_SECRET\n");
        fclose(fp);
    }

    /* Create a symlink in the workspace pointing outside */
    jc_snprintf(link_path, sizeof(link_path), "%s/outside_link", tmpdir);
    if (!JC_REQUIRE(symlink(outside_dir, link_path) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        (void)jc_test_rm_rf(outside_dir);
        return;
    }

    /* Init context */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        (void)jc_test_rm_rf(outside_dir);
        return;
    }

    /* The link should be skipped because it points outside */
    k = jc_walk_classify(&ctx, link_path, 1);
    JC_CHECK(k == JC_WALK_SKIP);
    JC_CHECK(ctx.skipped_outside == 1);

    /* The directory it points to should also be skipped */
    k = jc_walk_classify(&ctx, outside_dir, 1);
    JC_CHECK(k == JC_WALK_SKIP);
    JC_CHECK(ctx.skipped_outside == 2);  /* should count the direct out-of-root case too */

    jc_walk_ctx_free(&ctx);
    rm_tree(tmpdir);
    rm_tree(outside_dir);
}

static void test_walk_loop_link(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    const char *abs_tmpdir;
    char loop_link[2048];
    enum jc_walk_kind k;

    /* Create a temporary workspace root */
    abs_tmpdir = jc_test_tmp("test_walk_loop");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create a symlink that points to the root (loop) */
    jc_snprintf(loop_link, sizeof(loop_link), "%s/loop", tmpdir);
    if (!JC_REQUIRE(symlink(tmpdir, loop_link) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Init context */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* First classification of loop - should be JC_WALK_DIR */
    k = jc_walk_classify(&ctx, loop_link, 1);
    JC_CHECK(k == JC_WALK_DIR);

    /* Second classification - should be JC_WALK_SKIP (already visited) */
    k = jc_walk_classify(&ctx, loop_link, 1);
    JC_CHECK(k == JC_WALK_SKIP);

    jc_walk_ctx_free(&ctx);
    rm_tree(tmpdir);
}

static void test_walk_depth_bound(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    const char *abs_tmpdir;
    char deep[2048];
    char level5fresh[2048];
    char level6fresh[2048];
    int i;
    enum jc_walk_kind k;

    /* Create a temporary workspace root */
    abs_tmpdir = jc_test_tmp("test_walk_depth");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create a deep chain of directories */
    jc_snprintf(deep, sizeof(deep), "%s", tmpdir);
    for (i = 0; i < 50; i++) {
        char next[2048];
        jc_snprintf(next, sizeof(next), "%s/level%d", deep, i);
        (void)jc_test_rm_rf(next);
        if (!JC_REQUIRE(mkdir(next, 0755) == 0)) {
            (void)jc_test_rm_rf(tmpdir);
            return;
        }
        jc_snprintf(deep, sizeof(deep), "%s", next);
    }

    /* Also create fresh dirs at depths 5 and 6 for bound testing */
    jc_snprintf(level5fresh, sizeof(level5fresh), "%s/level5fresh", tmpdir);
    if (!JC_REQUIRE(mkdir(level5fresh, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }
    jc_snprintf(level6fresh, sizeof(level6fresh), "%s/level6fresh", tmpdir);
    if (!JC_REQUIRE(mkdir(level6fresh, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Init context with a shallow depth bound */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 5, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Within bound - should be a directory */
    k = jc_walk_classify(&ctx, tmpdir, 0);
    JC_CHECK(k == JC_WALK_DIR);

    /* Within bound - should be a directory */
    k = jc_walk_classify(&ctx, deep, 4);
    JC_CHECK(k == JC_WALK_DIR);

    /* At the bound - should be skipped (using a fresh dir not yet classified) */
    k = jc_walk_classify(&ctx, level5fresh, 5);
    JC_CHECK(k == JC_WALK_SKIP);

    /* Beyond the bound - should also be skipped (using a fresh dir not yet classified) */
    k = jc_walk_classify(&ctx, level6fresh, 6);
    JC_CHECK(k == JC_WALK_SKIP);

    jc_walk_ctx_free(&ctx);
    rm_tree(tmpdir);
}

static void test_walk_entries_bound(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    const char *abs_tmpdir;
    char file[2048];
    enum jc_walk_kind k;

    /* Create a temporary workspace root */
    abs_tmpdir = jc_test_tmp("test_walk_entries");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create test files */
    {
        char file_path[2048];
        FILE *fp;
        jc_snprintf(file_path, sizeof(file_path), "%s/a.c", tmpdir);
        fp = fopen(file_path, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            return;
        }
        fprintf(fp, "int a = 1;\n");
        fclose(fp);
        jc_snprintf(file_path, sizeof(file_path), "%s/b.c", tmpdir);
        fp = fopen(file_path, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            return;
        }
        fprintf(fp, "int b = 2;\n");
        fclose(fp);
        jc_snprintf(file_path, sizeof(file_path), "%s/c.c", tmpdir);
        fp = fopen(file_path, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            return;
        }
        fprintf(fp, "int c = 3;\n");
        fclose(fp);
    }

    /* Init context with a small entry budget */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 3) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* First entry - OK */
    jc_snprintf(file, sizeof(file), "%s/a.c", tmpdir);
    k = jc_walk_classify(&ctx, file, 0);
    JC_CHECK(k == JC_WALK_FILE);

    /* Second entry - OK */
    jc_snprintf(file, sizeof(file), "%s/b.c", tmpdir);
    k = jc_walk_classify(&ctx, file, 0);
    JC_CHECK(k == JC_WALK_FILE);

    /* Third entry - OK */
    jc_snprintf(file, sizeof(file), "%s/c.c", tmpdir);
    k = jc_walk_classify(&ctx, file, 0);
    JC_CHECK(k == JC_WALK_FILE);

    /* Fourth entry - should be capped */
    k = jc_walk_classify(&ctx, file, 0);
    JC_CHECK(k == JC_WALK_SKIP);
    JC_CHECK(ctx.capped == 1);

    jc_walk_ctx_free(&ctx);
    rm_tree(tmpdir);
}

static void test_walk_fifo_broken_link(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    const char *abs_tmpdir;
    char fifo_path[2048];
    char broken_link[2048];
    enum jc_walk_kind k;

    /* Create a temporary workspace root */
    abs_tmpdir = jc_test_tmp("test_walk_special");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create a FIFO */
    jc_snprintf(fifo_path, sizeof(fifo_path), "%s/pipe", tmpdir);
    if (!JC_REQUIRE(mkfifo(fifo_path, 0644) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Create a broken link */
    jc_snprintf(broken_link, sizeof(broken_link), "%s/broken", tmpdir);
    if (!JC_REQUIRE(symlink("/nonexistent/file", broken_link) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Init context */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* FIFO should be skipped */
    k = jc_walk_classify(&ctx, fifo_path, 1);
    JC_CHECK(k == JC_WALK_SKIP);

    /* Broken link should be skipped */
    k = jc_walk_classify(&ctx, broken_link, 1);
    JC_CHECK(k == JC_WALK_SKIP);

    jc_walk_ctx_free(&ctx);
    rm_tree(tmpdir);
}

/* Test for DEFECT A: relative walk roots break under_root (the reviewer's blocking finding, REPRODUCED by the lead).
 * jc_walk_ctx_init canonicalizes the root to an ABSOLUTE path (realpath, jc_walk.c ~line 90).
 * But jc_walk_classify's REAL-DIRECTORY branch runs under_root(ctx->root_canon, strlen(ctx->root_canon), full)
 * against the consumer-built `full`, which is RELATIVE when the consumer's starting dir is relative.
 * strncmp("src/sub", "/abs/cwd", ...) never matches, so every real subdirectory is misclassified JC_WALK_SKIP
 * and counted skipped_outside -- silently dropping nested files from list_files. */
static void test_walk_relative_root(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    char rootname[256];
    char subdir[2048];
    char subfile[2048];
    const char *abs_tmpdir;
    enum jc_walk_kind k;
    char oldcwd[2048];

    /* Create a temporary workspace root */
    abs_tmpdir = jc_test_tmp("test_walk_relative");
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s", abs_tmpdir);
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Add a subdirectory */
    jc_snprintf(subdir, sizeof(subdir), "%s/subdir", tmpdir);
    (void)jc_test_rm_rf(subdir);
    if (!JC_REQUIRE(mkdir(subdir, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Add a file in the subdirectory */
    jc_snprintf(subfile, sizeof(subfile), "%s/subfile.c", subdir);
    {
        FILE *fp = fopen(subfile, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            return;
        }
        fprintf(fp, "int x = 1;\n");
        fclose(fp);
    }

    /* Save current cwd and chdir into tmpdir so we can use relative paths */
    if (!JC_REQUIRE(getcwd(oldcwd, sizeof(oldcwd)) != NULL)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }
    if (!JC_REQUIRE(chdir(tmpdir) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Copy the relative root name */
    jc_snprintf(rootname, sizeof(rootname), "%s", ".");

    /* Init context with relative root */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, rootname, 10, 1000) == 0)) {
        restore_cwd_or_fail(oldcwd);
        (void)jc_test_rm_rf(tmpdir);
        return;
    }

    /* Classify the root (relative ".") - should be JC_WALK_DIR */
    k = jc_walk_classify(&ctx, ".", 0);
    JC_CHECK(k == JC_WALK_DIR);

    /* Classify the subdirectory (relative "subdir") - should be JC_WALK_DIR */
    k = jc_walk_classify(&ctx, "subdir", 1);
    JC_CHECK(k == JC_WALK_DIR);

    /* Check that no entries were skipped as outside (DEFECT A would cause skipped_outside != 0) */
    JC_CHECK(ctx.skipped_outside == 0);

    jc_walk_ctx_free(&ctx);
    restore_cwd_or_fail(oldcwd);
    rm_tree(tmpdir);
}

void test_walk(void)
{
    test_walk_relative_root();
    test_walk_ctx_init();
    test_walk_classify_basic();
    test_walk_outside_link();
    test_walk_loop_link();
    test_walk_depth_bound();
    test_walk_entries_bound();
    test_walk_fifo_broken_link();
}
