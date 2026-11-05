/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_eventlog.c - the opt-in JSONL event sink (jc_eventlog). */

#include "jc_test.h"
#include "jc_eventlog.h"
#include "jc_version.h"
#include "jc_log.h"      /* M1137 (C-19): jc_redact_register/active */
#include "jc_fault.h"    /* M1137 (C-19): JC_FAULT_REDACT probe */
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* A disabled sink: begin() yields NULL, end()/full() are safe no-ops. */
static void test_disabled(void)
{
    struct jc_eventlog log;
    cJSON *o;

    jc_eventlog_disable(&log);
    JC_CHECK(jc_eventlog_full(&log) == 0);
    o = jc_eventlog_begin(&log, "x");
    JC_CHECK(o == NULL);
    jc_eventlog_end(&log, o);   /* tolerates NULL log-target and NULL o */
    jc_eventlog_end(NULL, NULL);

    /* OFF level leaves it disabled even with a path. */
    JC_CHECK(jc_eventlog_open(&log, jc_test_tmp("jichi_should_not_exist.jsonl"),
                              "sid", JC_EVENTLOG_OFF) != JC_OK);
    JC_CHECK(jc_eventlog_begin(&log, "x") == NULL);
}

/* begin() stamps v/ts/sid/seq/event; seq increments; full() reflects the tier. */
static void test_begin_fields_and_tier(void)
{
    struct jc_eventlog log;
    const char *path = jc_test_tmp("jichi_test_eventlog_dir/events.jsonl");
    cJSON *o;
    cJSON *seq;

    remove(path);
    /* Nested path also exercises parent-dir creation. */
    JC_CHECK(jc_eventlog_open(&log, path, "sess-1", JC_EVENTLOG_METRICS)
             == JC_OK);
    JC_CHECK(jc_eventlog_full(&log) == 0); /* metrics tier: no content */

    o = jc_eventlog_begin(&log, "model_call");
    if (JC_REQUIRE(o != NULL)) { /* a guard (M729) */
        JC_CHECK(cJSON_GetObjectItem(o, "v") != NULL);
        JC_CHECK(cJSON_GetObjectItem(o, "ts") != NULL);
        JC_CHECK_STR(cJSON_GetObjectItem(o, "event")->valuestring, "model_call");
        JC_CHECK_STR(cJSON_GetObjectItem(o, "sid")->valuestring, "sess-1");
    }
    seq = cJSON_GetObjectItem(o, "seq");
    JC_CHECK(seq != NULL && (long)seq->valuedouble == 0);
    cJSON_AddNumberToObject(o, "in_tok", 123);
    jc_eventlog_end(&log, o);

    /* Second event: seq advances to 1. */
    o = jc_eventlog_begin(&log, "tool_call");
    if (JC_REQUIRE(o != NULL)) { /* a guard (M729) */
        JC_CHECK((long)cJSON_GetObjectItem(o, "seq")->valuedouble == 1);
    }
    jc_eventlog_end(&log, o);

    jc_eventlog_close(&log);

    /* Read the file back: two parseable JSONL lines with the expected fields. */
    {
        FILE *f = fopen(path, "rb");
        char line[4096];
        int n = 0;
        JC_CHECK(f != NULL);
        while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
            cJSON *root = cJSON_Parse(line);
            JC_CHECK(root != NULL);
            if (root != NULL) {
                JC_CHECK(cJSON_GetObjectItem(root, "event") != NULL);
                JC_CHECK(cJSON_GetObjectItem(root, "seq") != NULL);
                JC_CHECK((int)cJSON_GetObjectItem(root, "v")->valuedouble
                         == JC_EVENTLOG_SCHEMA);
                cJSON_Delete(root);
            }
            n++;
        }
        if (f != NULL) { fclose(f); }
        JC_CHECK(n == 2);
    }
    remove(path);
}

