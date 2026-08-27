/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_glossary.c - glossary loading (M35c). */

#include "jc_test.h"
#include "jc_snprintf.h"
#include "jc_glossary.h"
#include "jc_app.h"
#include "jc_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_glossary(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    const char *dir = jc_test_tmp("jichi_glossary_test");
    /* Held in a local, not re-fetched: the shell commands below must clean the
     * SAME directory the C side uses. A leftover literal /tmp here survived the
     * M457 conversion because it sits MID-STRING inside a shell command rather
     * than at a quote boundary -- and on Termux, where /tmp is not writable by
     * an app uid, `rm -rf /tmp/...` failed, the `&&` short-circuited, and the
     * mkdir that the whole test depends on never ran (M459). */
    char ghome[300];
    char path[256];

    memset(&app, 0, sizeof(app));
    app.arena = a;

    /* Isolate the global location (HOME/.config/...) so the host's real
     * glossary can't influence the test. */
    jc_snprintf(ghome, sizeof ghome, "%s", jc_test_tmp("jichi_glossary_home"));
    setenv("HOME", ghome, 1);
    {
        char cmd[800];
        jc_snprintf(cmd, sizeof cmd, "rm -rf %s %s && mkdir -p %s/.jichi",
                    dir, ghome, dir);
        if (system(cmd) != 0) { /* ignore */ }
    }
    strcpy(app.cwd, dir);

    /* No file yet => NULL. */
    JC_CHECK(jc_glossary_load(&app) == NULL);

    /* A project glossary loads. */
    sprintf(path, "%s/.jichi/glossary.md", dir);
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
        JC_CHECK(g != NULL);
        JC_CHECK(g != NULL && strstr(g, "frobnicator") != NULL);
    }

    {
        char cmd[800];
        jc_snprintf(cmd, sizeof cmd, "rm -rf %s %s", dir, ghome);
        if (system(cmd) != 0) { /* ignore */ }
    }
    jc_arena_free(a);
}
