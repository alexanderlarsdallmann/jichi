/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_pagestretch.c - the model-failure stretch (M1213, batch H12; SV:0433's
 * option (a), S2 of 3): the unit table for the exported seam in
 * jc_hp_pagestretch.c. The engine is pure (no git, no I/O): it builds
 * self-contained fixtures under $TMPDIR (the /tmp/stt* dirs are the lead's
 * scratch copies; the test reads nothing from them) and asserts the exact
 * bytes the product prints for the nine reference lines plus the -1 arms. */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_test.h"
#include "../src/harness/jc_hp_pagestretch.h"

/* Forward declaration (the T2/T3/T5/T8 test idiom). */
void test_pagestretch(void);

#define TC_BUF (16 * 1024)

static void
tc_eq(const char *buf, const char *want)
{
    if (strcmp(buf, want) != 0) {
        printf("  Expected: \"%s\"\n", want);
        printf("  Got:      \"%s\"\n", buf);
    }
    JC_CHECK(strcmp(buf, want) == 0);
}

/* Build a fixture under $TMPDIR/sttXXXXX with the embedded telemetry.
 * Supports up to 3 run dirs; file2 is NULL for only two run dirs, NON-NULL for a third empty dir (no telemetry.jsonl). */
static int
make_fixture(const char *tmpdir, const char *name,
             const char *telemetry0, const char *telemetry1,
             const char *file0, const char *file1,
             const char *file2)
{
    char dir[1024];
    char runsdir[1024];
    char rundir0[1024], rundir1[1024], rundir2[1024];
    char f0[1024], f1[1024];
    FILE *fp;
    int rc = 0;

    jc_snprintf(dir, sizeof dir, "%s/%s", tmpdir, name);
    if (mkdir(dir, 0755) != 0) return -1;

    jc_snprintf(runsdir, sizeof runsdir, "%s/runs", dir);
    if (mkdir(runsdir, 0755) != 0) { rc = -1; goto rm_dir; }

    if (file0) {
        jc_snprintf(rundir0, sizeof rundir0, "%s/runs/%s", dir, file0);
        if (mkdir(rundir0, 0755) != 0) { rc = -1; goto rm_runs; }
        jc_snprintf(f0, sizeof f0, "%s/telemetry.jsonl", rundir0);
        fp = fopen(f0, "wb");
        if (!fp) { rc = -1; goto rm_rundir0; }
        fprintf(fp, "%s", telemetry0);
        fclose(fp);
    }

    if (file1) {
        jc_snprintf(rundir1, sizeof rundir1, "%s/runs/%s", dir, file1);
        if (mkdir(rundir1, 0755) != 0) { rc = -1; goto rm_f0; }
        jc_snprintf(f1, sizeof f1, "%s/telemetry.jsonl", rundir1);
        fp = fopen(f1, "wb");
        if (!fp) { rc = -1; goto rm_rundir1; }
        fprintf(fp, "%s", telemetry1);
        fclose(fp);
    }

    if (file2) {
        jc_snprintf(rundir2, sizeof rundir2, "%s/runs/%s", dir, file2);
        if (mkdir(rundir2, 0755) != 0) { rc = -1; goto rm_f1; }
    }
    return 0;

rm_f1:
    if (file1) {
        jc_snprintf(f1, sizeof f1, "%s/telemetry.jsonl", rundir1);
        unlink(f1);
        rmdir(rundir1);
    }
rm_rundir1:
    if (file1) {
        jc_snprintf(rundir1, sizeof rundir1, "%s/runs/%s", dir, file1);
        rmdir(rundir1);
    }
rm_f0:
    if (file0) {
        jc_snprintf(f0, sizeof f0, "%s/telemetry.jsonl", rundir0);
        unlink(f0);
        rmdir(rundir0);
    }
rm_rundir0:
    if (file0) {
        rmdir(rundir0);
    }
rm_runs:
    rmdir(runsdir);
rm_dir:
    rmdir(dir);
    return rc;
}

