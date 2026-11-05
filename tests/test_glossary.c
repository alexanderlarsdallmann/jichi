/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_glossary.c - glossary loading (M35c). */

#include "jc_test.h"
#include "jc_snprintf.h"
#include "jc_glossary.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void wf(const char *path, const char *s)
{
    jc_status st = jc_write_file(path, s, strlen(s));
    JC_REQUIRE(st == JC_OK);
}

void test_glossary(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    const char *dir = jc_test_tmp("jichi_glossary_test");
    /* Held in a local, not re-fetched: the removals below must clean the
     * SAME directory the C side uses. A leftover literal /tmp here survived the
     * M457 conversion because it sits MID-STRING inside a shell command rather
     * than at a quote boundary -- and on Termux, where /tmp is not writable by
     * an app uid, `rm -rf /tmp/...` failed, the `&&` short-circuited, and the
     * mkdir that the whole test depends on never ran (M459). Since M728 there
     * is no shell here at all: jc_test_rm_rf removes, jc_mkdir_p creates. */
    char ghome[512];
    char path[600];

    memset(&app, 0, sizeof(app));
    app.arena = a;

    /* Isolate the global location (HOME/.config/...) so the host's real
     * glossary can't influence the test. */
    jc_snprintf(ghome, sizeof ghome, "%s", jc_test_tmp("jichi_glossary_home"));
    setenv("HOME", ghome, 1);
    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(ghome);
    jc_snprintf(path, sizeof path, "%s/.jichi", dir);
    JC_CHECK(jc_mkdir_p(path) == JC_OK);
    strcpy(app.cwd, dir);

    /* No file yet => NULL. */
    JC_CHECK(jc_glossary_load(&app) == NULL);

    /* A project glossary loads. */
    jc_snprintf(path, sizeof path, "%s/.jichi/glossary.md", dir);
    {
        FILE *f = fopen(path, "wb");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            fputs("# Glossary\n\n- **Widget**: a frobnicator unit\n", f);
            fclose(f);
        }
    }
    {
        char *g = jc_glossary_load(&app);
        if (JC_REQUIRE(g != NULL)) { /* a guard (M729) */
            JC_CHECK(g != NULL && strstr(g, "frobnicator") != NULL);
        }
    }

    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(ghome);
    jc_arena_free(a);
}

/* M992: symlink escape prevention for glossary -- a .jichi/glossary.md that is
 * a symlink to an outside file must NOT load the outside bytes. This is the
 * red-first test for D-34 glossary path. Before the fix, the loader follows the
 * symlink and the outside marker appears in the glossary. After the fix, the
 * file is skipped because its resolved target is outside the workspace root. */
void test_glossary_symlink_escape(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    char *g;
    const char *symlink_home;
    const char *symlink_ws;
    const char *outside_path;
    char ghome[512];
    char dir[512];
    char outside[512];

    memset(&app, 0, sizeof(app));
    app.arena = a;

    symlink_home = jc_test_tmp("jichi_glossary_symlink_home");
    jc_snprintf(ghome, sizeof ghome, "%s", symlink_home);
    setenv("HOME", ghome, 1);
    symlink_ws = jc_test_tmp("jichi_glossary_symlink_ws");
    jc_snprintf(dir, sizeof dir, "%s", symlink_ws);
    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(ghome);
    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_symlink_outside"));
    jc_snprintf(dir, sizeof dir, "%s/.jichi", symlink_ws);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    strcpy(app.cwd, symlink_ws);

    /* Create an outside marker file */
    outside_path = jc_test_tmp("jichi_glossary_symlink_outside");
    JC_REQUIRE(jc_mkdir_p(outside_path) == JC_OK);
    JC_REQUIRE(jc_is_dir(outside_path));
    jc_snprintf(outside, sizeof outside, "%s/outside_gloss.md", outside_path);
    wf(outside, "OUTSIDE_GLOSS_M992\n");
    /* Precondition: verify the outside marker file was written correctly */
    {
        FILE *f = fopen(outside, "rb");
        char buf[64];
        memset(buf, 0, sizeof(buf));
        JC_REQUIRE(f != NULL);
        JC_REQUIRE(f && fread(buf, 1, 18, f) == 18 && strstr(buf, "OUTSIDE_GLOSS_M992"));
        if (f) fclose(f);
    }

    /* Symlink .jichi/glossary.md to the outside file */
    jc_snprintf(dir, sizeof dir, "%s/.jichi/glossary.md", symlink_ws);
    /* First ensure the path doesn't exist */
    (void)unlink(dir);
    /* Precondition: the symlink must exist, and point at the OUTSIDE marker
     * file (the guarded path must be able to run). */
    if (!JC_REQUIRE(symlink(outside, dir) == 0)) {
        (void)jc_test_rm_rf(symlink_ws);
        (void)jc_test_rm_rf(symlink_home);
        jc_arena_free(a);
        return;
    }

    /* Global glossary in isolated HOME (control) */
    jc_snprintf(ghome, sizeof ghome, "%s/.config/jichi", symlink_home);
    JC_CHECK(jc_mkdir_p(ghome) == JC_OK);
    jc_snprintf(ghome, sizeof ghome, "%s/.config/jichi/glossary.md",
                symlink_home);
    wf(ghome, "GLOBAL_GLOSS_M992\n");

    /* Load glossary -- should NOT contain the outside marker */
    g = jc_glossary_load(&app);
    if (JC_REQUIRE(g != NULL)) {
        /* The symlink target outside the root was refused */
        JC_CHECK(strstr(g, "OUTSIDE_GLOSS_M992") == NULL);
        /* Global still loads (control) */
        JC_CHECK(strstr(g, "GLOBAL_GLOSS_M992") != NULL);
    }

    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_symlink_ws"));
    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_symlink_home"));
    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_symlink_outside"));
    jc_arena_free(a);
}

