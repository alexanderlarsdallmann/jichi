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
#include "jc_json.h"
#include "jc_str.h"

#include <string.h>
#include <stdlib.h>

static void test_cpu_count(void)
{
    JC_CHECK(jc_cpu_count() >= 1);
}

/* M669: the count and whether it was MEASURED are two different answers, and
 * conflating them is what let `maxParallelAgents` default to 1 on FreeBSD and
 * NetBSD in silence. The contract pinned here:
 *
 *   - jc_cpu_count() is always usable (>= 1), so no caller needs a guard;
 *   - jc_cpu_count_known() is 0 exactly when that number is a FALLBACK;
 *   - when it IS known the number must be a real one, so a diagnostic can warn
 *     on the fact rather than on the suspicion "the count is 1", which would
 *     nag a genuine single-core VM.
 *
 * On this bench the symbol is declared, so `known` is 1 -- the negative case is
 * verified on the FreeBSD row, where `doctor` prints the warning and Linux does
 * not. A unit test cannot reach the other branch: it is a #ifdef, decided when
 * this file was compiled. */
static void test_cpu_count_known(void)
{
    int known = jc_cpu_count_known();
    JC_CHECK(known == 0 || known == 1);
    if (known) {
        JC_CHECK(jc_cpu_count() >= 1);
    } else {
        /* THE INVARIANT THE WARNING RESTS ON: where the count is not known it
         * must be exactly 1, never 0 and never a stale number. doctor tells the
         * user "it reads 1", and a fallback that read anything else would make
         * that sentence false on the one platform it is written for. */
        JC_CHECK(jc_cpu_count() == 1);
    }
}

/* WHAT THIS TEST DELIBERATELY DOES NOT ASSERT, because the first version did
 * and it broke the FreeBSD row (2026-09-19). It said `JC_CHECK(known == 1)`,
 * reasoning that glibc declares _SC_NPROCESSORS_ONLN so this bench must say
 * "known" -- true here, and a bench-specific fact stated as a universal one.
 * On FreeBSD `known` is 0 BY DESIGN, which is the entire point of the
 * function, so the unit suite failed on the platform the feature exists for:
 *
 *     FAIL tests/test_parallel.c:46: known == 1
 *
 * A portable unit suite may assert the CONTRACT (known is boolean; the count is
 * usable; the fallback is exactly 1) and must not assert which branch a given
 * machine takes. Which branch this machine takes is what `doctor` reports and
 * what the platform rows verify. */

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

    /* M927 (R04-F1): the NUL form. git `diff -z` lists `<status>\0<path>\0`
     * with NO quoting, so a path that line mode would C-quote (a non-ASCII
     * byte) arrives as its raw UTF-8 bytes. Build the stream by hand: a C
     * string literal cannot hold an embedded NUL. */
    {
        char zstream[64];
        int zi = 0;
        const char *st1 = "A";
        const char *p1 = "caf\303\251.md";   /* UTF-8 for caf\xe9.md */
        const char *st2 = "M";
        const char *p2 = "src/a.c";
        const char *seg;
        zi = 0;
        seg = st1; for (; *seg; seg++) zstream[zi++] = *seg;
        zstream[zi++] = '\0';
        seg = p1;  for (; *seg; seg++) zstream[zi++] = *seg;
        zstream[zi++] = '\0';
        seg = st2; for (; *seg; seg++) zstream[zi++] = *seg;
        zstream[zi++] = '\0';
        seg = p2;  for (; *seg; seg++) zstream[zi++] = *seg;
        zstream[zi++] = '\0';
        zstream[zi] = '\0';
        jc_vec_init(&v, sizeof(struct jc_change));
        jc_parallel_parse_changes_z(zstream, a, &v);
        JC_CHECK(v.len == 2);
        c = (struct jc_change *)jc_vec_at(&v, 0);
        JC_CHECK(c->status == 'A');
        /* The path is the raw UTF-8 bytes, not a C-quoted escape. */
        JC_CHECK_STR(c->path, "caf\303\251.md");
        c = (struct jc_change *)jc_vec_at(&v, 1);
        JC_CHECK(c->status == 'M');
        JC_CHECK_STR(c->path, "src/a.c");
        jc_vec_free(&v);
    }

    /* The PRE-FIX bug, made concrete: the LINE parser copies the C-quoted text
     * as the path verbatim (no unquoting), so a merge would copy to
     * `wt/"caf\303\251.md"` -- a name that does not exist. This is the
     * byte-level proof that the line form cannot be parsed back to a real
     * path, and why the merge must switch to the -z form. */
    {
        jc_vec_init(&v, sizeof(struct jc_change));
        jc_parallel_parse_changes("A\t\"caf\\303\\251.md\"\n", a, &v);
        JC_CHECK(v.len == 1);
        c = (struct jc_change *)jc_vec_at(&v, 0);
        /* A real filename never carries the surrounding quotes: the parser
         * hands back the quoted literal, so the path is wrong. */
        JC_CHECK(c->path[0] == '"');
        JC_CHECK(c->path[0] == 0x22 && c->path[1] == 'c');
        jc_vec_free(&v);
    }

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

