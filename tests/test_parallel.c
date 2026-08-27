/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_parallel.c - tests for the parallel-swarm pure helpers (jc_parallel.c)
 * and CPU detection. The fork pool + git worktrees are verified end-to-end
 * (live model + the worktree integration test in test_snapshot). */

#include "jc_test.h"
#include "jc_parallel.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_vec.h"

#include <string.h>
#include <stdlib.h>

static void test_cpu_count(void)
{
    JC_CHECK(jc_cpu_count() >= 1);
}

static void test_eff_max(void)
{
    /* auto cap = min(cpu, ceiling); result = min(n_tasks, cap), >= 1 */
    JC_CHECK(jc_parallel_eff_max(4, 0, 8, 8) == 4);   /* fewer tasks than cap */
    JC_CHECK(jc_parallel_eff_max(20, 0, 8, 8) == 8);  /* capped by ceiling    */
    JC_CHECK(jc_parallel_eff_max(20, 0, 16, 8) == 8); /* ceiling beats cpu    */
    JC_CHECK(jc_parallel_eff_max(20, 3, 8, 8) == 3);  /* explicit config cap  */
    JC_CHECK(jc_parallel_eff_max(10, 0, 2, 8) == 2);  /* capped by cpu        */
    JC_CHECK(jc_parallel_eff_max(0, 0, 8, 8) == 1);   /* never below 1        */
    JC_CHECK(jc_parallel_eff_max(5, 0, 0, 8) == 1);   /* cpu<1 => autocap 1   */
}

static void test_parse_changes(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_vec v;
    struct jc_change *c;

    jc_vec_init(&v, sizeof(struct jc_change));
    jc_parallel_parse_changes(
        "A\tnew.txt\nM\tsrc/a.c\nD\told.txt\n", a, &v);
    JC_CHECK(v.len == 3);
    c = (struct jc_change *)jc_vec_at(&v, 0);
    JC_CHECK(c->status == 'A');
    JC_CHECK_STR(c->path, "new.txt");
    c = (struct jc_change *)jc_vec_at(&v, 1);
    JC_CHECK(c->status == 'M');
    JC_CHECK_STR(c->path, "src/a.c");
    c = (struct jc_change *)jc_vec_at(&v, 2);
    JC_CHECK(c->status == 'D');
    JC_CHECK_STR(c->path, "old.txt");
    jc_vec_free(&v);

    /* CRLF trimmed; blank and tab-less lines ignored; no trailing newline. */
    jc_vec_init(&v, sizeof(struct jc_change));
    jc_parallel_parse_changes("M\tx\r\n\ngarbage\nA\ty", a, &v);
    JC_CHECK(v.len == 2);
    c = (struct jc_change *)jc_vec_at(&v, 0);
    JC_CHECK_STR(c->path, "x");
    c = (struct jc_change *)jc_vec_at(&v, 1);
    JC_CHECK(c->status == 'A');
    JC_CHECK_STR(c->path, "y");
    jc_vec_free(&v);

    /* Empty / NULL are safe. */
    jc_vec_init(&v, sizeof(struct jc_change));
    jc_parallel_parse_changes("", a, &v);
    jc_parallel_parse_changes(NULL, a, &v);
    JC_CHECK(v.len == 0);
    jc_vec_free(&v);

    jc_arena_free(a);
}

static void test_claim(void)
{
    struct jc_vec seen;
    char *a = (char *)"src/a.c";
    char *b = (char *)"src/b.c";

    jc_vec_init(&seen, sizeof(char *));
    JC_CHECK(jc_parallel_claim(&seen, a) == 1);  /* first wins */
    JC_CHECK(jc_parallel_claim(&seen, b) == 1);  /* distinct path */
    JC_CHECK(jc_parallel_claim(&seen, a) == 0);  /* conflict: already claimed */
    JC_CHECK(jc_parallel_claim(&seen, NULL) == 0);
    JC_CHECK(seen.len == 2);
    jc_vec_free(&seen);
}

static void test_parse_msg(void)
{
    struct jc_pmsg m;

    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"tool\",\"name\":\"read_file\"}", &m)
             == JC_PMSG_TOOL);
    JC_CHECK(strcmp(m.tool, "read_file") == 0);
    free(m.answer); free(m.error);

    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"tok\",\"n\":1234}", &m)
             == JC_PMSG_TOK);
    JC_CHECK(m.tokens == 1234.0);
    free(m.answer); free(m.error);

    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"answer\":\"hi\",\"tokens\":7}", &m) == JC_PMSG_DONE);
    JC_CHECK(m.answer != NULL && strcmp(m.answer, "hi") == 0);
    JC_CHECK(m.error == NULL && m.tokens == 7.0);
    free(m.answer); free(m.error);

    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"error\":\"boom\",\"tokens\":3}", &m) == JC_PMSG_DONE);
    JC_CHECK(m.error != NULL && strcmp(m.error, "boom") == 0);
    JC_CHECK(m.answer == NULL);
    free(m.answer); free(m.error);

    /* Malformed / unknown -> NONE, no allocation. */
    JC_CHECK(jc_parallel_parse_msg("not json", &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"other\"}", &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg(NULL, &m) == JC_PMSG_NONE);
}

/* M144: verifier resolution for the per-child gate -- envelope command first,
 * then config `verify`, then `testCommand`; empty strings are skipped like
 * NULLs (the same precedence the run-level gate uses). */
static void test_verify_cmd(void)
{
    JC_CHECK(jc_parallel_verify_cmd(NULL, NULL, NULL) == NULL);
    JC_CHECK(strcmp(jc_parallel_verify_cmd("make v", "make c", "make t"),
                    "make v") == 0);
    JC_CHECK(strcmp(jc_parallel_verify_cmd(NULL, "make c", "make t"),
                    "make c") == 0);
    JC_CHECK(strcmp(jc_parallel_verify_cmd(NULL, NULL, "make t"),
                    "make t") == 0);
    JC_CHECK(strcmp(jc_parallel_verify_cmd("", "", "make t"),
                    "make t") == 0);
    JC_CHECK(jc_parallel_verify_cmd("", "", "") == NULL);
}

void test_parallel(void)
{
    test_cpu_count();
    test_eff_max();
    test_parse_changes();
    test_claim();
    test_parse_msg();
    test_verify_cmd();
}