/* M992: control case -- a REAL .jichi/glossary.md should still load. */
void test_glossary_symlink_control(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    char *g;
    const char *symlink_ctrl_home;
    const char *symlink_ctrl_ws;
    char dir[512];
    char ghome[512];

    memset(&app, 0, sizeof(app));
    app.arena = a;

    symlink_ctrl_home = jc_test_tmp("jichi_glossary_symlink_ctrl_home");
    jc_snprintf(ghome, sizeof ghome, "%s", symlink_ctrl_home);
    setenv("HOME", ghome, 1);
    symlink_ctrl_ws = jc_test_tmp("jichi_glossary_symlink_ctrl_ws");
    jc_snprintf(dir, sizeof dir, "%s", symlink_ctrl_ws);
    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(ghome);
    jc_snprintf(dir, sizeof dir, "%s/.jichi", symlink_ctrl_ws);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    strcpy(app.cwd, symlink_ctrl_ws);

    /* REAL file (not symlink) with inside marker */
    jc_snprintf(dir, sizeof dir, "%s/.jichi/glossary.md", symlink_ctrl_ws);
    wf(dir, "INSIDE_GLOSS_M992\n");

    /* Global glossary in isolated HOME (control) */
    jc_snprintf(ghome, sizeof ghome, "%s/.config/jichi", symlink_ctrl_home);
    JC_CHECK(jc_mkdir_p(ghome) == JC_OK);
    jc_snprintf(ghome, sizeof ghome, "%s/.config/jichi/glossary.md",
                symlink_ctrl_home);
    wf(ghome, "GLOBAL_GLOSS_CTRL_M992\n");

    /* Load glossary -- should contain the inside marker */
    g = jc_glossary_load(&app);
    if (JC_REQUIRE(g != NULL)) {
        /* The real file IS loaded */
        JC_CHECK(strstr(g, "INSIDE_GLOSS_M992") != NULL);
        /* Global still loads (control) */
        JC_CHECK(strstr(g, "GLOBAL_GLOSS_CTRL_M992") != NULL);
    }

    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_symlink_ctrl_ws"));
    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_symlink_ctrl_home"));
    jc_arena_free(a);
}

/* M1109-b (D-35, hunk-audit run 0113 F2): pin the glossary TAIL read. M1109
 * (acc21124) made append_file read only the trailing JC_GLOSSARY_MAX (8192)
 * bytes of each glossary file; F2 found that NO test ever exercised a
 * glossary file over 8192 bytes, so a HEAD/TAIL swap at the jc_read_file_bounded
 * call site (last arg 0 -> 1) would be undetectable. Every existing fixture
 * is ~30-45 bytes, so the tail-read branch (want = max, seek > 0) never ran.
 *
 * Project-only: the global glossary is left absent, so combined == project and
 * the project tail read is isolated. The file is 10032 bytes: a 17-byte START
 * marker at byte 0, 10000 'x' filler, and a 15-byte END marker at the tail.
 * The last 8192 bytes begin at offset 1840, so the START marker is outside
 * them while the END marker sits inside. With the correct TAIL read
 * (head=0), the loader's combined-buffer cut keeps the last 8192 bytes ->
 * end present, start absent (green). If the site regressed to a HEAD read
 * (head=1) -- or the whole-file read were mishandled by the combined cut --
 * the start would appear / the end would vanish and this check goes red. */
void test_glossary_tail_cut(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    char *g;
    const char *tail_home;
    const char *tail_ws;
    char ghome[512];
    char dir[512];
    static char buf[10033]; /* 17 + 10000 + 15 = 10032 bytes, NUL-terminated */

    memset(&app, 0, sizeof(app));
    app.arena = a;

    tail_home = jc_test_tmp("jichi_glossary_tail_home");
    jc_snprintf(ghome, sizeof ghome, "%s", tail_home);
    setenv("HOME", ghome, 1);
    tail_ws = jc_test_tmp("jichi_glossary_tail_ws");
    jc_snprintf(dir, sizeof dir, "%s", tail_ws);
    (void)jc_test_rm_rf(dir);
    (void)jc_test_rm_rf(ghome);
    jc_snprintf(dir, sizeof dir, "%s/.jichi", tail_ws);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    strcpy(app.cwd, tail_ws);

    /* Project glossary > JC_GLOSSARY_MAX, START marker at byte 0, END marker
     * at the tail; global glossary deliberately absent. */
    memset(buf, 'x', sizeof(buf));
    memcpy(buf, "GLOSSTART_MARKER_", 17);
    memcpy(buf + 17 + 10000, "GLOSEND_MARKER_", 15);
    buf[17 + 10000 + 15] = '\0';
    jc_snprintf(dir, sizeof dir, "%s/.jichi/glossary.md", tail_ws);
    wf(dir, buf);

    g = jc_glossary_load(&app);
    if (JC_REQUIRE(g != NULL)) {
        /* The TAIL (kept) has the end marker. */
        JC_CHECK(strstr(g, "GLOSEND_MARKER_") != NULL);
        /* The HEAD (not kept) must be absent. */
        JC_CHECK(strstr(g, "GLOSSTART_MARKER_") == NULL);
    }

    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_tail_ws"));
    (void)jc_test_rm_rf(jc_test_tmp("jichi_glossary_tail_home"));
    jc_arena_free(a);
}