/* M854 (N33, N24c): which write children answer with a note. The three endings,
 * and every reason one is NOT such a child: a read-only task, no worktree, no done
 * line (the batch killed it), a batch abort (the race in which a group-wide signal
 * reached the child first), a SIGINT rather than a SIGTERM, a normal stop. */
static void test_ending(void)
{
    const char *e;

    e = jc_parallel_ending(1, 1, 1, 0, "aborted", 1, 0, 0);
    JC_CHECK(e != NULL && strstr(e, "stopped from outside") != NULL);
    e = jc_parallel_ending(1, 1, 1, 0, "error", 0, 1, 0);
    JC_CHECK(e != NULL && strstr(e, "ran out of context") != NULL);
    e = jc_parallel_ending(1, 1, 1, 0, "max_iters", 0, 0, 0);
    JC_CHECK(e != NULL && strstr(e, "iteration cap") != NULL);
    /* SIGTERM outranks the cap: a stopped child is described as stopped */
    e = jc_parallel_ending(1, 1, 1, 0, "aborted", 1, 1, 0);
    JC_CHECK(e != NULL && strstr(e, "stopped from outside") != NULL);

    JC_CHECK(jc_parallel_ending(1, 1, 1, 1, "aborted", 1, 0, 0) == NULL); /* batch abort */
    JC_CHECK(jc_parallel_ending(1, 1, 1, 1, "max_iters", 0, 0, 0) == NULL);
    JC_CHECK(jc_parallel_ending(1, 1, 0, 0, "aborted", 1, 0, 0) == NULL); /* no done line */
    JC_CHECK(jc_parallel_ending(0, 1, 1, 0, "max_iters", 0, 0, 0) == NULL); /* read-only */
    JC_CHECK(jc_parallel_ending(1, 0, 1, 0, "max_iters", 0, 0, 0) == NULL); /* no worktree */
    JC_CHECK(jc_parallel_ending(1, 1, 1, 0, "aborted", 0, 0, 0) == NULL); /* SIGINT */
    JC_CHECK(jc_parallel_ending(1, 1, 1, 0, "done", 0, 0, 0) == NULL);
    JC_CHECK(jc_parallel_ending(1, 1, 1, 0, "error", 0, 0, 0) == NULL);
    JC_CHECK(jc_parallel_ending(1, 1, 1, 0, NULL, 0, 0, 0) == NULL);

    /* M858 (N42): the fourth ending -- a turn that ended on a reply cut at the
     * output ceiling, with text (done) or without (no_answer). A cut on a capped
     * run is the cap's ending; a batch abort still never merges. */
    e = jc_parallel_ending(1, 1, 1, 0, "done", 0, 0, 16384);
    JC_CHECK(e != NULL && strstr(e, "cut off at the output ceiling") != NULL);
    e = jc_parallel_ending(1, 1, 1, 0, "no_answer", 0, 0, 16384);
    JC_CHECK(e != NULL && strstr(e, "cut off at the output ceiling") != NULL);
    e = jc_parallel_ending(1, 1, 1, 0, "max_iters", 0, 0, 16384);
    JC_CHECK(e != NULL && strstr(e, "iteration cap") != NULL);
    JC_CHECK(jc_parallel_ending(1, 1, 1, 1, "done", 0, 0, 16384) == NULL);
    JC_CHECK(jc_parallel_ending(1, 1, 1, 0, "done", 0, 0, 0) == NULL);

    /* M861 (N28): a budget stop is an ending -- the task's own max_tool_calls, or
     * its share of the run's budget. M854 listed `budget` with the normal stops;
     * the branch review's answer to N28's merge question named that a defect. */
    e = jc_parallel_ending(1, 1, 1, 0, "task_budget", 0, 0, 0);
    JC_CHECK(e != NULL && strstr(e, "max_tool_calls") != NULL);
    e = jc_parallel_ending(1, 1, 1, 0, "budget", 0, 0, 0);
    JC_CHECK(e != NULL && strstr(e, "share of the run's budget") != NULL);
    JC_CHECK(jc_parallel_ending(0, 1, 1, 0, "task_budget", 0, 0, 0) == NULL);
    JC_CHECK(jc_parallel_ending(1, 1, 1, 1, "budget", 0, 0, 0) == NULL);
}