/* The full tier reports content capture enabled. */
static void test_full_tier(void)
{
    struct jc_eventlog log;
    const char *path = jc_test_tmp("jichi_test_eventlog_full.jsonl");

    remove(path);
    JC_CHECK(jc_eventlog_open(&log, path, NULL, JC_EVENTLOG_FULL) == JC_OK);
    JC_CHECK(jc_eventlog_full(&log) == 1);
    /* No sid configured => no "sid" field stamped. */
    {
        cJSON *o = jc_eventlog_begin(&log, "turn");
        JC_CHECK(o != NULL && cJSON_GetObjectItem(o, "sid") == NULL);
        jc_eventlog_end(&log, o);
    }
    jc_eventlog_close(&log);
    remove(path);
}

/* Level name <-> tier mapping. */
static void test_level_parse(void)
{
    JC_CHECK(jc_eventlog_level_parse(NULL) == JC_EVENTLOG_OFF);
    JC_CHECK(jc_eventlog_level_parse("") == JC_EVENTLOG_OFF);
    JC_CHECK(jc_eventlog_level_parse("off") == JC_EVENTLOG_OFF);
    JC_CHECK(jc_eventlog_level_parse("metrics") == JC_EVENTLOG_METRICS);
    JC_CHECK(jc_eventlog_level_parse("full") == JC_EVENTLOG_FULL);
    JC_CHECK(jc_eventlog_level_parse("bogus") == -1);
    JC_CHECK_STR(jc_eventlog_level_name(JC_EVENTLOG_OFF), "off");
    JC_CHECK_STR(jc_eventlog_level_name(JC_EVENTLOG_METRICS), "metrics");
    JC_CHECK_STR(jc_eventlog_level_name(JC_EVENTLOG_FULL), "full");
}

/* Bounded, UTF-8-safe content field for the full tier. */
static void test_add_text(void)
{
    cJSON *o = cJSON_CreateObject();
    const char *v;
    int i;
    char big[300];
    char utf[201];

    /* Short string: added verbatim. */
    jc_eventlog_add_text(o, "short", "hello", 100);
    v = cJSON_GetObjectItem(o, "short")->valuestring;
    JC_CHECK_STR(v, "hello");

    /* Long ASCII: truncated to <= max bytes + a marker. */
    for (i = 0; i < 260; i++) { big[i] = 'a'; }
    big[260] = '\0';
    jc_eventlog_add_text(o, "long", big, 64);
    v = cJSON_GetObjectItem(o, "long")->valuestring;
    JC_CHECK(strncmp(v, big, 64) == 0);          /* first 64 bytes kept */
    JC_CHECK(strstr(v, "...[+") != NULL);        /* truncation marker */

    /* UTF-8: cutting at an odd offset must back off a split 2-byte char. */
    for (i = 0; i < 100; i++) { utf[i * 2] = (char)0xC3; utf[i * 2 + 1] = (char)0xA9; }
    utf[200] = '\0';                              /* 100 x "é" = 200 bytes */
    jc_eventlog_add_text(o, "utf", utf, 11);     /* 11 would split the 6th é */
    v = cJSON_GetObjectItem(o, "utf")->valuestring;
    JC_CHECK(strncmp(v, utf, 10) == 0);          /* backed off to 10 (5 chars) */
    JC_CHECK(((unsigned char)v[10]) == '.');     /* marker starts right after */

    /* max==0 => unbounded. */
    jc_eventlog_add_text(o, "unb", big, 0);
    JC_CHECK_STR(cJSON_GetObjectItem(o, "unb")->valuestring, big);

    /* NULL inputs are no-ops. */
    jc_eventlog_add_text(o, "n", NULL, 10);
    JC_CHECK(cJSON_GetObjectItem(o, "n") == NULL);
    cJSON_Delete(o);
}

/* M290: every event carries the BUILD (`jichi`) next to the event SCHEMA (`v`).
 * Per event rather than in a header, because the reader filters by workspace and
 * by --since, and logs get concatenated and shared -- a header is lost by exactly
 * the operations that make the version matter. */
