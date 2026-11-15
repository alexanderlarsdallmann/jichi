/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_gateci.c - the gate ci stage's PURE line-assembly (M1154, H9b,
 * task 8, wave 3): the unit table for the exported seam in
 * jc_hp_gateci.c. The engine is pure (no git, no I/O): it assembles the
 * EXACT bytes the stage's record prints -- the gate-run.sh:77 rc line,
 * the gate-run.sh:79 RED join's tr bytes (the trailing ';', the empty
 * substitution's double space), the b6 RED line with the SV:1238 excerpt
 * attach, the excerpt's one-before / twenty-after blocks (each line cut
 * 300, one blank line between blocks), the ci-gate.sh:24-25 KEPT-prefix
 * diagnostics, the 16 keyless names, and the run child's env (the 16
 * names set EMPTY PLUS TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING).
 * Each case builds one line (or the excerpt half) into a buffer and
 * compares it to the reference-derived literal. */

/* mkstemp is XOPEN2K (POSIX-2008): the build-wide -D_POSIX_C_SOURCE=200112L
 * does NOT declare it (the M1134 / T5 / gateci per-TU idiom: the
 * _XOPEN_SOURCE 700 guard BEFORE the first header that reaches features.h). */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#elif _XOPEN_SOURCE < 700
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
/* M1177: <time.h> FIRST, before any header that can pull in <sys/stat.h>
 * (<fcntl.h> does, on glibc 2.11). With _XOPEN_SOURCE 700, glibc 2.11-2.24's
 * <sys/stat.h> declares struct timespec fields (st_atim, ...) but includes
 * <time.h> only for time_t, so timespec stays incomplete and the build failed on
 * Debian 6-9 and CentOS 6-7 (the userland ladder, 2026-11-13). A full <time.h>
 * first defines it; newer glibcs, musl and the BSDs define it either way. */
#include <time.h>

#include <stdio.h>
#include <stdlib.h>       /* mkstemp */
#include <string.h>
#include <unistd.h>       /* unlink */

#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_test.h"
#include "jc_vec.h"
#include "../src/harness/jc_hp_gateci.h"

/* The -Wmissing-prototypes self-declaration (the T2 / T3 / T5 test idiom:
 * the function is declared in tests/test_main.c, a different TU, so it
 * names its own prototype here). */
void test_gateci(void);

#define TC_BUF (16 * 1024)

static void
tc_eq(const char *buf, const char *want)
{
    JC_CHECK(strcmp(buf, want) == 0);
}