/* M861 (N28): `max_tool_calls`, read leniently -- a whole number, or a string of
 * digits -- and anything else refused (-1), so a lead never believes a budget
 * binds that does not; and the note's four-fifths, rounded up, none under 5. */
static void test_max_calls(void)
{
    cJSON *v;
    int n = -7;
    jc_size i;
    const double bad_num[] = { 0.0, -3.0, 1.5 };
    const char *bad_str[] = { "a few", "", "0", "-5", "1.5", " 3" };
    struct jc_sb sb;

    JC_CHECK(jc_parallel_parse_max_calls(NULL, &n) == 0 && n == -7);
    v = cJSON_CreateNull();
    JC_CHECK(jc_parallel_parse_max_calls(v, &n) == 0 && n == -7);
    cJSON_Delete(v);
    v = cJSON_CreateNumber(150);
    JC_CHECK(jc_parallel_parse_max_calls(v, &n) == 1 && n == 150);
    cJSON_Delete(v);
    v = cJSON_CreateString("150");
    JC_CHECK(jc_parallel_parse_max_calls(v, &n) == 1 && n == 150);
    cJSON_Delete(v);
    v = cJSON_CreateNumber(5e9);
    JC_CHECK(jc_parallel_parse_max_calls(v, &n) == 1 && n == JC_PAR_MAX_CALLS_CAP);
    cJSON_Delete(v);
    v = cJSON_CreateString("99999999999999999999");
    JC_CHECK(jc_parallel_parse_max_calls(v, &n) == 1 && n == JC_PAR_MAX_CALLS_CAP);
    cJSON_Delete(v);
    for (i = 0; i < sizeof bad_num / sizeof bad_num[0]; i++) {
        v = cJSON_CreateNumber(bad_num[i]);
        JC_CHECK(jc_parallel_parse_max_calls(v, &n) == -1);
        cJSON_Delete(v);
    }
    for (i = 0; i < sizeof bad_str / sizeof bad_str[0]; i++) {
        v = cJSON_CreateString(bad_str[i]);
        JC_CHECK(jc_parallel_parse_max_calls(v, &n) == -1);
        cJSON_Delete(v);
    }
    v = cJSON_CreateTrue();
    JC_CHECK(jc_parallel_parse_max_calls(v, &n) == -1);
    cJSON_Delete(v);

    JC_CHECK(jc_parallel_budget_note_at(150) == 120);
    JC_CHECK(jc_parallel_budget_note_at(10) == 8);
    JC_CHECK(jc_parallel_budget_note_at(7) == 6);
    JC_CHECK(jc_parallel_budget_note_at(5) == 4);
    JC_CHECK(jc_parallel_budget_note_at(4) == 0);
    JC_CHECK(jc_parallel_budget_note_at(0) == 0);
    jc_sb_init(&sb);
    jc_parallel_budget_note(120, 150, &sb);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "You have used 120 of the 150 tool calls") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "At 150 calls the task ends") != NULL);
    jc_sb_free(&sb);
}