static void test_version_stamp(void)
{
    const char *path = jc_test_tmp("jichi_test_eventlog_ver.jsonl");
    struct jc_eventlog log;
    cJSON *o;
    int lines = 0;
    int stamped = 0;

    remove(path);
    JC_CHECK(jc_eventlog_open(&log, path, "sess-v", JC_EVENTLOG_METRICS)
             == JC_OK);
    o = jc_eventlog_begin(&log, "turn_start");
    JC_CHECK(o != NULL);
    /* Present on the in-memory object too, next to `v` and distinct from it.
     * The value check is GUARDED: an unguarded deref would segfault exactly when
     * the stamp is missing, i.e. on the run this test exists to diagnose
     * (ANECDOTES #29 -- the diagnostic that crashed on the run it was built to
     * explain). It cost a wasted red-before-green pass to relearn. */
    {
        cJSON *jv = cJSON_GetObjectItem(o, "jichi");
        JC_CHECK(jv != NULL);
        JC_CHECK(cJSON_GetObjectItem(o, "v") != NULL);
        if (jv != NULL) {
            JC_CHECK_STR(jv->valuestring, JC_VERSION);
        }
    }
    jc_eventlog_end(&log, o);
    o = jc_eventlog_begin(&log, "turn_end");
    jc_eventlog_end(&log, o);
    jc_eventlog_close(&log);

    /* EVERY written line carries it -- not just the first. */
    {
        FILE *f = fopen(path, "rb");
        char line[4096];
        JC_CHECK(f != NULL);
        while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
            cJSON *e = cJSON_Parse(line);
            lines++;
            if (e != NULL) {
                cJSON *jv = cJSON_GetObjectItem(e, "jichi");
                if (jv != NULL && jv->valuestring != NULL &&
                    strcmp(jv->valuestring, JC_VERSION) == 0) {
                    stamped++;
                }
                cJSON_Delete(e);
            }
        }
        if (f != NULL) {
            fclose(f);
        }
    }
    JC_CHECK(lines == 2);
    JC_CHECK(stamped == 2);
    remove(path);
}

/* M292: the sink knew which file it was appending to and could not be asked, so
 * the TUI had no way to analyse the log THIS session is producing -- the obvious
 * default for `/learn analyze`. */
static void test_path_accessor(void)
{
    const char *path = jc_test_tmp("jichi_test_eventlog_path.jsonl");
    struct jc_eventlog log;

    remove(path);
    /* A disabled sink reports no path. */
    jc_eventlog_disable(&log);
    JC_CHECK(jc_eventlog_path(&log) == NULL);
    JC_CHECK(jc_eventlog_path(NULL) == NULL);

    JC_CHECK(jc_eventlog_open(&log, path, "sid-p", JC_EVENTLOG_METRICS)
             == JC_OK);
    {
        const char *p = jc_eventlog_path(&log);
        JC_CHECK(p != NULL);
        if (p != NULL) {
            JC_CHECK(strcmp(p, path) == 0);
        }
    }
    jc_eventlog_close(&log);
    /* After close the sink is disabled again, so it reports no path -- a stale
     * path would point at a file nothing is writing. */
    JC_CHECK(jc_eventlog_path(&log) == NULL);
    /* And the field itself is cleared, not merely masked by the accessor's
     * f == NULL guard. Asserted directly because the guard makes a stale path
     * unreachable through the public API -- so without this the reset would be
     * defensive code with no test behind it, which the red-before-green pass
     * duly reported as green. */
    JC_CHECK(log.path[0] == '\0');
    remove(path);
}

/* Opening a sink inside a directory that ALREADY EXISTS must not change that
 * directory's mode (M488).
 *
 * THE DEFECT. make_parent_dir() called jc_make_private() on the log path's
 * parent unconditionally -- whether or not jichi had created it -- and the only
 * guard was for "/" and the no-slash case. Run as root, which is every container
 * and most CI, `--log /tmp/jichi.jsonl` therefore turned /tmp into 0700
 * root-only for the whole machine (measured: 1777 before, 700 after). Non-root
 * was inert only BY ACCIDENT: the chmod fails with EPERM on a directory the user
 * does not own, and the return was discarded.
 *
 * That accident is why this test does not need root. A directory the test user
 * OWNS is chmod-able by them, so the same call reproduces the same defect at any
 * privilege level -- and it is also the realistic case for a user who points
 * --log at a shared project directory of their own.
 *
 * The file itself is still 0600 and the directories jichi CREATES are still
 * 0700; M132's guarantee is unchanged for everything jichi owns. What changed is
 * that a directory it did not create is not its to re-permission. */