void test_gateci(void)
{
    char buf[TC_BUF];

    /* The gate-run.sh:77 rc line (CHANGED into the record): the LAST
     * `^ci: OK` line's tail, or the reference's ${okl:-no 'ci: OK' line}
     * (the inner single quotes are the reference's own). */
    JC_CHECK(jc_gateci_rc_line(0, "ci: OK", buf, sizeof buf) > 0);
    tc_eq(buf, "  ci: rc=0 ci: OK\n");
    JC_CHECK(jc_gateci_rc_line(2, "ci: OK", buf, sizeof buf) > 0);
    tc_eq(buf, "  ci: rc=2 ci: OK\n");
    JC_CHECK(jc_gateci_rc_line(0, "", buf, sizeof buf) > 0);
    tc_eq(buf, "  ci: rc=0 no 'ci: OK' line\n");
    JC_CHECK(jc_gateci_rc_line(1, NULL, buf, sizeof buf) > 0);
    tc_eq(buf, "  ci: rc=1 no 'ci: OK' line\n");

    /* The gate-run.sh:79 join's EXACT bytes: the first four matching
     * lines, each cut 160, joined by ';' -- the reference's
     * `tr '\n' ';'` turns every matched line's newline into a ';', so
     * the join carries a TRAILING ';'. The set: `^smoke: FAILED --`,
     * `^not ok`, `^\*\* \[`, `^ *FAIL ` -- `ok` and `ci: OK` do NOT
     * match (the C2 bytes below are the driver's). */
    {
        const char *log = "ok 1 - one\n"
                          "not ok 2 - two failed because of a very long "
                          "reason that keeps going and going and going "
                          "to test the cut\n"
                          "*** [FAIL] the suite\n"
                          "ci: OK\n";
        JC_CHECK(jc_gateci_red_join(log, buf, sizeof buf) > 0);
        tc_eq(buf,
              "not ok 2 - two failed because of a very long reason that "
              "keeps going and going and going to test the cut;*** "
              "[FAIL] the suite;");
    }
    /* No matching lines -> the EMPTY join (the reference's empty
     * substitution: the C3 double space beside "Log on the host"). */
    {
        const char *log = "ok 1 - one\nci: OK\n";
        JC_CHECK(jc_gateci_red_join(log, buf, sizeof buf) == 0);
        tc_eq(buf, "");
    }
    /* Only the first four of a longer run match (the reference's
     * `head -4`). */
    {
        const char *log = "not ok 1 - a\nnot ok 2 - b\nnot ok 3 - c\n"
                          "not ok 4 - d\nnot ok 5 - e\n";
        JC_CHECK(jc_gateci_red_join(log, buf, sizeof buf) > 0);
        tc_eq(buf, "not ok 1 - a;not ok 2 - b;not ok 3 - c;not ok 4 - d;");
    }
    /* A matching line cut to 160 (the reference's `cut -c1-160`). */
    {
        char longline[256];
        char out[256];
        size_t i;

        for (i = 0; i < 11; i++) {
            longline[i] = 'n';
        }
        for (i = 11; i < 240; i++) {
            longline[i] = 'x';
        }
        longline[240] = '\0';
        snprintf(buf, sizeof buf, "not ok 1 - %s\nok 2 - fine\n", longline);
        JC_CHECK(jc_gateci_red_join(buf, out, sizeof out) > 0);
        /* the cut 160 line + its tr ';' */
        JC_CHECK(strlen(out) == 161);
        JC_CHECK(out[160] == ';');
    }

    /* The b6 RED line (the stage's own copy of the orchestrator's
     * `red ci` text, so wave 4 lifts it verbatim) + the SV:1238 ask-3
     * excerpt attach. The empty join is the reference's double space. */
    JC_CHECK(jc_gateci_red_line(2, "", "/w/ci/ci-d4caab30.log",
                                "/w/ci/ci-d4caab30.excerpt", buf, sizeof buf)
             > 0);
    tc_eq(buf,
          "  ci: RED make ci rc=2.  Log on the host: "
          "/w/ci/ci-d4caab30.log. (excerpt: /w/ci/ci-d4caab30.excerpt)\n");
    {
        const char *log = "not ok 2 - the one\n*** [FAIL] the suite\n";
        JC_CHECK(jc_gateci_red_line(2, log, "/w/ci/ci-77d2406f.log",
                                    "/w/ci/ci-77d2406f.excerpt", buf,
                                    sizeof buf) > 0);
        tc_eq(buf,
              "  ci: RED make ci rc=2. not ok 2 - the one;*** [FAIL] the "
              "suite; Log on the host: /w/ci/ci-77d2406f.log. (excerpt: "
              "/w/ci/ci-77d2406f.excerpt)\n");
    }

    /* The excerpt's blocks: every `^not ok` line's byte offset + the
     * lines in its block (the ONE line before it + the line + up to the
     * TWENTY lines after it, clipped at the log's end). */
    {
        long starts[8];
        int counts[8];
        const char *log = "ok 1 - one\nnot ok 2 - two\nok 3 - three\n";
        int n = jc_gateci_excerpt_blocks(log, starts, counts, 8);

        JC_CHECK(n == 1);
        /* "not ok 2 - two" starts at byte 11 */
        JC_CHECK(starts[0] == 11);
        /* the one-before (line 1) + the line + the one-after (line 3): 3 */
        JC_CHECK(counts[0] == 3);
    }
    /* The anchor at the first line: no line before it. */
    {
        long starts[8];
        int counts[8];
        const char *log = "not ok 1 - first\nok 2 - second\n";
        int n = jc_gateci_excerpt_blocks(log, starts, counts, 8);

        JC_CHECK(n == 1);
        JC_CHECK(starts[0] == 0);
        JC_CHECK(counts[0] == 2);
    }
    /* The 20-after window clips at the log's end. */
    {
        long starts[8];
        int counts[8];
        char log[512];
        int k;
        int n;
        int w;
        long pos;

        snprintf(log, sizeof log, "not ok 1 - first\n");
        pos = (long)strlen(log);
        for (k = 0; k < 30; k++) {
            w = snprintf(buf, sizeof buf, "ok %d - tail\n", k + 2);
            /* The guard is dead for THIS cell (17 + 8*12 + 22*13 = 399 < 512)
             * but is kept: this log is 512 bytes, not the 44800-byte sink the
             * siblings (:292, :335) append into, so a larger loop bound would
             * overflow it. The strncat the gate's -O2 fortified build rejected
             * (stringop-truncation) was this append; the memcpy + guard is the
             * bounded form. */
            if (w > 0 && pos + (long)w < (long)sizeof log) {
                memcpy(log + pos, buf, (size_t)w);
                pos += (long)w;
            }
        }
        log[pos] = '\0';
        n = jc_gateci_excerpt_blocks(log, starts, counts, 8);
        JC_CHECK(n == 1);
        /* the line + the TWENTY after it (the 30 tail lines clip to 20): 21 */
        JC_CHECK(counts[0] == 21);
    }
    /* No `^not ok` line: the empty excerpt (0); bad args refuse (-1). */
    JC_CHECK(jc_gateci_excerpt_blocks("ok 1 - one\n", NULL, NULL, 0)
             == -1);
    {
        long starts[8];
        int counts[8];
        JC_CHECK(jc_gateci_excerpt_blocks("ok 1 - one\n", starts, counts, 8)
                 == 0);
    }
    /* T8-f2b (Finding 1 of the cb09acca hunk audit): the cap overflow arm --
     * a log with TWO `^not ok` lines at cap 1. The body's `nmatch >= cap`
     * check runs BEFORE `starts[nmatch]` is written (jc_hp_gateci.h's
     * contract: -1 when a match would exceed `cap`), so the second match
     * (line 3) returns -1 without touching index 1 of the caller's
     * one-slot arrays. The stage's step-10 guard (jc_hp_gateci.c, `nm =
     * jc_gateci_excerpt_blocks(logtext, &st1, &ct1, 1); if (nm != 0)`)
     * relies on this: 1 and -1 both write the excerpt, only 0 skips. The
     * teeth are the -1-vs-2 contrast: the cap-2 control proves the log
     * PARSES to two matches (starts 0 and 22 -- line 1, "not ok 1 -
     * a\n", is 13 bytes 0-12, line 2, "ok 1 - x\n", is 9 bytes 13-21,
     * so line 3 starts at 22), so the -1 is the cap overflow, not a
     * parse failure -- a write-then-check body would return 1 or
     * corrupt index 1 at cap 1 and red this cell. */
    {
        long st1;
        long st2[2];
        int ct1;
        int ct2[2];
        const char *log = "not ok 1 - a\nok 1 - x\nnot ok 2 - b\nok 2 - y\n";

        JC_CHECK(jc_gateci_excerpt_blocks(log, &st1, &ct1, 1) == -1);
        JC_CHECK(jc_gateci_excerpt_blocks(log, st2, ct2, 2) == 2);
        JC_CHECK(st2[0] == 0 && st2[1] == 22);
    }

    /* The excerpt's TEXT: the block's lines, each cut to 300 bytes, one
     * blank line between the blocks, a trailing newline. Block 1 (the
     * `not ok` at line 2) takes one-before + itself + 20-after (clipped to
     * the log's end: all 6 lines); block 2 (line 5) takes one-before +
     * itself + the one remaining after (lines 4-6). */
    {
        const char *log = "ok 1 - one\nnot ok 2 - two\nok 3 - three\n"
                          "ok 4 - four\nnot ok 5 - five\nok 6 - six\n";
        JC_CHECK(jc_gateci_excerpt_text(log, buf, sizeof buf) > 0);
        tc_eq(buf,
              "ok 1 - one\nnot ok 2 - two\nok 3 - three\nok 4 - four\n"
              "not ok 5 - five\nok 6 - six\n\n"
              "ok 4 - four\nnot ok 5 - five\nok 6 - six\n");
    }
    /* A 300-byte cut of a long `^not ok` line (the reference's
     * SV:1238 ask-3: each line cut to 300). */
    {
        char longline[400];
        char out[1024];
        size_t i;

        for (i = 0; i < 11; i++) {
            longline[i] = 'n';
        }
        for (i = 11; i < 380; i++) {
            longline[i] = 'x';
        }
        longline[380] = '\0';
        snprintf(buf, sizeof buf, "not ok 1 - %s\nok 2 - after\n", longline);
        JC_CHECK(jc_gateci_excerpt_text(buf, out, sizeof out) > 0);
        /* the cut 300 line + its \n + the one-after + its \n */
        JC_CHECK(strlen(out) == 300 + 1 + 12 + 1);
        JC_CHECK(out[300] == '\n');
        JC_CHECK(strstr(out, "ok 2 - after\n") != NULL);
    }
    /* The empty excerpt: no `^not ok` line -> 0 bytes. */
    JC_CHECK(jc_gateci_excerpt_text("ok 1 - one\n", buf, sizeof buf) == 0);
    tc_eq(buf, "");

    /* F2 (the review's >512-line cell): the ONLY `^not ok` line of a 700-line
     * log is line 600 -- the fixed 512-line split of the current head sees
     * only lines 1-512, so before the streaming fix excerpt_blocks returns 0
     * and excerpt_text returns 0 (empty) (RED by the arrays, not the
     * semantics). After the fix the per-line window is UNCHANGED (brief F2
     * rule 1: the ONE line before + the line + the TWENTY lines after, the
     * existing table's semantics, not the brief's off-by-one step-4 cell):
     * line 599 (before) through line 620 (the 20th after), so `ok 620 - end`
     * IS in the block (counts 22) and `ok 621 - line` / `ok 598 - line` are
     * OUT. Byte offset of line 600's start: k 1-9 are 12 ("ok N - line\n"),
     * k 10-99 are 13 ("ok NN - line\n"), k 100-598 are 14 ("ok NNN -
     * line\n"), k 599 is 16 -> 9 x 12 + 90 x 13 + 499 x 14 + 16 = 8280. */
    {
        char log[700 * 64];
        char out[4096];
        char ln[64];
        long starts[8];
        int counts[8];
        size_t pos = 0;
        int k;
        int w;
        int n;

        for (k = 1; k <= 700; k++) {
            if (k == 599) {
                w = snprintf(ln, sizeof ln, "ok 599 - before\n");
            } else if (k == 600) {
                w = snprintf(ln, sizeof ln,
                             "not ok 600 - the failing driver\n");
            } else if (k == 620) {
                w = snprintf(ln, sizeof ln, "ok 620 - end\n");
            } else if (k > 600 && k <= 619) {
                w = snprintf(ln, sizeof ln, "ok %d - after\n", k);
            } else {
                w = snprintf(ln, sizeof ln, "ok %d - line\n", k);
            }
            memcpy(log + pos, ln, (size_t)w);
            pos += (size_t)w;
        }
        log[pos] = '\0';
        n = jc_gateci_excerpt_blocks(log, starts, counts, 8);
        JC_CHECK(n == 1);
        JC_CHECK(starts[0] == 8280);
        JC_CHECK(counts[0] == 22);
        JC_CHECK(jc_gateci_excerpt_text(log, out, sizeof out) > 0);
        JC_CHECK(strstr(out, "not ok 600 - the failing driver") != NULL);
        JC_CHECK(strstr(out, "ok 599 - before") != NULL);
        JC_CHECK(strstr(out, "ok 619 - after") != NULL);
        JC_CHECK(strstr(out, "ok 620 - end") != NULL);
        JC_CHECK(strstr(out, "ok 621 - line") == NULL);
        JC_CHECK(strstr(out, "ok 598 - line") == NULL);
    }
    /* F2 (the symmetric file-seam cell): the SAME 700-line log written
     * through jc_gateci_excerpt_file (mkstemp, read back, cleaned) -- the
     * stage's unbounded half: the containment is the text's, and the file
     * must carry it past line 512 exactly as the buffer does. */
    {
        char log[700 * 64];
        char out[4096];
        char ln[64];
        char path[64];
        size_t pos = 0;
        int k;
        int w;
        int fd;

        for (k = 1; k <= 700; k++) {
            if (k == 599) {
                w = snprintf(ln, sizeof ln, "ok 599 - before\n");
            } else if (k == 600) {
                w = snprintf(ln, sizeof ln,
                             "not ok 600 - the failing driver\n");
            } else if (k == 620) {
                w = snprintf(ln, sizeof ln, "ok 620 - end\n");
            } else if (k > 600 && k <= 619) {
                w = snprintf(ln, sizeof ln, "ok %d - after\n", k);
            } else {
                w = snprintf(ln, sizeof ln, "ok %d - line\n", k);
            }
            memcpy(log + pos, ln, (size_t)w);
            pos += (size_t)w;
        }
        log[pos] = '\0';
        snprintf(path, sizeof path, "/tmp/jc_test_gateci_f2.XXXXXX");
        fd = mkstemp(path);
        JC_CHECK(fd >= 0);
        JC_CHECK(jc_gateci_excerpt_file(log, path) == JC_OK);
        {
            FILE *f = fdopen(fd, "rb");
            size_t r;

            JC_CHECK(f != NULL);
            if (f != NULL) {
                memset(out, 0, sizeof out);
                r = fread(out, 1, sizeof out - 1, f);
                out[r] = '\0';
                JC_CHECK(fclose(f) == 0);
            }
        }
        JC_CHECK(strstr(out, "not ok 600 - the failing driver") != NULL);
        JC_CHECK(strstr(out, "ok 599 - before") != NULL);
        JC_CHECK(strstr(out, "ok 619 - after") != NULL);
        JC_CHECK(strstr(out, "ok 620 - end") != NULL);
        JC_CHECK(strstr(out, "ok 621 - line") == NULL);
        JC_CHECK(unlink(path) == 0);
    }

    /* The ci-gate.sh:24 KEPT-prefix warning (verbatim, the reference's
     * own diagnostic sentence -- do not strip it). */
    JC_CHECK(jc_gateci_mergebase_warn("a1b2c3d4", "77d2406f", buf,
                                      sizeof buf) > 0);
    tc_eq(buf,
          "ci-gate.sh: GitLab master a1b2c3d4 is not an ancestor of "
          "77d2406f; the sweep uses their merge-base\n");

    /* The ci-gate.sh:25 KEPT-prefix MUTANT_BASE line (the reference's
     * own printf: the FULL 40-hex MB, the 8-hex B). */
    JC_CHECK(jc_gateci_mutant_base_line(
                     "59657355e70b02205d1c141e134f34b18e00651a", "a1b2c3d4",
                     buf, sizeof buf) > 0);
    tc_eq(buf,
          "ci-gate.sh: MUTANT_BASE=59657355e70b02205d1c141e134f34b18e00651a "
          "(GitLab master as push.sh fetched it: a1b2c3d4)\n");

    /* The 16 keyless names (the sweep's unit-pin idiom: the count and
     * the first / last names). */
    {
        const char *names[16];
        int n = jc_gateci_key_names(names, 16);

        JC_CHECK(n == 16);
        JC_CHECK(strcmp(names[0], "JICHI_API_KEY") == 0);
        JC_CHECK(strcmp(names[15], "ZD_TOKEN_FILE") == 0);
        JC_CHECK(jc_gateci_key_names(names, 15) == -1);
        JC_CHECK(jc_gateci_key_names(NULL, 16) == -1);
    }

    /* The run child's env: the 16 names set EMPTY (the keyless scrub)
     * PLUS TMPDIR / MUTANT_BASE / JC_SMOKE_KEEP_GOING, in that order. */
    {
        struct jc_arena *a = jc_arena_new(0);
        struct jc_vec env;
        const char *want[16];
        int i;

        JC_CHECK(a != NULL);
        JC_CHECK(jc_gateci_key_names(want, 16) == 16);
        jc_vec_init(&env, sizeof(char *));
        JC_CHECK(jc_gateci_run_env(a, "/w/tmp",
                                   "59657355e70b02205d1c141e134f34b18e00651a",
                                   "1", &env) == 0);
        JC_CHECK((int)env.len == 19);
        /* the first 16 are the names set EMPTY (a bare "NAME=") */
        for (i = 0; i < 16; i++) {
            JC_CHECK(jc_snprintf(buf, sizeof buf, "%s=", want[i]) > 0);
            JC_CHECK(strcmp(*(const char **)jc_vec_at(&env, i), buf) == 0);
        }
        JC_CHECK(strcmp(*(const char **)jc_vec_at(&env, 16),
                        "TMPDIR=/w/tmp") == 0);
        JC_CHECK(strcmp(*(const char **)jc_vec_at(&env, 17),
                        "MUTANT_BASE=59657355e70b02205d1c141e"
                        "134f34b18e00651a") == 0);
        JC_CHECK(strcmp(*(const char **)jc_vec_at(&env, 18),
                        "JC_SMOKE_KEEP_GOING=1") == 0);
        jc_vec_free(&env);
        jc_arena_free(a);
    }
}
