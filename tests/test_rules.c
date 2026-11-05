/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_rules.c - AGENTS.md discovery, ordering, dedup, and size cap. */

#include "jc_test.h"
#include "jc_rules.h"
#include "jc_app.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void wf(const char *path, const char *s)
{
    jc_status st = jc_write_file(path, s, strlen(s));
    JC_REQUIRE(st == JC_OK);
}

void test_rules(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    char *r;

    /* Temp tree: a global file, a git root with AGENTS.md, and a subdir. */
    setenv("HOME", jc_test_tmp("jichi_rules_test"), 1);
    jc_mkdir_p(jc_test_tmp("jichi_rules_test/.config/jichi"));
    wf(jc_test_tmp("jichi_rules_test/.config/jichi/AGENTS.md"), "GLOBAL RULE\n");
    jc_mkdir_p(jc_test_tmp("jichi_rules_test/proj/.git"));
    wf(jc_test_tmp("jichi_rules_test/proj/AGENTS.md"), "ROOT RULE\n");
    jc_mkdir_p(jc_test_tmp("jichi_rules_test/proj/sub"));
    wf(jc_test_tmp("jichi_rules_test/proj/sub/AGENTS.md"), "SUB RULE\n");
    wf(jc_test_tmp("jichi_rules_test/AGENTS.md"), "ABOVE THE ROOT\n");

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, jc_test_tmp("jichi_rules_test/proj/sub"));

    r = jc_rules_load(&app);
    JC_CHECK(r != NULL);
    if (r != NULL) {
        char *g = strstr(r, "GLOBAL RULE");
        char *ro = strstr(r, "ROOT RULE");
        char *su = strstr(r, "SUB RULE");
        JC_CHECK(g != NULL && ro != NULL && su != NULL);
        /* Order: global, then git root, then cwd. */
        JC_CHECK(g < ro && ro < su);
        /* The walk stops at the root: the directory above it is not read. */
        JC_CHECK(strstr(r, "ABOVE THE ROOT") == NULL);
    }

    /* M884: a linked worktree's `.git` is a FILE ("gitdir: ..."), and so is a
     * submodule's. The walk stopped only at a `.git` DIRECTORY, so from a
     * worktree it climbed past the repository and read the rules files above
     * it, ahead of the worktree's own: in this project's own worktree, a
     * template AGENTS.md two directories up, on every request. */
    {
        struct jc_app app4;
        char *r4;

        setenv("HOME", jc_test_tmp("jichi_rules_wt"), 1);
        jc_mkdir_p(jc_test_tmp("jichi_rules_wt/top/wt/sub"));
        wf(jc_test_tmp("jichi_rules_wt/top/AGENTS.md"), "ABOVE THE WORKTREE\n");
        wf(jc_test_tmp("jichi_rules_wt/top/wt/.git"),
           "gitdir: /nonexistent/.git/worktrees/wt\n");
        wf(jc_test_tmp("jichi_rules_wt/top/wt/CLAUDE.md"), "WORKTREE RULE\n");

        memset(&app4, 0, sizeof(app4));
        app4.arena = a;
        jc_snprintf(app4.cwd, sizeof(app4.cwd), "%s",
                    jc_test_tmp("jichi_rules_wt/top/wt/sub"));
        r4 = jc_rules_load(&app4);
        if (JC_REQUIRE(r4 != NULL)) {
            JC_CHECK(strstr(r4, "WORKTREE RULE") != NULL);
            JC_CHECK(strstr(r4, "ABOVE THE WORKTREE") == NULL);
        }
    }

    /* M992: symlink escape prevention -- a rules file that is a symlink to an
     * outside file must NOT load the outside bytes. This is the red-first test
     * for D-34. Before the fix, the loader follows the symlink and the outside
     * marker appears in the rules. After the fix, the file is skipped because
     * its resolved target is outside the workspace root. */
    {
        struct jc_app app6;
        char *r6;
        char outside[512];
        char ws6[512];

        /* Idempotent: clear any stale tree from a prior run first (supervisor 01:0x). */
        (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_symlink"));
        setenv("HOME", jc_test_tmp("jichi_rules_symlink"), 1);
        jc_mkdir_p(jc_test_tmp("jichi_rules_symlink/.config/jichi"));
        wf(jc_test_tmp("jichi_rules_symlink/.config/jichi/AGENTS.md"),
           "GLOBAL RULE M992\n");

        /* Create an isolated workspace with a .git (so the root is found) */
        jc_snprintf(ws6, sizeof(ws6), "%s",
                    jc_test_tmp("jichi_rules_symlink/ws6"));
        jc_mkdir_p(ws6);
        jc_snprintf(ws6, sizeof(ws6), "%s/.git",
                    jc_test_tmp("jichi_rules_symlink/ws6"));
        jc_mkdir_p(ws6);

        /* Create an outside marker file (outside the workspace) */
        {
            char outdir[512];
            const char *tmp_outside;
            jc_status mkdir_r;
            jc_status wf_st;
            tmp_outside = jc_test_tmp("jichi_rules_symlink/outside");
            jc_snprintf(outdir, sizeof(outdir), "%s", tmp_outside);
            mkdir_r = jc_mkdir_p(outdir);
            JC_REQUIRE(mkdir_r == JC_OK);
            JC_REQUIRE(jc_is_dir(outdir));
            jc_snprintf(outside, sizeof(outside), "%s/outside_rules.md", outdir);
            wf_st = jc_write_file(outside, "OUTSIDE_RULES_M992\n", 19);
            JC_REQUIRE(wf_st == JC_OK);
        }
        /* Precondition: verify the outside marker file was written correctly */
        {
            FILE *f = fopen(outside, "rb");
            char buf[64];
            memset(buf, 0, sizeof(buf));
            JC_REQUIRE(f != NULL);
            JC_REQUIRE(f && fread(buf, 1, 18, f) == 18 && strstr(buf, "OUTSIDE_RULES_M992"));
            if (f) fclose(f);
        }

        /* Symlink CLAUDE.md in workspace to the outside file */
        jc_snprintf(ws6, sizeof(ws6), "%s",
                    jc_test_tmp("jichi_rules_symlink/ws6"));
        /* First remove the file, then create symlink */
        {
            char linkto[1100];
            const char *linkpath;
            JC_REQUIRE(jc_mkdir_p(jc_test_tmp("jichi_rules_symlink/ws6")) == JC_OK);
            JC_REQUIRE(jc_is_dir(jc_test_tmp("jichi_rules_symlink/ws6")));
            jc_snprintf(linkto, sizeof(linkto), "%s/outside_rules.md",
                        jc_test_tmp("jichi_rules_symlink/outside"));
            linkpath = jc_test_tmp("jichi_rules_symlink/ws6/CLAUDE.md");
            unlink(linkpath);
            /* Precondition: the symlink must exist, and point at the OUTSIDE
             * marker file (the guarded path must be able to run). */
            if (!JC_REQUIRE(symlink(linkto, linkpath) == 0)) {
                (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_symlink"));
                return;
            }
        }

        memset(&app6, 0, sizeof(app6));
        app6.arena = a;
        strcpy(app6.cwd, jc_test_tmp("jichi_rules_symlink/ws6"));
        r6 = jc_rules_load(&app6);
        if (JC_REQUIRE(r6 != NULL)) {
            /* The symlink target outside the root was refused */
            JC_CHECK(strstr(r6, "OUTSIDE_RULES_M992") == NULL);
            /* Global still loads (control) */
            JC_CHECK(strstr(r6, "GLOBAL RULE M992") != NULL);
        }
        (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_symlink"));
    }

    /* M992: control case -- a REAL file (not a symlink) in the workspace
     * should still load. This proves the guard does not over-block normal files. */
    {
        struct jc_app app7;
        char *r7;
        char ws7[512];

        /* Idempotent: clear any stale tree from a prior run first (supervisor 01:0x). */
        (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_symlink_control"));
        setenv("HOME", jc_test_tmp("jichi_rules_symlink_control"), 1);
        jc_mkdir_p(jc_test_tmp("jichi_rules_symlink_control/.config/jichi"));
        wf(jc_test_tmp("jichi_rules_symlink_control/.config/jichi/AGENTS.md"),
           "GLOBAL CONTROL M992\n");

        /* Create an isolated workspace with a .git */
        jc_snprintf(ws7, sizeof(ws7), "%s",
                    jc_test_tmp("jichi_rules_symlink_control/ws7"));
        jc_mkdir_p(ws7);
        jc_snprintf(ws7, sizeof(ws7), "%s/.git",
                    jc_test_tmp("jichi_rules_symlink_control/ws7"));
        jc_mkdir_p(ws7);

        /* REAL file (not symlink) with an inside marker */
        jc_snprintf(ws7, sizeof(ws7), "%s/CLAUDE.md",
                    jc_test_tmp("jichi_rules_symlink_control/ws7"));
        wf(ws7, "INSIDE_RULES_M992\n");

        memset(&app7, 0, sizeof(app7));
        app7.arena = a;
        strcpy(app7.cwd, jc_test_tmp("jichi_rules_symlink_control/ws7"));
        r7 = jc_rules_load(&app7);
        if (JC_REQUIRE(r7 != NULL)) {
            /* The real file IS loaded */
            JC_CHECK(strstr(r7, "INSIDE_RULES_M992") != NULL);
            /* Global still loads (control) */
            JC_CHECK(strstr(r7, "GLOBAL CONTROL M992") != NULL);
        }
        (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_symlink_control"));
    }

    /* M1013: no-repo workspace must read REGULAR parent-directory rules files.
     * The M992 guard must not change which regular files the walk reads; it should
     * only block SYMLINKED files that escape. In a no-repo case, wsroot = realpath(cwd),
     * but the documented walk climbs to read parent directories. */
    {
        struct jc_app app8;
        char *r8;
        char ws8[512];
        char marker[64];

        /* Build marker by arithmetic so a match can only come from a parent file. */
        jc_snprintf(marker, sizeof(marker), "PARENT-%d", 70 + 3);

        /* Idempotent: clear any stale tree from a prior run first. */
        (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_walk_no_repo"));
        setenv("HOME", jc_test_tmp("jichi_rules_walk_no_repo"), 1);
        jc_mkdir_p(jc_test_tmp("jichi_rules_walk_no_repo/.config/jichi"));
        wf(jc_test_tmp("jichi_rules_walk_no_repo/.config/jichi/AGENTS.md"),
           "GLOBAL M1013\n");

        /* Create a no-repo workspace: cwd = top/sub, parent top/AGENTS.md with marker */
        jc_snprintf(ws8, sizeof(ws8), "%s",
                    jc_test_tmp("jichi_rules_walk_no_repo/top"));
        jc_mkdir_p(ws8);
        wf(jc_test_tmp("jichi_rules_walk_no_repo/top/AGENTS.md"), marker);

        jc_snprintf(ws8, sizeof(ws8), "%s/sub",
                    jc_test_tmp("jichi_rules_walk_no_repo/top"));
        jc_mkdir_p(ws8);

        memset(&app8, 0, sizeof(app8));
        app8.arena = a;
        strcpy(app8.cwd, ws8);
        r8 = jc_rules_load(&app8);
        if (JC_REQUIRE(r8 != NULL)) {
            /* The parent regular file must be read (regression M1013) */
            JC_CHECK(strstr(r8, marker) != NULL);
            /* Global still loads (control) */
            JC_CHECK(strstr(r8, "GLOBAL M1013") != NULL);
        }
        (void)jc_test_rm_rf(jc_test_tmp("jichi_rules_walk_no_repo"));
    }

    /* M884, the same test seen from a submodule: its `.git` is a file inside a
     * superproject whose `.git` is a directory. The walk used to run on to the
     * superproject's root and read its rules; a submodule is a repository of its
     * own, and `git rev-parse --show-toplevel` names its directory too. */
    {
        struct jc_app app5;
        char *r5;

        setenv("HOME", jc_test_tmp("jichi_rules_sm"), 1);
        jc_mkdir_p(jc_test_tmp("jichi_rules_sm/super/.git"));
        jc_mkdir_p(jc_test_tmp("jichi_rules_sm/super/lib"));
        wf(jc_test_tmp("jichi_rules_sm/super/AGENTS.md"), "SUPERPROJECT RULE\n");
        wf(jc_test_tmp("jichi_rules_sm/super/lib/.git"),
           "gitdir: ../.git/modules/lib\n");
        wf(jc_test_tmp("jichi_rules_sm/super/lib/AGENTS.md"), "SUBMODULE RULE\n");

        memset(&app5, 0, sizeof(app5));
        app5.arena = a;
        jc_snprintf(app5.cwd, sizeof(app5.cwd), "%s",
                    jc_test_tmp("jichi_rules_sm/super/lib"));
        r5 = jc_rules_load(&app5);
        if (JC_REQUIRE(r5 != NULL)) {
            JC_CHECK(strstr(r5, "SUBMODULE RULE") != NULL);
            JC_CHECK(strstr(r5, "SUPERPROJECT RULE") == NULL);
        }
    }

    /* Size cap: an oversized rules file is truncated. */
    {
        struct jc_app app2;
        char *big;
        jc_size n = 40000;
        jc_size i;
        char *r2;

        setenv("HOME", jc_test_tmp("jichi_rules_cap"), 1);
        jc_mkdir_p(jc_test_tmp("jichi_rules_cap/p/.git"));
        big = (char *)malloc(n + 1);
        for (i = 0; i < n; i++) {
            big[i] = 'x';
        }
        big[n] = '\0';
        jc_write_file(jc_test_tmp("jichi_rules_cap/p/AGENTS.md"), big, n);
        free(big);

        memset(&app2, 0, sizeof(app2));
        app2.arena = a;
        strcpy(app2.cwd, jc_test_tmp("jichi_rules_cap/p"));
        r2 = jc_rules_load(&app2);
        JC_CHECK(r2 != NULL && strstr(r2, "[rules truncated]") != NULL);
    }

    /* M516: the cap must not split a multi-byte character. A byte-exact cut
     * left `\xe2\x80` -- two bytes of an em dash -- at the end of the rules
     * block on EVERY request in this repository, which the provider then
     * replaced with U+FFFD and warned about once per call. Found by a
     * self-hosting review run printing that warning six times
     * (docs/analysis/2026-08-21-self-hosting-first-review.md).
     *
     * Three pad lengths, because where the cut lands depends on the bytes the
     * header and any earlier file contributed: with an all-em-dash body, at
     * least one of pad 0/1/2 must put the cut mid-character, so the trio is
     * deterministic where any single one would be luck. */
    {
        jc_size pad;
        for (pad = 0; pad < 3; pad++) {
            struct jc_app app3;
            char *body;
            char *r3;
            jc_size n = 40000;
            jc_size i;
            char dir[512];
            char file[600];

            jc_snprintf(dir, sizeof(dir), "%s%lu",
                        jc_test_tmp("jichi_rules_utf8_"), (unsigned long)pad);
            setenv("HOME", dir, 1);
            jc_snprintf(file, sizeof(file), "%s/p/.git", dir);
            jc_mkdir_p(file);
            body = (char *)malloc(n + 4);
            for (i = 0; i < pad; i++) {
                body[i] = 'x';
            }
            /* U+2014 EM DASH, three bytes, repeated to the end. */
            for (i = pad; i + 2 < n; i += 3) {
                body[i] = (char)0xe2; body[i + 1] = (char)0x80;
                body[i + 2] = (char)0x94;
            }
            jc_snprintf(file, sizeof(file), "%s/p/AGENTS.md", dir);
            jc_write_file(file, body, i);
            free(body);

            memset(&app3, 0, sizeof(app3));
            app3.arena = a;
            jc_snprintf(app3.cwd, sizeof(app3.cwd), "%s/p", dir);
            r3 = jc_rules_load(&app3);
            if (JC_REQUIRE(r3 != NULL)) { /* a guard: strlen(NULL) (M729) */
                JC_CHECK(jc_utf8_valid(r3, strlen(r3)));
            }
        }
    }

    jc_arena_free(a);
}