/* M854: the done line's new fields reach the parent. */
static void test_parse_ending(void)
{
    struct jc_pmsg m;

    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"error\":\"interrupted\",\"stop\":\"aborted\","
        "\"last\":\"half done\",\"term\":1,\"overflow\":1,\"cut\":16384}", &m) == JC_PMSG_DONE);
    JC_CHECK(m.last != NULL && strcmp(m.last, "half done") == 0);
    JC_CHECK(m.term == 1 && m.overflow == 1);
    JC_CHECK(m.cut == 16384);   /* M858 */
    free(m.answer); free(m.error); free(m.last);
    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"stop\":\"task_budget\",\"tools\":2,\"tused\":6}", &m)
             == JC_PMSG_DONE);
    JC_CHECK(m.tused == 6 && m.tool_calls == 2);   /* M861 */
    free(m.answer); free(m.error); free(m.last);
    /* an older child sends none of them */
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"done\",\"answer\":\"x\"}", &m)
             == JC_PMSG_DONE);
    JC_CHECK(m.last == NULL && m.term == 0 && m.overflow == 0 && m.cut == 0);
    JC_CHECK(m.tused == 0);   /* M861 */
    free(m.answer); free(m.error); free(m.last);
}

/* M862 (N43): a red verify's excerpt -- the failing lines from before the tail
 * first, bounded, then the tail; nothing shown twice; NUL-safe. */
static void summary(struct jc_sb *b, int lines)
{
    int i;
    for (i = 0; i < lines; i++) {
        jc_sb_append_fmt(b, "summary line %02d: build step ok, cached .........\n", i);
    }
}