static void test_parent_dir_not_retightened(void)
{
    struct jc_eventlog log;
    const char *dir = jc_test_tmp("jichi_test_eventlog_pre");
    const char *path = jc_test_tmp("jichi_test_eventlog_pre/events.jsonl");
    struct stat st;
    mode_t before;

    remove(path);
    (void)rmdir(dir);
    JC_CHECK(mkdir(dir, 0755) == 0);
    JC_CHECK(chmod(dir, 0755) == 0);
    JC_CHECK(stat(dir, &st) == 0);
    before = (mode_t)(st.st_mode & 0777);
    JC_CHECK(before == (mode_t)0755);

    JC_CHECK(jc_eventlog_open(&log, path, "sid-pre", JC_EVENTLOG_METRICS)
             == JC_OK);
    jc_eventlog_close(&log);

    JC_CHECK(stat(dir, &st) == 0);
    /* The pre-existing directory keeps the mode its owner chose. */
    JC_CHECK((mode_t)(st.st_mode & 0777) == before);
    /* ...while the log file itself is still private. */
    JC_CHECK(stat(path, &st) == 0);
    JC_CHECK((mode_t)(st.st_mode & 0777) == (mode_t)0600);

    remove(path);
    (void)rmdir(dir);
}

/* The other half: a directory jichi DOES create is still made private (M132). */
static void test_created_dir_is_private(void)
{
    struct jc_eventlog log;
    const char *dir = jc_test_tmp("jichi_test_eventlog_new");
    const char *path = jc_test_tmp("jichi_test_eventlog_new/events.jsonl");
    struct stat st;

    remove(path);
    (void)rmdir(dir);
    JC_CHECK(jc_eventlog_open(&log, path, "sid-new", JC_EVENTLOG_METRICS)
             == JC_OK);
    jc_eventlog_close(&log);
    JC_CHECK(stat(dir, &st) == 0);
    JC_CHECK((mode_t)(st.st_mode & 0777) == (mode_t)0700);

    remove(path);
    (void)rmdir(dir);
}

/* M860 (N26): a child of batch `run` is stamped "<run>-t<N>" and banded by the band
 * its parent numbered -- so the first task of a session's second batch is not the
 * first task of its first. A band too large for a `long` is clamped, and band < 1
 * falls back to the task index (jc_eventlog_set_task's banding). */
static void test_task_run_per_batch(void)
{
    struct jc_eventlog log;
    const char *path = jc_test_tmp("jichi_test_eventlog_batch.jsonl");
    cJSON *o;

    remove(path);
    JC_CHECK(jc_eventlog_open(&log, path, "sess-9", JC_EVENTLOG_METRICS) == JC_OK);
    jc_eventlog_set_task_run(&log, "sess-9-b2", 1, 3);
    JC_CHECK_STR(log.task_run, "sess-9-b2-t1");
    o = jc_eventlog_begin(&log, "model_call");
    if (JC_REQUIRE(o != NULL)) {
        JC_CHECK((long)cJSON_GetObjectItem(o, "seq")->valuedouble ==
                 3L * JC_EVENTLOG_CHILD_SEQ_STRIDE);
    }
    jc_eventlog_end(&log, o);
    /* no run: the session id, as jc_eventlog_set_task does */
    jc_eventlog_set_task_run(&log, NULL, 2, 0);
    JC_CHECK_STR(log.task_run, "sess-9-t2");
    JC_CHECK(log.seq == 2L * JC_EVENTLOG_CHILD_SEQ_STRIDE);
    /* a band past the type's range is clamped, never wrapped negative */
    jc_eventlog_set_task_run(&log, "r", 1, LONG_MAX);
    JC_CHECK(log.seq > 0);
    /* task <= 0 clears the marking */
    jc_eventlog_set_task_run(&log, "r", 0, 5);
    JC_CHECK(log.task_run[0] == '\0');
    jc_eventlog_close(&log);
    remove(path);
}