static void
test_fixture(const char *tmpdir, const char *name,
             const char *telemetry0, const char *telemetry1,
             const char *file0, const char *file1,
             const char *file2,
             const char *expected)
{
    char dir[1024];
    struct jc_arena *a;
    char line[TC_BUF];
    int rc;

    /* Build the fixture. */
    if (make_fixture(tmpdir, name, telemetry0, telemetry1, file0, file1, file2) != 0) {
        JC_CHECK(0 == 1); /* fail early if fixture build failed */
    }

    jc_snprintf(dir, sizeof dir, "%s/%s", tmpdir, name);

    /* Call the product. */
    a = jc_arena_new(0);
    JC_CHECK(a != NULL);

    rc = jc_pagestretch_line(dir, a, line, sizeof line);
    JC_CHECK(rc == 0);
    tc_eq(line, expected);

    jc_arena_free(a);

    /* Remove the fixture. */
    jc_snprintf(dir, sizeof dir, "%s/%s", tmpdir, name);
    jc_test_rm_rf(dir);
}

/* The nine reference lines from the oracle (page_stretch_oracle.c on the
 * lead's nine fixtures, run 0101 LOG). */
void test_pagestretch(void)
{
    struct jc_arena *a;
    char line[TC_BUF];
    int rc;

    /* --- the nine reference lines (b1-b5 of notes/M1213.md) --- */

    /* sttest: 1 0 1298 1350 2 503/connect:1,503/stall:1 0100
     * Fixture: /tmp/sttest (lead's scratch copy, read-only reference).
     * runs/0098 has: ts=1000 ok, ts=1200.5 fail connect; runs/0099 has:
     * ts=1300 fail connect depth 2, ts=1350 fail stall depth 1.
     * runs/0100 is an empty directory (no telemetry.jsonl) -- the structural tooth:
     * b1's runs set = the LAST TWO 4-digit-named entries of <drive>/runs (sorted, len==4 and isdigit), missing telemetry files skipped.
     * The product reads only the LAST TWO (0099, 0100), so 0098's ok call at ts=1000 is excluded
     * (last_ok=0) and the last run is 0100. Reference line, M1213 S2-b; S2's re-creation
     * dropped the 0100 slot. */
    test_fixture(jc_test_tmpdir(), "sttest",
                 "{\"event\":\"model_call\",\"ts\":1000.0,\"latency_ms\":1500.0,\"ok\":true,\"status\":200,\"depth\":0}\n"
                 "{\"event\":\"other\",\"ts\":1100,\"ok\":false,\"status\":500}\n"
                 "{\"event\":\"model_call\",\"ts\":1200.5,\"latency_ms\":1000.0,\"ok\":false,\"status\":503,\"fail_class\":\"connect\",\"depth\":0}\n",
                 "{\"event\":\"model_call\",\"ts\":1300.0,\"latency_ms\":2000.0,\"ok\":false,\"status\":503,\"fail_class\":\"connect\",\"depth\":2}\n"
                 "{\"event\":\"model_call\",\"ts\":1350.0,\"latency_ms\":0,\"ok\":false,\"status\":503,\"fail_class\":\"stall\",\"depth\":1}\n",
                 "0098", "0099", "0100",
                 "1 0 1298 1350 2 503/connect:1,503/stall:1 0100\n");

    /* sttest2: 0 0 0 0 0 - -
     * Fixture: /tmp/sttest2 (empty runs set). */
    test_fixture(jc_test_tmpdir(), "sttest2",
                 NULL, NULL, NULL, NULL, NULL,
                 "0 0 0 0 0 - -\n");

    /* sttest3: 0 42 0 0 0 - 0001
     * Fixture: /tmp/sttest3 (one run 0001, one ok call at ts=42). */
    test_fixture(jc_test_tmpdir(), "sttest3",
                 "{\"event\":\"model_call\",\"ts\":42.0,\"latency_ms\":10,\"ok\":true}\n",
                 NULL,
                 "0001", NULL, NULL,
                 "0 42 0 0 0 - 0001\n");

    /* sttest4: 1 100 150 150 1 None/None:1 0001
     * Fixture: /tmp/sttest4 (one run, ts=100 ok, ts=150 fail, no status/fail_class). */
    test_fixture(jc_test_tmpdir(), "sttest4",
                 "{\"event\":\"model_call\",\"ts\":100.0,\"ok\":true}\n"
                 "{\"event\":\"model_call\",\"ts\":150.0,\"ok\":false}\n",
                 NULL,
                 "0001", NULL, NULL,
                 "1 100 150 150 1 None/None:1 0001\n");

    /* sttest5: 1 100 110 140 4 503/stall:2,503/connect:2 0001
     * Fixture: /tmp/sttest5 (one run, ok at 100, four fails at 110,120,130,140). */
    test_fixture(jc_test_tmpdir(), "sttest5",
                 "{\"event\":\"model_call\",\"ts\":100,\"ok\":true}\n"
                 "{\"event\":\"model_call\",\"ts\":110,\"ok\":false,\"status\":503,\"fail_class\":\"stall\"}\n"
                 "{\"event\":\"model_call\",\"ts\":120,\"ok\":false,\"status\":503,\"fail_class\":\"connect\"}\n"
                 "{\"event\":\"model_call\",\"ts\":130,\"ok\":false,\"status\":503,\"fail_class\":\"stall\"}\n"
                 "{\"event\":\"model_call\",\"ts\":140,\"ok\":false,\"status\":503,\"fail_class\":\"connect\"}\n",
                 NULL,
                 "0001", NULL, NULL,
                 "1 100 110 140 4 503/stall:2,503/connect:2 0001\n");

    /* stt6: 1 0 300 300 1 503/stall:1 0002
     * Fixture: /tmp/stt6 (two runs 0001 and 0002; run 0001 has ts="120" as STRING
     * which should be SKIPPED, run 0002 has ts=300 fail). */
    test_fixture(jc_test_tmpdir(), "stt6",
                 "{\"event\":\"model_call\",\"ts\":\"120\",\"ok\":false,\"status\":503,\"fail_class\":\"connect\"}\n",
                 "{\"event\":\"model_call\",\"ts\":300,\"ok\":false,\"status\":503,\"fail_class\":\"stall\"}\n",
                 "0001", "0002", NULL,
                 "1 0 300 300 1 503/stall:1 0002\n");

    /* stta: 0 50 0 0 0 - 0001  (M1213 S1-b: the round-to-even tooth, 50 not 51)
     * Fixture: /tmp/stta (one run 0001, ts=50.5 ok -> round half to even = 50). */
    test_fixture(jc_test_tmpdir(), "stta",
                 "{\"event\":\"model_call\",\"ts\":50.5,\"ok\":true}\n",
                 NULL,
                 "0001", NULL, NULL,
                 "0 50 0 0 0 - 0001\n");

    /* sttb: 1 100 105 106 1 503/stall:1 0001  (M1213 S1-b: the 106 tooth, %.0f byte guarantee)
     * Fixture: /tmp/sttb (one run 0001, ts=100 ok, ts=105.5 fail with latency_ms=10
     * so sent=105.5 -> round to 106). */
    test_fixture(jc_test_tmpdir(), "sttb",
                 "{\"event\":\"model_call\",\"ts\":100.0,\"ok\":true}\n"
                 "{\"event\":\"model_call\",\"ts\":105.5,\"latency_ms\":10,\"ok\":false,\"status\":503,\"fail_class\":\"stall\"}\n",
                 NULL,
                 "0001", NULL, NULL,
                 "1 100 105 106 1 503/stall:1 0001\n");

    /* stt9: 0 0 0 0 0 - -
     * Fixture: /tmp/stt9 (runs dir with a non-4-digit name 12345, nothing else). */
    /* Note: 12345 is 5 digits, not 4, so it doesn't qualify. runs set is empty. */
    {
        char dir[1024];
        char runsdir[1024];
        char rundir[1024];

        jc_snprintf(dir, sizeof dir, "%s/stt9", jc_test_tmpdir());
        mkdir(dir, 0755);
        jc_snprintf(runsdir, sizeof runsdir, "%s/runs", dir);
        mkdir(runsdir, 0755);
        jc_snprintf(rundir, sizeof rundir, "%s/runs/12345", dir);
        mkdir(rundir, 0755);

        a = jc_arena_new(0);
        JC_CHECK(a != NULL);

        rc = jc_pagestretch_line(dir, a, line, sizeof line);
        JC_CHECK(rc == 0);
        tc_eq(line, "0 0 0 0 0 - -\n");

        jc_arena_free(a);

        jc_test_rm_rf(dir);
    }

    /* --- the -1 arms (beyond the reference, the contract's own teeth) --- */

    /* NULL drive_dir returns -1. */
    a = jc_arena_new(0);
    JC_CHECK(a != NULL);
    rc = jc_pagestretch_line(NULL, a, line, sizeof line);
    JC_CHECK(rc == -1);
    jc_arena_free(a);

    /* NULL arena returns -1. */
    rc = jc_pagestretch_line("/tmp", NULL, line, sizeof line);
    JC_CHECK(rc == -1);

    /* NULL line_out returns -1. */
    a = jc_arena_new(0);
    JC_CHECK(a != NULL);
    rc = jc_pagestretch_line("/tmp", a, NULL, sizeof line);
    JC_CHECK(rc == -1);
    jc_arena_free(a);

    /* too-small cap returns -1 (the empty-drive line is 15 bytes + NUL). */
    {
        char dir[1024];
        jc_snprintf(dir, sizeof dir, "%s/empty_drive_cap", jc_test_tmpdir());
        mkdir(dir, 0755);
        a = jc_arena_new(0);
        JC_CHECK(a != NULL);
        rc = jc_pagestretch_line(dir, a, line, 10); /* cap 10 < 15 */
        JC_CHECK(rc == -1);
        jc_arena_free(a);
        jc_test_rm_rf(dir);
    }

    /* Empty drive dir (no runs) should still produce the line. */
    {
        char dir[1024];
        jc_snprintf(dir, sizeof dir, "%s/empty_drive", jc_test_tmpdir());
        mkdir(dir, 0755);

        a = jc_arena_new(0);
        JC_CHECK(a != NULL);

        rc = jc_pagestretch_line(dir, a, line, sizeof line);
        JC_CHECK(rc == 0);
        tc_eq(line, "0 0 0 0 0 - -\n");

        jc_arena_free(a);
        jc_test_rm_rf(dir);
    }

    /* stt65: 1 0 1 65 65 65 distinct status/fail_class strings, one fail each.
     * Fixture: /tmp/stt65 (one run 0001, 65 fails with distinct classes).
     * Each fail has a unique status/fail_class: 503/fail000 through 503/fail064.
     * All counts are 1, so first-seen order applies (503/fail000 first, then 001-064). */
    {
        char dir[1024];
        char runsdir[1024];
        char rundir[1024];
        char f0[1024];
        FILE *fp;
        int i;
        char want[8192];
        int len = 0;

        jc_snprintf(dir, sizeof dir, "%s/stt65", jc_test_tmpdir());
        mkdir(dir, 0755);
        jc_snprintf(runsdir, sizeof runsdir, "%s/runs", dir);
        mkdir(runsdir, 0755);
        jc_snprintf(rundir, sizeof rundir, "%s/runs/0001", dir);
        mkdir(rundir, 0755);
        jc_snprintf(f0, sizeof f0, "%s/telemetry.jsonl", rundir);
        fp = fopen(f0, "wb");
        if (JC_REQUIRE(fp != NULL)) { /* a guard (M729) */
            /* 65 distinct classes, one fail each (ts 1..65, fail000..fail064). */
            for (i = 0; i < 65; i++) {
                fprintf(fp, "{\"event\":\"model_call\",\"ts\":%d,\"ok\":false,\"status\":503,\"fail_class\":\"fail%03d\"}\n", i + 1, i);
            }
            fclose(fp);
        }

        /* Build expected line: 1 0 1 65 65 <cls> 0001\n */
        len = jc_snprintf(want, sizeof want, "1 0 1 65 65 ");
        for (i = 0; i < 65; i++) {
            if (i > 0) {
                len += jc_snprintf(want + len, sizeof want - (size_t)len, ",");
            }
            len += jc_snprintf(want + len, sizeof want - (size_t)len, "503/fail%03d:1", i);
        }
        len += jc_snprintf(want + len, sizeof want - (size_t)len, " 0001\n");

        a = jc_arena_new(0);
        JC_CHECK(a != NULL);

        rc = jc_pagestretch_line(dir, a, line, sizeof line);
        JC_CHECK(rc == 0);
        tc_eq(line, want);

        jc_arena_free(a);

        jc_test_rm_rf(dir);
    }
    /* stt66: 1 0 1 100 66 66 distinct status/fail_class strings (65 unique + 1 repeated).
     * Fixture: /tmp/stt66 (one run 0001, 66 fails: 65 unique + fail000 repeated to make it most common). */
    {
        char dir[1024];
        char runsdir[1024];
        char rundir[1024];
        char f0[1024];
        FILE *fp;
        int i;
        char want[8192];
        int len = 0;

        jc_snprintf(dir, sizeof dir, "%s/stt66", jc_test_tmpdir());
        mkdir(dir, 0755);
        jc_snprintf(runsdir, sizeof runsdir, "%s/runs", dir);
        mkdir(runsdir, 0755);
        jc_snprintf(rundir, sizeof rundir, "%s/runs/0001", dir);
        mkdir(rundir, 0755);
        jc_snprintf(f0, sizeof f0, "%s/telemetry.jsonl", rundir);
        fp = fopen(f0, "wb");
        if (JC_REQUIRE(fp != NULL)) { /* a guard (M729) */
            /* 65 distinct classes (ts 1..65, fail000..fail064), plus one repeated fail000 (ts 100) */
            for (i = 0; i < 65; i++) {
                fprintf(fp, "{\"event\":\"model_call\",\"ts\":%d,\"ok\":false,\"status\":503,\"fail_class\":\"fail%03d\"}\n", i + 1, i);
            }
            /* Add the repeated class to make it most common (count=2) */
            fprintf(fp, "{\"event\":\"model_call\",\"ts\":100,\"ok\":false,\"status\":503,\"fail_class\":\"fail000\"}\n");
            fclose(fp);
        }

        /* Build expected line: 1 0 1 100 66 <cls> 0001\n
         * cls = 503/fail000:2 first (most common), then 503/fail001:1..503/fail064:1 */
        len = jc_snprintf(want, sizeof want, "1 0 1 100 66 ");
        len += jc_snprintf(want + len, sizeof want - (size_t)len, "503/fail000:2");
        for (i = 1; i < 65; i++) {
            len += jc_snprintf(want + len, sizeof want - (size_t)len, ",503/fail%03d:1", i);
        }
        len += jc_snprintf(want + len, sizeof want - (size_t)len, " 0001\n");

        a = jc_arena_new(0);
        JC_CHECK(a != NULL);

        rc = jc_pagestretch_line(dir, a, line, sizeof line);
        JC_CHECK(rc == 0);
        tc_eq(line, want);

        jc_arena_free(a);

        jc_test_rm_rf(dir);
    }
    /* stt67: 300 distinct status/fail_class strings (over the 2048 B cls buffer cap).
     * Fixture: /tmp/stt67 (one run 0001, 300 fails with distinct classes).
     * The cls text is ~3400 bytes > 2048, so jc_pagestretch_line must return -1.
     * Assert JC_CHECK(rc == -1) (and that it did NOT return 0). */
    {
        char dir[1024];
        char runsdir[1024];
        char rundir[1024];
        char f0[1024];
        FILE *fp;
        int i;
        int expected_rc = -1;

        jc_snprintf(dir, sizeof dir, "%s/stt67", jc_test_tmpdir());
        mkdir(dir, 0755);
        jc_snprintf(runsdir, sizeof runsdir, "%s/runs", dir);
        mkdir(runsdir, 0755);
        jc_snprintf(rundir, sizeof rundir, "%s/runs/0001", dir);
        mkdir(rundir, 0755);
        jc_snprintf(f0, sizeof f0, "%s/telemetry.jsonl", rundir);
        fp = fopen(f0, "wb");
        if (JC_REQUIRE(fp != NULL)) { /* a guard (M729) */
            /* 300 distinct classes (ts 1..300, fail000..fail299). */
            for (i = 0; i < 300; i++) {
                fprintf(fp, "{\"event\":\"model_call\",\"ts\":%d,\"ok\":false,\"status\":503,\"fail_class\":\"fail%03d\"}\n", i + 1, i);
            }
            fclose(fp);
        }

        a = jc_arena_new(0);
        JC_CHECK(a != NULL);

        rc = jc_pagestretch_line(dir, a, line, sizeof line);
        /* The pre-fix code writes OOB and returns 0; the fix returns -1 on overflow */
        JC_CHECK(rc == expected_rc);

        jc_arena_free(a);

        jc_test_rm_rf(dir);
    }
}