static void test_verify_excerpt(void)
{
    struct jc_sb in;
    struct jc_sb ex;
    int i;
    char longline[320];

    /* zig's order: the failures, then a summary longer than the tail */
    jc_sb_init(&in);
    jc_sb_append(&in, "error: 'demo.test.x' failed:\nexpected 47, found 40\n"
                      "a quiet line before the tail\n");
    summary(&in, 20);
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt(in.data, in.len, 500, &ex);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "failing lines (before the tail):\n"
             "error: 'demo.test.x' failed:\nexpected 47, found 40\n") != NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "a quiet line") == NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "output tail:\n") != NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "summary line 19") != NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "summary line 00") == NULL);
    /* the tail starts at a line's start, not inside a line */
    JC_CHECK(ex.data != NULL && strstr(ex.data, "output tail:\nsummary line ") != NULL);
    jc_sb_free(&ex);
    jc_sb_free(&in);

    /* a failing line that straddles the cut is whole, in the tail */
    jc_sb_init(&in);
    summary(&in, 20);
    jc_sb_append(&in, "FAIL straddler: the line the 500-byte cut would split\n");
    summary(&in, 10);   /* 490 bytes after it: the cut falls 44 bytes into it */
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt(in.data, in.len, 500, &ex);
    JC_CHECK(ex.data != NULL &&
             strstr(ex.data, "FAIL straddler: the line the 500-byte cut would split\n")
             != NULL);
    jc_sb_free(&ex);
    jc_sb_free(&in);

    /* all of it inside the tail: the tail alone, nothing twice */
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt("FAIL short\n", 11, 500, &ex);
    JC_CHECK(ex.data != NULL && strcmp(ex.data, "output tail:\nFAIL short\n") == 0);
    jc_sb_free(&ex);

    /* thirty failing lines: twelve, and the count of the rest */
    jc_sb_init(&in);
    for (i = 0; i < 30; i++) {
        jc_sb_append_fmt(&in, "FAIL case-%d\n", 100 + i);
    }
    summary(&in, 20);
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt(in.data, in.len, 500, &ex);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "FAIL case-111\n") != NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "FAIL case-112") == NULL);
    JC_CHECK(ex.data != NULL &&
             strstr(ex.data, "(18 more such line(s) before the tail)") != NULL);
    jc_sb_free(&ex);
    jc_sb_free(&in);

    /* a long failing line is cut at JC_PAR_VFAIL_LINE_MAX */
    memset(longline, 'x', sizeof longline);
    memcpy(longline, "FAIL ", 5);
    longline[300] = '\n';
    jc_sb_init(&in);
    jc_sb_append_n(&in, longline, 301);
    summary(&in, 20);
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt(in.data, in.len, 500, &ex);
    JC_CHECK(ex.data != NULL && strstr(ex.data, " ...\n") != NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "FAIL ") != NULL &&
             strlen(strstr(ex.data, "FAIL ")) > 0);
    {
        const char *f = (ex.data != NULL) ? strstr(ex.data, "FAIL ") : NULL;
        const char *d = (f != NULL) ? strstr(f, " ...\n") : NULL;
        JC_CHECK(f != NULL && d != NULL && (d - f) == JC_PAR_VFAIL_LINE_MAX);
    }
    jc_sb_free(&ex);
    jc_sb_free(&in);

    /* a NUL inside a failing line is a space; the excerpt stays one C string */
    jc_sb_init(&in);
    jc_sb_append_n(&in, "FAIL a\0b\n", 9);
    summary(&in, 20);
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt(in.data, in.len, 500, &ex);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "FAIL a b\n") != NULL);
    JC_CHECK(ex.data != NULL && strstr(ex.data, "output tail:") != NULL);
    jc_sb_free(&ex);
    jc_sb_free(&in);

    /* no output */
    jc_sb_init(&ex);
    jc_parallel_verify_excerpt(NULL, 0, 500, &ex);
    JC_CHECK(ex.data != NULL && strcmp(ex.data, "(no output)\n") == 0);
    jc_sb_free(&ex);
}

/* M865: which diffs change an existing test assertion -- the scoreboard pin moved
 * (card 215's case), and what is not: a pure addition, a non-test path, a changed
 * comment in a test file. A later hunk counts; NULL is no. */
static void test_assertion_diff(void)
{
    const char *moved =
        "diff --git a/src/demos/scoreboard_test.zig b/src/demos/scoreboard_test.zig\n"
        "--- a/src/demos/scoreboard_test.zig\n"
        "+++ b/src/demos/scoreboard_test.zig\n"
        "@@ -12 +12 @@ test \"L1 pin\" {\n"
        "-    try std.testing.expectEqual(@as(usize, 47), passed);\n"
        "+    try std.testing.expectEqual(@as(usize, 43), passed);\n";
    const char *added =
        "@@ -12,0 +13 @@\n"
        "+    try std.testing.expectEqual(@as(usize, 1), extra);\n";
    const char *comment =
        "@@ -3 +3 @@\n"
        "-// the pin counts the L1 demos\n"
        "+// the pin counts every L1 demo\n";
    const char *second =
        "@@ -3 +3 @@\n"
        "-// the pin counts the L1 demos\n"
        "+// the pin counts every L1 demo\n"
        "@@ -12 +12 @@\n"
        "-    assert(n == 47);\n"
        "+    assert(n == 43);\n";

    JC_CHECK(jc_parallel_diff_edits_assertion("src/demos/scoreboard_test.zig", moved) == 1);
    JC_CHECK(jc_parallel_diff_edits_assertion("src/demos/scoreboard_test.zig", added) == 0);
    JC_CHECK(jc_parallel_diff_edits_assertion("src/demos/scoreboard.zig", moved) == 0);
    JC_CHECK(jc_parallel_diff_edits_assertion("tests/pin_test.c", comment) == 0);
    JC_CHECK(jc_parallel_diff_edits_assertion("tests/pin_test.c", second) == 1);
    JC_CHECK(jc_parallel_diff_edits_assertion(NULL, moved) == 0);
    JC_CHECK(jc_parallel_diff_edits_assertion("x_test.c", NULL) == 0);
    JC_CHECK(jc_parallel_diff_edits_assertion("x_test.c", "") == 0);
}

