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

static void wf(const char *path, const char *s)
{
    jc_write_file(path, s, strlen(s));
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
            JC_CHECK(r3 != NULL);
            JC_CHECK(jc_utf8_valid(r3, strlen(r3)));
        }
    }

    jc_arena_free(a);
}
