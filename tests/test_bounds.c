/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_bounds.c - cap boundaries and total ordering (M198 #6, #7).
 *
 * Development use sits well inside every limit, so the behaviour AT a cap is
 * untested by definition. Two separate questions per cap, and the second is the
 * one that bites:
 *
 *   1. is the arithmetic right at exactly the boundary?
 *   2. is the truncation VISIBLE to whoever needs to know?
 *
 * M191 established that a truncation is a correctness boundary when the output
 * feeds a strict parser; M197 added that an invisible skip is a correctness
 * boundary when the USER is the parser -- a session over JC_READ_FILE_MAX simply
 * disappeared from /sessions with "no session matching" as the only symptom.
 *
 * See docs/proposals/2026-07-robustness-edge-cases.md (#6, #7).
 */

#include "jc_test.h"
#include "jc_config.h"
#include "jc_session.h"
#include "jc_platform.h"
#include "jc_memory.h"
#include "jc_glossary.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_snprintf.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- #7: the cap resolver, table-driven over under/at/over ---------------- */

static void test_cap_table(void)
{
    /* {configured, builtin, expected}. jc_config_cap's contract is "a positive
     * configured value wins, anything else falls back to the built-in" -- so 0
     * and every negative are the SAME case, which is easy to get wrong when the
     * check is written as `configured != 0`. */
    static const long cases[][3] = {
        {  0,    4096, 4096 },   /* unset => built-in                        */
        { -1,    4096, 4096 },   /* negative => built-in (not a huge size_t!) */
        { -5,    4096, 4096 },
        {  1,    4096,    1 },   /* smallest positive wins                   */
        {  4095, 4096, 4095 },   /* just under the built-in                  */
        {  4096, 4096, 4096 },   /* exactly the built-in                     */
        {  4097, 4096, 4097 },   /* just over: configured may EXCEED builtin */
        {  1L << 20, 4096, 1L << 20 }
    };
    jc_size i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        jc_size got = jc_config_cap(cases[i][0], (jc_size)cases[i][1]);
        JC_CHECK(got == (jc_size)cases[i][2]);
    }
    /* A zero built-in with nothing configured is still zero -- callers treat
     * that as "use the tool's #define", so it must not silently become huge. */
    JC_CHECK(jc_config_cap(0, 0) == 0);
}

/* --- #7: the read cap, and whether crossing it is VISIBLE ----------------- */

static void write_session_of_size(const char *dir, const char *sid, long total)
{
    /* A valid session JSON padded to exactly `total` bytes. */
    static const char tail[] = "\"}]}";
    char path[512];
    char head[512];
    char chunk[8192];
    long pad;
    FILE *f;

    jc_snprintf(head, sizeof head,
        "{\"sessionId\":\"%s\",\"title\":\"t\",\"workspaceDirectory\":\"/tmp/b\","
        "\"mode\":\"chat\",\"history\":[{\"role\":\"user\",\"content\":\"", sid);
    /* EXACT size: head + pad + tail == total. An earlier version of this helper
     * was off by one and produced a file exactly AT the read cap rather than
     * over it, which quietly turned the over-cap case into an at-cap case --
     * precisely the boundary error this file exists to catch. */
    pad = total - (long)strlen(head) - (long)(sizeof(tail) - 1);
    if (pad < 0) pad = 0;

    jc_snprintf(path, sizeof path, "%s/%s.json", dir, sid);
    f = fopen(path, "wb");
    if (f == NULL) return;
    fputs(head, f);
    memset(chunk, 'a', sizeof chunk);
    while (pad > 0) {
        long n = (pad > (long)sizeof chunk) ? (long)sizeof chunk : pad;
        if (fwrite(chunk, 1, (size_t)n, f) != (size_t)n) break;
        pad -= n;
    }
    fputs(tail, f);
    fclose(f);
}

static void test_read_cap_visibility(void)
{
    char home[128];
    char dir[256];
    char cmd[512];
    struct jc_arena *a;
    struct jc_vec metas;
    int skipped = 0;
    const char *small = "aaaaaaaa-0000-4000-8000-000000000001";
    const char *huge  = "bbbbbbbb-0000-4000-8000-000000000002";

    jc_snprintf(home, sizeof home, "%s/jichi_bounds_%ld", jc_test_tmpdir(), (long)getpid());
    jc_snprintf(cmd, sizeof cmd, "rm -rf %s", home);
    system(cmd);
    jc_snprintf(dir, sizeof dir, "%s/.jichi.d/sessions", home);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    setenv("HOME", home, 1);

    write_session_of_size(dir, small, 512);
    /* Comfortably OVER the 64 MiB read cap (the check is `size > MAX`, so +1
     * would be enough, but a margin makes the intent unmistakable):
     * jc_read_file returns JC_ERR_TOOBIG and this session cannot be listed. */
    write_session_of_size(dir, huge, (long)JC_READ_FILE_MAX + 4096);

    a = jc_arena_new(0);
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    JC_CHECK(jc_session_list_ex(&metas, a, &skipped) == JC_OK);

    /* The under-cap session lists... */
    JC_CHECK(metas.len == 1);
    /* ...and the over-cap one is COUNTED, not silently dropped. That count is
     * the whole point: before M198 the user saw a short list and a success exit
     * code, with no way to tell a missing session from an empty store. */
    JC_CHECK(skipped == 1);

    /* The NULL form must still work for callers that do not care. */
    {
        struct jc_vec m2;
        jc_vec_init(&m2, sizeof(struct jc_session_meta));
        JC_CHECK(jc_session_list_ex(&m2, a, NULL) == JC_OK);
        JC_CHECK(m2.len == 1);
        jc_vec_free(&m2);
    }

    jc_vec_free(&metas);
    jc_arena_free(a);
    system(cmd);
}

/* --- #6: the listing order must be TOTAL ---------------------------------- */

static void test_total_order(void)
{
    char home[128];
    char dir[256];
    char cmd[512];
    struct jc_arena *a;
    struct jc_vec m1, m2;
    char first_run[8][64];
    jc_size i;
    int n;
    /* Deliberately NOT in sorted order on disk, and all written within the same
     * second so every mtime ties -- the exact condition under which qsort's
     * instability used to make "most recent" unpredictable. */
    static const char *ids[] = {
        "cccccccc-0000-4000-8000-000000000003",
        "aaaaaaaa-0000-4000-8000-000000000001",
        "eeeeeeee-0000-4000-8000-000000000005",
        "bbbbbbbb-0000-4000-8000-000000000002",
        "dddddddd-0000-4000-8000-000000000004"
    };

    jc_snprintf(home, sizeof home, "%s/jichi_order_%ld", jc_test_tmpdir(), (long)getpid());
    jc_snprintf(cmd, sizeof cmd, "rm -rf %s", home);
    system(cmd);
    jc_snprintf(dir, sizeof dir, "%s/.jichi.d/sessions", home);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    setenv("HOME", home, 1);

    n = (int)(sizeof(ids) / sizeof(ids[0]));
    for (i = 0; i < (jc_size)n; i++) {
        write_session_of_size(dir, ids[i], 400);
    }

    a = jc_arena_new(0);
    jc_vec_init(&m1, sizeof(struct jc_session_meta));
    JC_CHECK(jc_session_list(&m1, a) == JC_OK);
    JC_CHECK(m1.len == (jc_size)n);
    for (i = 0; i < m1.len; i++) {
        struct jc_session_meta *mm =
            (struct jc_session_meta *)jc_vec_at(&m1, i);
        jc_snprintf(first_run[i], sizeof first_run[0], "%s",
                    mm->id != NULL ? mm->id : "");
    }

    /* Same store, listed again: identical order. With equal mtimes this only
     * holds because the comparator breaks the tie on id. */
    jc_vec_init(&m2, sizeof(struct jc_session_meta));
    JC_CHECK(jc_session_list(&m2, a) == JC_OK);
    JC_CHECK(m2.len == m1.len);
    for (i = 0; i < m2.len; i++) {
        struct jc_session_meta *mm =
            (struct jc_session_meta *)jc_vec_at(&m2, i);
        JC_CHECK(mm->id != NULL && strcmp(mm->id, first_run[i]) == 0);
    }

    /* And the tie order is the documented one: ascending id. */
    for (i = 1; i < m2.len; i++) {
        struct jc_session_meta *p =
            (struct jc_session_meta *)jc_vec_at(&m2, i - 1);
        struct jc_session_meta *c =
            (struct jc_session_meta *)jc_vec_at(&m2, i);
        if (p->mtime == c->mtime) {
            JC_CHECK(strcmp(p->id, c->id) < 0);
        }
    }

    jc_vec_free(&m1);
    jc_vec_free(&m2);
    jc_arena_free(a);
    system(cmd);
}

void test_bounds(void)
{
    const char *saved = getenv("HOME");
    char keep[512];
    keep[0] = '\0';
    if (saved != NULL) {
        jc_snprintf(keep, sizeof keep, "%s", saved);
    }

    test_cap_table();
    test_read_cap_visibility();
    test_total_order();

    /* The two text caps are tail-keeping, so their contract is "no more than
     * MAX bytes survive" -- assert the constants are sane rather than
     * re-testing the loaders, which have their own suites. */
    JC_CHECK(JC_MEMORY_MAX > 0 && JC_MEMORY_MAX <= (64 * 1024));
    JC_CHECK(JC_GLOSSARY_MAX > 0 && JC_GLOSSARY_MAX <= (64 * 1024));
    /* The read cap must stay far above any plausible source file and far below
     * a size that would OOM a 32 MB target (docs/LOW_MEMORY.md's smallest tier). */
    JC_CHECK(JC_READ_FILE_MAX >= (1L << 20));
    JC_CHECK(JC_READ_FILE_MAX <= (128L * 1024L * 1024L));

    if (keep[0] != '\0') {
        setenv("HOME", keep, 1);
    } else {
        unsetenv("HOME");
    }
}