/* M869 (N27, def-305): a loop stop is named as one -- the phrase per kind, NULL for
 * anything else (the caller keeps M854's "iteration cap" then); and the done
 * line's `looped` reaches the parent. */
static void test_loop_ending(void)
{
    struct jc_pmsg m;
    const char *e;

    e = jc_parallel_loop_ending(1);
    JC_CHECK(e != NULL && strstr(e, "same successful call") != NULL);
    e = jc_parallel_loop_ending(2);
    JC_CHECK(e != NULL && strstr(e, "same refused call") != NULL);
    e = jc_parallel_loop_ending(3);
    JC_CHECK(e != NULL && strstr(e, "placeholders") != NULL);
    e = jc_parallel_loop_ending(4);   /* M870 (N21) */
    JC_CHECK(e != NULL && strstr(e, "cycling through the same calls") != NULL);
    JC_CHECK(jc_parallel_loop_ending(0) == NULL);
    JC_CHECK(jc_parallel_loop_ending(5) == NULL);
    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"stop\":\"max_iters\",\"looped\":2,\"repeated\":9}", &m)
             == JC_PMSG_DONE);
    JC_CHECK(m.looped == 2 && m.repeated == 9);
    free(m.answer); free(m.error); free(m.last);
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"done\",\"stop\":\"max_iters\"}", &m)
             == JC_PMSG_DONE);
    JC_CHECK(m.looped == 0);
    free(m.answer); free(m.error); free(m.last);
}

/* M875: the metric gate's pure parts. The parse is what turns a census into a
 * number, so each way it could misread is checked: a label, the last number, a
 * number inside a word, a negative, a fraction, and the absence of any. */
/* M876 (N47): the child's done line carries the run's model failure and how often
 * its last failing class failed; an older child sends neither, which reads as
 * empty and 0. */
static void test_parse_run_error(void)
{
    struct jc_pmsg m;
    JC_CHECK(jc_parallel_parse_msg(
        "{\"t\":\"done\",\"stop\":\"error\",\"error\":\"run failed\","
        "\"ftool\":\"run_terminal_command\",\"fcls\":6,\"fcount\":3,"
        "\"rerr\":\"could not connect within 20s\",\"rerrn\":12,"
        "\"rerr0\":1790951327,\"rerr1\":1790951638}", &m) == JC_PMSG_DONE);
    JC_CHECK(m.fcount == 3 && m.rerrn == 12);
    JC_CHECK(strcmp(m.rerr, "could not connect within 20s") == 0);
    JC_CHECK(m.rerr0 == 1790951327L && m.rerr1 == 1790951638L);
    free(m.error);
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"done\",\"stop\":\"done\","
                                   "\"answer\":\"x\"}", &m) == JC_PMSG_DONE);
    JC_CHECK(m.fcount == 0 && m.rerr[0] == '\0' && m.rerrn == 0);
    free(m.answer);
}

