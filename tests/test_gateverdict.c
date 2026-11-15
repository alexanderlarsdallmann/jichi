/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_gateverdict.c - the gate sweep-verdict's PURE classification core
 * (M1154, H9b, task 3, wave 1): the per-stage line patterns (a missing
 * line, a dirty line), the smoke-mutant arm selection (the OK line, the
 * no-changed-drivers line + its MUTANT_BASE extraction, neither), the
 * flagged pipe (the head -3, the cut -c1-120, the newline->|), the 14:47
 * uses_bin stripping (a `"$BIN"` in a comment does not count, one in a
 * live line does), and the green-note / red-note assembly
 * byte-for-byte. The engine is pure (no git, no I/O) and exported from
 * jc_hp_gateverdict.c for this table; the smoke driver's V1-V12 cells
 * hold the END-TO-END arms (the real git fixture), and this table pins
 * each half at the boundary.
 *
 * Teeth: inverting the pattern order, dropping a line's `$` anchor, or
 * changing the note bytes fires the matching checks below (the driver's
 * V4/V5 pin the green note, V6/V10 the red notes, V12 the flagged join
 * end-to-end). */

#include "jc_test.h"
#include "jc_snprintf.h"   /* jc_snprintf */
#include "../src/harness/jc_hp_gateverdict.h"
#include "jc_vec.h"
#include "jc_mem.h"

#include <string.h>

/* Local forward declaration (-Wmissing-prototypes; the M1124 idiom -- the
 * runner's declaration in test_main.c is a different translation unit). */
void test_gateverdict(void);

/* A fully clean sweep.txt for SHA8 `s` (the reference's shape): every
 * stage's line present and clean, the OK mutant line. */
static void
tgv_clean(char *buf, size_t cap, const char *s)
{
    int n = jc_snprintf(
        buf, cap,
        "  build: %s\n"
        "sweep: ran 99 drivers on %s; red: none\n"
        "  smoke-mutant rc=0 (MUTANT_BASE=77d2406f): mutant: OK\n"
        "  unit san rc=0 37482 checks, 0 failures\n"
        "  unit o2 rc=0 37482 checks, 0 failures\n"
        "  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok\n"
        "  container resolve_timeout_connect rc=0 5 ok, 0 not ok\n"
        "records: /x/verify/sweep-%s\n",
        s, s, s);
    JC_CHECK(n >= 0 && (size_t)n < cap);
}

/* A class with EVERY stage line clean except the smoke-mutant arm (the
 * note-assembly checks below isolate the mutant half that way). */
static void
tgv_clean_but_mut(struct jc_hp_gv_class *c)
{
    memset(c, 0, sizeof *c);
    c->build = 1;
    c->sweep = 1;
    c->san = 1;
    c->o2 = 1;
    c->cf = 1;
    c->cr = 1;
}

