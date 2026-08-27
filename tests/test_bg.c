/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_bg.c - background process registry (M26). Forks real /bin/sh children. */

#include "jc_test.h"
#include "jc_bg.h"
#include "jc_str.h"
#include "jc_platform.h"

#include <string.h>

/* Poll+read up to ~2s, waiting for `id` to exit; ACCUMULATES every read in
 * `sb` (cleared once, at entry).
 *
 * It must accumulate rather than overwrite, because jc_bg_read is an
 * INCREMENTAL reader by design: it advances read_off and returns only the
 * bytes new since the last call ("(no new output)" when there are none), so
 * that read_background_output hands the model a tail instead of re-sending
 * the whole buffer -- and the token bill -- on every poll. Clearing `sb` each
 * iteration therefore DISCARDED output an earlier poll had already consumed,
 * and the caller's assertions held only when a child's output and its exit
 * happened to land in the SAME 20ms window.
 *
 * That is true of `echo x` on a fast idle host and false the moment anything
 * is slow: found on an Android tablet whose build had drifted from 27s to 43s
 * between runs, where "background_hello" arrived one poll before "exited"
 * (M459). The two adjacent assertions are what identified it -- "exited,
 * status 0" passed, so the shell had run and echo had succeeded, which left
 * only the reader. */
static void wait_exit(struct jc_bg_mgr *m, int id, struct jc_sb *sb)
{
    struct jc_sb chunk;
    int i;

    jc_sb_init(&chunk);
    jc_sb_clear(sb);
    for (i = 0; i < 100; i++) {
        jc_sb_clear(&chunk);
        jc_bg_read(m, id, &chunk);
        if (chunk.data != NULL) {
            jc_sb_append_n(sb, chunk.data, chunk.len);
        }
        if (chunk.data != NULL && strstr(chunk.data, "exited") != NULL) {
            break;
        }
        jc_sleep_ms(20, NULL);
    }
    jc_sb_free(&chunk);
}

void test_bg(void)
{
    struct jc_bg_mgr m;
    struct jc_sb sb;
    int id;
    int i;

    jc_bg_mgr_init(&m);
    jc_sb_init(&sb);

    /* A short command runs, produces output, and exits 0. */
    id = jc_bg_start(&m, "echo background_hello");
    JC_CHECK(id > 0);
    if (id > 0) {
        wait_exit(&m, id, &sb);
        JC_CHECK(strstr(sb.data ? sb.data : "", "background_hello") != NULL);
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited, status 0") != NULL);
    }

    /* Output produced BEFORE the process exits -- the case the feature exists
     * for (a dev server logging while it runs), and the one a SINGLE poll
     * cannot observe. The case above can pass without ever crossing a poll
     * boundary, which is why the helper's bug hid behind it for 400
     * milestones. `sleep 1` is deliberately a whole second: POSIX sleep takes
     * an integer, and this suite runs on busybox and uClibc targets where a
     * fractional argument is not portable. */
    jc_sb_clear(&sb);
    id = jc_bg_start(&m, "echo early_line; sleep 1");
    JC_CHECK(id > 0);
    if (id > 0) {
        wait_exit(&m, id, &sb);
        JC_CHECK(strstr(sb.data ? sb.data : "", "early_line") != NULL);
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited, status 0") != NULL);
    }

    /* An unknown id is reported, not crashed on. */
    jc_sb_clear(&sb);
    JC_CHECK(jc_bg_read(&m, 99999, &sb) == JC_ERR_NOTFOUND);
    JC_CHECK(jc_bg_kill(&m, 99999) == JC_ERR_NOTFOUND);

    /* A long-runner can be killed; afterwards it reads as exited. */
    id = jc_bg_start(&m, "sleep 100");
    JC_CHECK(id > 0);
    if (id > 0) {
        JC_CHECK(jc_bg_kill(&m, id) == JC_OK);
        jc_sb_clear(&sb);
        jc_bg_read(&m, id, &sb);
        JC_CHECK(strstr(sb.data ? sb.data : "", "exited") != NULL);
    }

    /* The registry is bounded: once full, start returns 0. */
    {
        int started = 0;
        int full_hit = 0;
        for (i = 0; i < JC_BG_MAX + 3; i++) {
            int r = jc_bg_start(&m, "sleep 100");
            if (r > 0) started++;
            else if (r == 0) full_hit = 1;
        }
        JC_CHECK(started <= JC_BG_MAX);
        JC_CHECK(full_hit == 1);
    }

    jc_sb_free(&sb);
    /* Frees buffers and SIGTERM/KILLs every live child (no zombies under ASan/
     * valgrind). */
    jc_bg_mgr_free(&m);

    /* Finished slots are reclaimed: more than JC_BG_MAX *lifetime* starts
     * succeed as long as prior ones have exited (regression test for the
     * "feature unusable after 8 starts" bug). */
    {
        struct jc_bg_mgr m2;
        struct jc_sb sb2;
        int total = 0;
        jc_bg_mgr_init(&m2);
        jc_sb_init(&sb2);
        for (i = 0; i < JC_BG_MAX + 4; i++) {
            int r = jc_bg_start(&m2, "true"); /* exits immediately */
            JC_CHECK(r > 0);                  /* never "registry full" */
            if (r > 0) {
                total++;
                wait_exit(&m2, r, &sb2);       /* reap so the slot can recycle */
            }
        }
        JC_CHECK(total == JC_BG_MAX + 4);
        jc_sb_free(&sb2);
        jc_bg_mgr_free(&m2);
    }
}