static void test_metric_gate_helpers(void)
{
    double v = 0.0;
    char buf[48];
    struct jc_vec pins;
    const char *p1 = "src/score*_test.zig";
    const char *p2 = "./docs/pin.md";

    /* the number after the label, not before it, nor the last one */
    JC_CHECK(jc_parallel_metric_parse("L1 in scope: 52 of 139 (L2 50)\n",
                                      "L1 in scope:", &v) == 1 && v == 52.0);
    /* no label: the last number printed */
    JC_CHECK(jc_parallel_metric_parse("built 3 targets\nscore 47\n", NULL, &v) == 1 &&
             v == 47.0);
    JC_CHECK(jc_parallel_metric_parse("score 47\n", "", &v) == 1 && v == 47.0);
    /* a number inside a word is not one: "L1", "x86_64", "v1.2" */
    JC_CHECK(jc_parallel_metric_parse("L1 x86_64 v1.2 done", NULL, &v) == 0);
    JC_CHECK(jc_parallel_metric_parse("L1: 7", "L1:", &v) == 1 && v == 7.0);
    /* a negative and a fraction */
    JC_CHECK(jc_parallel_metric_parse("delta: -3", "delta:", &v) == 1 && v == -3.0);
    JC_CHECK(jc_parallel_metric_parse("rate 0.25\n", NULL, &v) == 1 && v == 0.25);
    /* the label absent, or nothing after it: no number, never a guess */
    JC_CHECK(jc_parallel_metric_parse("score: n/a\n", "score:", &v) == 0);
    JC_CHECK(jc_parallel_metric_parse("total 5\n", "score:", &v) == 0);
    JC_CHECK(jc_parallel_metric_parse("", NULL, &v) == 0);
    JC_CHECK(jc_parallel_metric_parse(NULL, NULL, &v) == 0);

    /* worse, both ways; equal is not worse */
    JC_CHECK(jc_parallel_metric_worse(47.0, 43.0, 1) == 1);
    JC_CHECK(jc_parallel_metric_worse(47.0, 48.0, 1) == 0);
    JC_CHECK(jc_parallel_metric_worse(47.0, 47.0, 1) == 0);
    JC_CHECK(jc_parallel_metric_worse(5.0, 7.0, 0) == 1);
    JC_CHECK(jc_parallel_metric_worse(5.0, 3.0, 0) == 0);

    /* pins: a glob, an exact path, "./" on either side */
    jc_vec_init(&pins, sizeof(char *));
    jc_vec_push(&pins, &p1);
    jc_vec_push(&pins, &p2);
    JC_CHECK(jc_parallel_path_is_pin("src/score_test.zig", &pins) == 1);
    JC_CHECK(jc_parallel_path_is_pin("src/score2_test.zig", &pins) == 1);
    JC_CHECK(jc_parallel_path_is_pin("./src/score3_test.zig", &pins) == 1);
    JC_CHECK(jc_parallel_path_is_pin("docs/pin.md", &pins) == 1);
    JC_CHECK(jc_parallel_path_is_pin("src/other_test.zig", &pins) == 0);
    JC_CHECK(jc_parallel_path_is_pin("lib/src/score_test.zig", &pins) == 0);
    JC_CHECK(jc_parallel_path_is_pin(NULL, &pins) == 0);
    jc_vec_free(&pins);
    jc_vec_init(&pins, sizeof(char *));
    JC_CHECK(jc_parallel_path_is_pin("src/score_test.zig", &pins) == 0);
    jc_vec_free(&pins);

    /* the number as a person reads it; huge values never take the long cast */
    jc_parallel_metric_fmt(52.0, buf, sizeof buf);
    JC_CHECK(strcmp(buf, "52") == 0);
    jc_parallel_metric_fmt(-3.0, buf, sizeof buf);
    JC_CHECK(strcmp(buf, "-3") == 0);
    jc_parallel_metric_fmt(0.25, buf, sizeof buf);
    JC_CHECK(strcmp(buf, "0.25") == 0);
    jc_parallel_metric_fmt(1e300, buf, sizeof buf);
    JC_CHECK(strchr(buf, 'e') != NULL && strlen(buf) < 16);
    jc_parallel_metric_fmt(5e9, buf, sizeof buf);   /* beyond a 32-bit long */
    JC_CHECK(strcmp(buf, "5000000000") == 0);
}

void test_parallel(void)
{
    test_metric_gate_helpers();
    test_parse_run_error();
    test_cpu_count();
    test_cpu_count_known();
    test_eff_max();
    test_parse_changes();
    test_claim();
    test_parse_msg();
    test_verify_cmd();
    test_ending();          /* M854 */
    test_parse_ending();    /* M854 */
    test_max_calls();       /* M861 */
    test_verify_excerpt();  /* M862 */
    test_assertion_diff();  /* M865 */
    test_loop_ending();     /* M869 */
}