void
test_gateverdict(void)
{
    struct jc_hp_gv_class c;
    char buf[4096];
    const char *s = "832c7930";
    char why[512], note[512];   /* the green note is ~195 bytes (the :41 form) */
    int green;
    size_t i;

    /* (0) A fully clean file: every flag set, no flagged line, the OK
     *     mutant arm -> green, empty note. */
    tgv_clean(buf, sizeof buf, s);
    jc_hp_gv_classify(buf, s, &c);
    JC_CHECK(c.build == 1 && c.sweep == 1 && c.mut_ok == 1);
    JC_CHECK(c.mut_none == 0 && c.san == 1 && c.o2 == 1);
    JC_CHECK(c.cf == 1 && c.cr == 1 && c.n_flagged == 0);
    JC_CHECK(jc_hp_gv_verdict(&c, s, GV_MUT_OK, 0, "", 0,
                              0, 0, 0, 0,
                              buf + 2048, 1024, buf + 1024, 1024) == 1);
    JC_CHECK_STR(buf + 1024, "");   /* a green note is empty for the OK arm */

    /* (1) The build line: present and clean vs the SHA8. */
    {
        jc_hp_gv_classify("  build: 832c7930\n", s, &c);
        JC_CHECK(c.build == 1);
    }
    /* A different sha: the build line is NOT for this s -> missing. */
    {
        jc_hp_gv_classify("  build: 9ada7cd7\n", s, &c);
        JC_CHECK(c.build == 0);
    }
    /* Trailing garbage on the build line: the $ anchor fails -> missing. */
    {
        jc_hp_gv_classify("  build: 832c7930 extra\n", s, &c);
        JC_CHECK(c.build == 0);
    }

    /* (2) The sweep line: N must be > 0 (a sweep that ran no driver is
     *     red -- the reference's [1-9][0-9]*). */
    {
        jc_hp_gv_classify("sweep: ran 99 drivers on 832c7930; red: none\n",
                          s, &c);
        JC_CHECK(c.sweep == 1);
    }
    /* ran 0 -> the [1-9] first digit fails. */
    {
        jc_hp_gv_classify("sweep: ran 0 drivers on 832c7930; red: none\n",
                          s, &c);
        JC_CHECK(c.sweep == 0);
    }
    /* red: anything-but-none -> the `red: none` literal fails. */
    {
        jc_hp_gv_classify(
            "sweep: ran 3 drivers on 832c7930; red: foo.sh\n", s, &c);
        JC_CHECK(c.sweep == 0);
    }

    /* (3) The smoke-mutant arm selection. */
    /* The OK line (the reference's :21 `^  smoke-mutant rc=0 .*mutant: OK`):
     * no trailing $, so a longer tail still matches (grep -E unanchored end). */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=77d2406f): mutant: OK\n", s, &c);
        JC_CHECK(c.mut_ok == 1 && c.mut_none == 0);
    }
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=77d2406f): mutant: OK extra\n",
            s, &c);
        JC_CHECK(c.mut_ok == 1);
    }
    /* The no-changed-drivers line (the :22 pattern, checked by hand):
     * captures MUTANT_BASE. */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=832c7930): mutant: no changed"
            " drivers to sweep (0 excluded)\n",
            s, &c);
        JC_CHECK(c.mut_ok == 0 && c.mut_none == 1);
        JC_CHECK(c.mut_nex == 0);
        JC_CHECK_STR(c.mut_mb, "832c7930");
    }
    /* The (1 excluded) tail -> IS the no-changed arm (M1187-b: the prepared
     * fix reads any "(<n> excluded)", the count is not asserted as zero). */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=832c7930): mutant: no changed"
            " drivers to sweep (1 excluded)\n",
            s, &c);
        JC_CHECK(c.mut_ok == 0 && c.mut_none == 1);
        JC_CHECK(c.mut_nex == 1);
    }
    /* MUTANT_BASE too short (6 hex) -> not the arm (needs 7-40). */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=123456): mutant: no changed"
            " drivers to sweep (0 excluded)\n",
            s, &c);
        JC_CHECK(c.mut_none == 0);
    }
    /* rc=1 on the OK line -> neither arm (the reference's `rc=0` literal). */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=1 (MUTANT_BASE=77d2406f): mutant: OK\n", s, &c);
        JC_CHECK(c.mut_ok == 0 && c.mut_none == 0);
    }

    /* (4) The unit + container lines (each its own $ anchor + digit
     *     classes). */
    {
        jc_hp_gv_classify("  unit san rc=0 37482 checks, 0 failures\n",
                          s, &c);
        JC_CHECK(c.san == 1);
    }
    {
        jc_hp_gv_classify("  unit san rc=0 37482 checks, 2 failures\n",
                          s, &c);
        JC_CHECK(c.san == 0);
    }
    {
        jc_hp_gv_classify("  unit o2 rc=0 37482 checks, 0 failures\n",
                          s, &c);
        JC_CHECK(c.o2 == 1);
    }
    {
        jc_hp_gv_classify("  unit o2 rc=1 37482 checks, 0 failures\n",
                          s, &c);
        JC_CHECK(c.o2 == 0);
    }
    {
        jc_hp_gv_classify(
            "  container fetch_ssrf_reuse rc=0 4 ok, 0 not ok\n", s, &c);
        JC_CHECK(c.cf == 1);
    }
    {
        jc_hp_gv_classify(
            "  container fetch_ssrf_reuse rc=0 0 ok, 0 not ok\n", s, &c);
        JC_CHECK(c.cf == 0);   /* > 0 ok required */
    }
    {
        jc_hp_gv_classify(
            "  container fetch_ssrf_reuse rc=0 4 ok, 1 not ok\n", s, &c);
        JC_CHECK(c.cf == 0);
    }
    {
        jc_hp_gv_classify(
            "  container resolve_timeout_connect rc=0 5 ok, 0 not ok\n",
            s, &c);
        JC_CHECK(c.cr == 1);
    }

    /* (5) The flagged pipe: the head -3, the cut -c1-120, the newline->|
     *     (the capture here; the join in the verdict below). */
    /* Two flagged lines captured (the first two, in order). */
    {
        jc_hp_gv_classify(
            "  RED tests/smoke/a.sh rc=1 bad\n"
            "  RED tests/smoke/b.sh rc=1 bad\n",
            s, &c);
        JC_CHECK(c.n_flagged == 2);
        JC_CHECK_STR(c.flagged[0], "  RED tests/smoke/a.sh rc=1 bad");
        JC_CHECK_STR(c.flagged[1], "  RED tests/smoke/b.sh rc=1 bad");
    }
    /* head -3: a FOURTH flagged line is not captured. */
    {
        jc_hp_gv_classify(
            "INVALID x\nFAILED y\nSKIPPED z\n  RED w rc=1\n",
            s, &c);
        JC_CHECK(c.n_flagged == 3);
        JC_CHECK_STR(c.flagged[0], "INVALID x");
        JC_CHECK_STR(c.flagged[1], "FAILED y");
        JC_CHECK_STR(c.flagged[2], "SKIPPED z");
    }
    /* cut -c1-120: a longer flagged line is cut to 120 bytes. */
    {
        char longline[200];
        int n = jc_snprintf(longline, sizeof longline, "INVALID");
        JC_CHECK(n >= 0 && (size_t)n < sizeof longline);
        for (i = 7; i + 1 < sizeof longline; i++) {
            longline[i] = (char)('A' + (char)(i % 26));
        }
        longline[sizeof longline - 2] = '\n';
        longline[sizeof longline - 1] = '\0';
        jc_hp_gv_classify(longline, s, &c);
        JC_CHECK(c.n_flagged == 1);
        JC_CHECK(strlen(c.flagged[0]) == 120);
    }
    /* The flagged join byte-for-byte (all stages clean, one RED line):
     * `; flagged:` then `|` + the line (the :55 tr '\n' '|'). classify is a
     * whole-file pass (it memsets the struct), so feed it a clean file plus
     * the RED line rather than presetting flags. */
    {
        char fl[4096];
        int n = jc_snprintf(fl, sizeof fl, "%s  RED tests/smoke/whatever.sh"
                              " rc=1 some error text\n", buf);
        JC_CHECK(n >= 0 && (size_t)n < sizeof fl);
        jc_hp_gv_classify(fl, s, &c);
        JC_CHECK(c.n_flagged == 1);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_OK, 0, "", 0, 0, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 0);
        /* The :55-56 form: "flagged: " (a space) + the line (its own two
         * leading spaces) + a trailing | (tr converts the final newline). */
        JC_CHECK_STR(
            why,
            "; flagged:   RED tests/smoke/whatever.sh rc=1 some"
            " error text|");
        /* The driver's red: line strips the leading ; (the :58 ${why#;}). */
        JC_CHECK_STR(why + 1,
                     " flagged:   RED tests/smoke/whatever.sh rc=1"
                     " some error text|");
    }

    /* (6) The green note (the second-count agreement arm) -- byte-for-byte
     *     the reference's :41 note. nall=5, nsm=0 (the FV-832c7930 shape
     *     on the LOG's real counts). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, "", 0, 5, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 1);
        JC_CHECK_STR(why, "");
        JC_CHECK_STR(
            note,
            " (smoke-mutant had nothing to sweep: of the 5 file(s) changed"
            " from 77d2406f to 832c7930, none of the 0 smoke script(s)"
            " is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE"
            " table), counted again by git)");
    }
    /* nall=14, nsm=1 (the FV-9ada7cd7 shape): one no-"$BIN" driver. */
    {
        struct jc_hp_gv_class cc;
        memset(&cc, 0, sizeof cc);
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=832c7930): mutant: no changed"
            " drivers to sweep (0 excluded)\n",
            "9ada7cd7", &cc);
        JC_CHECK(cc.mut_none == 1);
        JC_CHECK_STR(cc.mut_mb, "832c7930");
        /* A clean class (the other stages present) for the green verdict;
         * the classify above only proves the MUTANT_BASE extraction. */
        tgv_clean_but_mut(&cc);
        memcpy(cc.mut_mb, "832c7930", 9);
        green = jc_hp_gv_verdict(
            &cc, "9ada7cd7", GV_MUT_ARM, 0, "", 0, 14, 1, 0, 0,
            why, sizeof why, note, sizeof note);
        JC_CHECK(green == 1);
        JC_CHECK_STR(
            note,
            " (smoke-mutant had nothing to sweep: of the 14 file(s) changed"
            " from 832c7930 to 9ada7cd7, none of the 1 smoke script(s)"
            " is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE"
            " table), counted again by git)");
    }

    /* (7) The red notes (byte-for-byte; the stages stay clean so the
     *     smoke-mutant reason is the only one). */
    /* Disagreement: nd > 0 -> the :43 note. */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, "", 0, 3, 1, 1, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(
            why + 1,
            " missing or not clean: smoke-mutant (it swept no driver, but"
            " git counts 1 changed smoke driver(s) that invoke the binary"
            " and are not in mutant-sweep.sh's EXCLUDE table, of 1"
            " changed smoke script(s) in 3 changed file(s) from 77d2406f"
            " to 832c7930)");
    }
    /* Empty batch: nall=0 -> the :43 note with 0 files. */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, s, 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, "", 0, 0, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(
            why + 1,
            " missing or not clean: smoke-mutant (it swept no driver, and"
            " git counts no changed file from 832c7930 to 832c7930)");
    }
    /* A git count failure (the :46 note). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "deadbeef", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 1, "", 0, 0, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(
            why + 1,
            " missing or not clean: smoke-mutant (it swept no driver, and"
            " git could not count the batch's files, or read"
            " scripts/mutant-sweep.sh, from deadbeef to 832c7930)");
    }
    /* A driver git could not read (the :39 note, gerr = " <driver>..."). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, " tests/smoke/x.sh", 0, 5, 1, 0, 0,
            why, sizeof why, note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(
            why + 1,
            " missing or not clean: smoke-mutant (it swept no driver, and"
            " git could not read tests/smoke/x.sh at 832c7930)");
    }
    /* The missing smoke-mutant line (the :49 plain reason). */
    {
        tgv_clean_but_mut(&c);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_MISSING, 0, "", 0, 0, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(why + 1, " missing or not clean: smoke-mutant");
    }

    /* (8) The reason ORDER + the multi-reason join (the reference's own
     *     g() order: build, sweep, mutant, san, o2, cf, cr, flagged). A
     *     file missing EVERYTHING (empty) names them all, in order. */
    {
        jc_hp_gv_classify("", s, &c);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_MISSING, 0, "", 0, 0, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(
            why + 1,
            " missing or not clean: the build line for 832c7930"
            "; missing or not clean: the lint/docs sweep"
            "; missing or not clean: smoke-mutant"
            "; missing or not clean: the SAN unit suite"
            "; missing or not clean: the -O2 unit suite"
            "; missing or not clean: container fetch_ssrf_reuse"
            "; missing or not clean: container resolve_timeout_connect");
    }

    /* (9) uses_bin (the 14:47 selector, scripts/mutant-sweep.sh's
     *     `sed -e 's|#.*$||' | grep -q '"$BIN"'`). */
    /* A live line invoking "$BIN": counts. */
    JC_CHECK(jc_hp_gv_uses_bin("#!/bin/sh\n\"$BIN\" --version\necho done\n")
             == 1);
    /* A `"$BIN"` in a comment ONLY: does not count (the 9ada7cd7 driver). */
    JC_CHECK(jc_hp_gv_uses_bin("#!/bin/sh\n# \"$BIN\" is not invoked\n"
                               "echo done\n")
             == 0);
    /* The comment cut is per-line: a `#` mid-line erases a `"$BIN"` AFTER
     * the #, but not one before it. */
    JC_CHECK(jc_hp_gv_uses_bin("echo hi # \"$BIN\" in a comment\n") == 0);
    JC_CHECK(jc_hp_gv_uses_bin("\"$BIN\" # comment after\n") == 1);
    /* No `"$BIN"` at all. */
    JC_CHECK(jc_hp_gv_uses_bin("#!/bin/sh\necho no bin here\n") == 0);
    /* The literal is `"$BIN"` with the quotes: `$BIN` without does not. */
    JC_CHECK(jc_hp_gv_uses_bin("echo $BIN\n") == 0);
    /* An empty / NULL body. */
    JC_CHECK(jc_hp_gv_uses_bin("") == 0);
    JC_CHECK(jc_hp_gv_uses_bin(NULL) == 0);

    /* (10) smoke_union (the reference's :31 `printf | grep
     *      '^tests/smoke/.*\\.sh$' | sort -u`). */
    {
        struct jc_vec out;
        struct jc_arena *a = jc_arena_new(0);
        jc_vec_init(&out, sizeof (const char *));
        JC_REQUIRE(a != NULL);
        /* Two captures, one shared, one tests/smoke only in the own. */
        jc_hp_gv_smoke_union("tests/smoke/zeta.sh\ntests/smoke/alpha.sh\n"
                             "docs/x.md\n",
                             "tests/smoke/alpha.sh\n"
                             "tests/smoke/mid.sh\nsrc/c.c\n",
                             a, &out);
        JC_CHECK(out.len == 3);
        JC_CHECK_STR(*(const char **)jc_vec_at(&out, 0), "tests/smoke/alpha.sh");
        JC_CHECK_STR(*(const char **)jc_vec_at(&out, 1), "tests/smoke/mid.sh");
        JC_CHECK_STR(*(const char **)jc_vec_at(&out, 2), "tests/smoke/zeta.sh");
        /* Non-.sh and non-tests/smoke are excluded; the .sh suffix is
         * required (a tests/smoke/x.c is not a driver). The union clears its
         * own out vec at the top, so call it again WITHOUT freeing (a free
         * here would leave the next clear on freed memory). */
        jc_hp_gv_smoke_union("src/harness/x.c\ntests/smoke/y.c\n",
                             "tests/smoke/run.sh\n",
                             a, &out);
        JC_CHECK(out.len == 1);
        JC_CHECK_STR(*(const char **)jc_vec_at(&out, 0), "tests/smoke/run.sh");
        jc_vec_free(&out);
        jc_arena_free(a);
    }

    /* (11) The driver's V4/V5 green-note shapes (the LOG 18:40 entry's
     *      byte shapes, on the driver's p-chain counts). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, "", 0, 2, 0, 0, 0, why, sizeof why,
            note, sizeof note);
        JC_CHECK(green == 1);
        JC_CHECK_STR(
            note,
            " (smoke-mutant had nothing to sweep: of the 2 file(s) changed"
            " from 77d2406f to 832c7930, none of the 0 smoke script(s)"
            " is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE"
            " table), counted again by git)");
    }
    /* V5's shape: nall=2, nsm=1. */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "832c7930", 9);
        green = jc_hp_gv_verdict(
            &c, "9ada7cd7", GV_MUT_ARM, 0, "", 0, 2, 1, 0, 0,
            why, sizeof why, note, sizeof note);
        JC_CHECK(green == 1);
        JC_CHECK_STR(
            note,
            " (smoke-mutant had nothing to sweep: of the 2 file(s) changed"
            " from 832c7930 to 9ada7cd7, none of the 1 smoke script(s)"
            " is swept by mutant-sweep.sh (its uses_bin rule + EXCLUDE"
            " table), counted again by git)");
    }

    /* (12) The (12 excluded) tail -> mut_nex == 12 (M1187-b delta 1). */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=832c7930): mutant: no changed"
            " drivers to sweep (12 excluded)\n",
            s, &c);
        JC_CHECK(c.mut_none == 1 && c.mut_nex == 12);
        JC_CHECK_STR(c.mut_mb, "832c7930");
    }
    /* The bare tail (no `(<n> excluded)` at all) -> not the arm. */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=0 (MUTANT_BASE=832c7930): mutant: no changed"
            " drivers to sweep\n",
            s, &c);
        JC_CHECK(c.mut_none == 0);
    }
    /* The `mutant: FAILED` line -> not the no-changed arm. */
    {
        jc_hp_gv_classify(
            "  smoke-mutant rc=1 (MUTANT_BASE=832c7930): mutant: FAILED --"
            " foo.sh stayed green (1 swept, 0 excluded)\n",
            s, &c);
        JC_CHECK(c.mut_none == 0 && c.mut_ok == 0);
    }

    /* (13) EXCLUDE-table parse: the live two-line table (delta 3). */
    {
        struct jc_arena *a = jc_arena_new(0);
        char *table;
        int r = jc_hp_gv_excl_parse(
            "# a comment line\n"
            "EXCLUDE='faults faults_net faults_net_midstream smoke_lint"
            " project_records_lint\ndoc_commands_lint'\nrest\n",
            a, &table);
        JC_CHECK(r == 0);
        JC_CHECK_STR(table,
            "faults faults_net faults_net_midstream smoke_lint"
            " project_records_lint doc_commands_lint ");
        jc_arena_free(a);
    }
    /* One-line table: the closing quote on the first line. */
    {
        struct jc_arena *a = jc_arena_new(0);
        char *table;
        int r = jc_hp_gv_excl_parse(
            "EXCLUDE='faults smoke_lint'\n",
            a, &table);
        JC_CHECK(r == 0);
        JC_CHECK_STR(table, "faults smoke_lint ");
        jc_arena_free(a);
    }
    /* No EXCLUDE line: the table reads empty (the caller's table_empty arm). */
    {
        struct jc_arena *a = jc_arena_new(0);
        char *table;
        int r = jc_hp_gv_excl_parse(
            "# just a comment\nnothing here\n", a, &table);
        JC_CHECK(r == 0);
        JC_CHECK_STR(table, "");
        jc_arena_free(a);
    }

    /* (14) EXCLUDE-table membership: the prefix-name must NOT match
     *     (`smoke_lint` vs `smoke_lint_extra`). */
    {
        JC_CHECK(jc_hp_gv_excl_contains(
            "faults smoke_lint project_records_lint doc_commands_lint ",
            "smoke_lint") == 1);
        JC_CHECK(jc_hp_gv_excl_contains(
            "faults smoke_lint project_records_lint doc_commands_lint ",
            "smoke_lint_extra") == 0);
        JC_CHECK(jc_hp_gv_excl_contains(
            "faults smoke_lint project_records_lint doc_commands_lint ",
            "faults") == 1);
        JC_CHECK(jc_hp_gv_excl_contains("", "smoke_lint") == 0);
    }

    /* (15) Six-outcome order: gerr + table_empty -> gerr wins
     *     (the if-chain checks gerr BEFORE table_empty). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, " tests/smoke/x.sh", 1,
            5, 1, 0, 0, why, sizeof why, note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(why + 1,
            " missing or not clean: smoke-mutant (it swept no driver, and"
            " git could not read tests/smoke/x.sh at 832c7930)");
    }
    /* Six-outcome: table_empty alone (no gerr, no cnt_fail) -> the
     *     empty-table arm (V14 shape, delta 5). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, "", 1,
            5, 1, 0, 0, why, sizeof why, note, sizeof note);
        JC_CHECK(green == 0);
        JC_CHECK_STR(why + 1,
            " missing or not clean: smoke-mutant (it swept no driver, and"
            " the EXCLUDE table of scripts/mutant-sweep.sh at 832c7930"
            " read as empty)");
    }
    /* Six-outcome: green guard but nx != mut_nex -> the cross-check arm
     *     (V13 shape, delta 5). */
    {
        tgv_clean_but_mut(&c);
        memcpy(c.mut_mb, "77d2406f", 9);
        c.mut_nex = 2;   /* the line said (2 excluded) */
        green = jc_hp_gv_verdict(
            &c, s, GV_MUT_ARM, 0, "", 0,
            5, 1, 0, 1, why, sizeof why, note, sizeof note);
        /* nx=1 != mut_nex=2: falls through the green guard to cross-check */
        JC_CHECK(green == 0);
        JC_CHECK_STR(why + 1,
            " missing or not clean: smoke-mutant (it reports 2 excluded,"
            " but git counts 1 changed smoke driver(s) that invoke the"
            " binary and are in mutant-sweep.sh's EXCLUDE table, from"
            " 77d2406f to 832c7930)");
    }
}