/* M1137 (C-19): the full-tier add_text redaction must fail CLOSED. Open a
 * full-tier log, register a real secret, and write a row whose value holds it.
 * With JC_FAULT_REDACT armed (stands in for the scrub's malloc failing) the
 * helper returns NULL and the site DROPS the key -- so the written row must not
 * contain the key at all (RED pre-fix: the site did its own malloc, ignored the
 * probe, and wrote the value unredacted, so the key was present with the canary
 * in it). Control leg with the probe cleared: the key is present but the value
 * is redacted ("***" in, canary out). FAULT=1 only (the probe is inert in a
 * release build). */
#ifdef JC_FAULT
static void test_redact_failclosed(void)
{
    struct jc_eventlog log;
    const char *path = jc_test_tmp("jichi_test_eventlog_reduct.jsonl");
    const char *sec = "EVENTLOG-CANARY-0123456789";
    const char *val = "note EVENTLOG-CANARY-0123456789 end";
    cJSON *o;
    FILE *f;
    char *content;
    long clen;

    jc_redact_register(sec);
    JC_CHECK(jc_redact_active() == 1);

    /* RED leg: probe armed -> the key is dropped, never written unredacted. */
    remove(path);
    JC_CHECK(jc_eventlog_open(&log, path, "sid-r", JC_EVENTLOG_FULL) == JC_OK);
    o = jc_eventlog_begin(&log, "turn");
    JC_REQUIRE(o != NULL);
    setenv("JICHI_FAULT_REDACT_AFTER", "0", 1);
    jc_fault_reset();
    jc_eventlog_add_text(o, "canarykey", val, 0);
    unsetenv("JICHI_FAULT_REDACT_AFTER");
    jc_fault_reset();
    jc_eventlog_end(&log, o);
    jc_eventlog_close(&log);
    f = fopen(path, "rb");
    content = NULL;
    if (f != NULL) {
        clen = 0;
        fseek(f, 0, SEEK_END);
        clen = ftell(f);
        fseek(f, 0, SEEK_SET);
        content = (char *)malloc((size_t)clen + 1);
        if (content != NULL) {
            size_t rd = fread(content, 1, (size_t)clen, f);
            content[rd] = '\0';
        }
        fclose(f);
    }
    JC_REQUIRE(f != NULL && content != NULL);
    if (content != NULL) {
        /* The key must be ABSENT (dropped), and so must the canary. */
        JC_CHECK(strstr(content, "canarykey") == NULL);
        JC_CHECK(strstr(content, sec) == NULL);
        free(content);
    }

    /* Control leg: probe cleared -> the key is present, value redacted. */
    remove(path);
    JC_CHECK(jc_eventlog_open(&log, path, "sid-c", JC_EVENTLOG_FULL) == JC_OK);
    o = jc_eventlog_begin(&log, "turn");
    JC_REQUIRE(o != NULL);
    jc_eventlog_add_text(o, "canarykey", val, 0);
    jc_eventlog_end(&log, o);
    jc_eventlog_close(&log);
    f = fopen(path, "rb");
    content = NULL;
    if (f != NULL) {
        clen = 0;
        fseek(f, 0, SEEK_END);
        clen = ftell(f);
        fseek(f, 0, SEEK_SET);
        content = (char *)malloc((size_t)clen + 1);
        if (content != NULL) {
            size_t rd = fread(content, 1, (size_t)clen, f);
            content[rd] = '\0';
        }
        fclose(f);
    }
    JC_REQUIRE(f != NULL && content != NULL);
    if (content != NULL) {
        /* The key IS present now, but the value is redacted. */
        JC_CHECK(strstr(content, "canarykey") != NULL);
        JC_CHECK(strstr(content, sec) == NULL);
        JC_CHECK(strstr(content, "***") != NULL);
        free(content);
    }

    jc_redact_reset();
    remove(path);
}
#endif

void test_eventlog(void)
{
    test_version_stamp();
    test_path_accessor();
    test_level_parse();
    test_add_text();
    test_disabled();
    test_begin_fields_and_tier();
    test_full_tier();
    test_parent_dir_not_retightened();
    test_created_dir_is_private();
    test_task_run_per_batch();   /* M860 */
#ifdef JC_FAULT
    test_redact_failclosed();    /* M1137 (C-19) */
#endif
}
