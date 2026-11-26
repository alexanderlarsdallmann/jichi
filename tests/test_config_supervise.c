/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_config_supervise.c - def-377 W3.3/W4.4 (M1194): the `parallelSupervise`
 * config object, read lenient (a person writes it) and named when wrong.
 *
 * One object, four members, all in SECONDS (the note's unit decision, run 0059):
 *   {"noEditSec":3600,"placeholders":2,"action":"report","graceSec":60}
 *   noEditSec    R1's threshold: no edit for this many seconds AFTER the first
 *                edit (the 60-min measurement: 7 of 563 -> 7 of 158). Default 3600.
 *   placeholders R2's threshold: total copied placeholders that flags.
 *                The 60-min measurement: 4 of 880 -> 3 of 245. Default 2.
 *   action       report (default) / wrapup / stop. An unknown value is a NAMED
 *                config error (W4.4) and the batch runs as report -- not silent.
 *   graceSec     the fence that bounds a soft stop's fallback to the hard stop
 *                (W4.2/W4.3; A4 consumes it). Default 60.
 *
 * W3.4: it must NOT reuse parallelTaskTimeout's name or path -- a timeout-stopped
 * task is kept and never merged (def-300); a flagged task under `report` is
 * neither. So these are their own members beside parallel_task_timeout.
 *
 * RED-FIRST on the head: the object's PARSE is absent. The struct fields exist
 * (compile scaffolding) and are defaulted, so the absent-object case reads the
 * documented defaults (GREEN -- report is the old behaviour's default); but the
 * LENIENT case (parse not run -> the values stay at the defaults, not 90/3/wrapup/
 * 10) and the NAMED-ERROR case (no `explode` warning is printed, so the captured
 * stderr is empty) are RED. The flag rule itself (R1/R2) is the driver's red
 * (wave C cells 2-8) and is not unit-tested here.
 */
#include "jc_test.h"
#include "jc_config.h"
#include "jc_log.h"
#include "jc_mem.h"
#include "jc_fcntl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void test_config_supervise(void);   /* def-377 W3/W4: file-local prototype */

/* Capture what jc_logf writes to stderr for one parse: redirect fd 2 to a
 * temp file, run `parse` (0..2), and return a malloc'd copy of the bytes
 * written. test_redact.c's idiom (dup/dup2), read back under one bounded
 * buffer. */
static char *capture_stderr(void (*parse)(void *), void *arg)
{
    const char *path = jc_test_tmp("jichi_sup_stderr.txt");
    char *out;
    size_t n = 0, cap = 4096;
    int fd, saved;
    int prev_level;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | JC_O_BINARY, 0600);
    if (fd < 0) {
        return NULL;
    }
    out = (char *)malloc(cap);
    if (out == NULL) {
        close(fd);
        return NULL;
    }
    prev_level = jc_log_get_level();
    jc_log_set_level(JC_LOG_WARN);   /* the named error is a WARN */
    saved = dup(2);
    if (saved < 0) {
        jc_log_set_level(prev_level);
        close(fd);
        free(out);
        return NULL;
    }
    dup2(fd, 2);
    if (parse != NULL) {
        parse(arg);
    }
    dup2(saved, 2);
    close(saved);
    close(fd);
    jc_log_set_level(prev_level);

    /* Read the captured bytes back. */
    fd = open(path, O_RDONLY | JC_O_BINARY);
    if (fd >= 0) {
        while (1) {
            ssize_t r;
            if (n + 1024 + 1 > cap) {
                cap *= 2;
                out = (char *)realloc(out, cap);
                if (out == NULL) {
                    break;
                }
            }
            r = read(fd, out + n, cap - n - 1);
            if (r <= 0) {
                break;
            }
            n += (size_t)r;
        }
        close(fd);
    }
    out[n] = '\0';
    jc_test_rm_rf(path);
    return out;
}

/* A load wrapper so capture_stderr's void(*)(void) signature can be used. */
struct sup_load {
    const char *json;
    struct jc_arena *a;
};
static void sup_load_run(void *arg)
{
    struct sup_load *l = (struct sup_load *)arg;
    struct jc_config c;
    jc_config_load_json(l->json, 0, &c, l->a);
    jc_config_free(&c);
}

/* W3.3 absent -> the documented defaults (SECONDS): 3600 / 2 / report / 60.
 * GREEN on the red commit too (the fields are defaulted; report is the old
 * behaviour's default). */
static void test_supervise_defaults(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config c;
    jc_config_load_json("{\"models\":[{\"name\":\"a\"}]}", 0, &c, a);
    JC_CHECK(c.parallel_sup_no_edit_sec == 3600);
    JC_CHECK(c.parallel_sup_placeholders == 2);
    JC_CHECK(c.parallel_sup_action == JC_PAR_SUP_REPORT);
    JC_CHECK(c.parallel_sup_grace_sec == 60);
    jc_config_free(&c);
    jc_arena_free(a);
}

/* W3.3 present, LENIENT: a number as a string is read; prose falls to default.
 * "90" (a string) is 90 s; 3 (a number) is 3; "wrapup" is wrapup; 10 (a number)
 * is 10. RED on the red commit: the parse is absent, so the values stay at the
 * defaults (3600/2/report/60), not 90/3/wrapup/10. */
static void test_supervise_lenient(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config c;
    jc_config_load_json(
        "{\"models\":[{\"name\":\"a\"}],"
        "\"parallelSupervise\":{\"noEditSec\":\"90\",\"placeholders\":3,"
        "\"action\":\"wrapup\",\"graceSec\":10}}", 0, &c, a);
    JC_CHECK(c.parallel_sup_no_edit_sec == 90);
    JC_CHECK(c.parallel_sup_placeholders == 3);
    JC_CHECK(c.parallel_sup_action == JC_PAR_SUP_WRAPUP);
    JC_CHECK(c.parallel_sup_grace_sec == 10);
    jc_config_free(&c);
    jc_arena_free(a);
}

/* W4.4: an unknown action is a NAMED config error (it names the value and the
 * fallback) and the action stays report -- not a silent default. The name goes
 * through jc_logf(WARN), captured here. RED on the red commit: no warning is
 * printed, so the captured stderr is empty (err == NULL). */
static void test_supervise_unknown_action(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config c;
    struct sup_load l;
    char *err;
    const char *json =
        "{\"models\":[{\"name\":\"a\"}],"
        "\"parallelSupervise\":{\"action\":\"explode\"}}";

    jc_config_load_json(json, 0, &c, a);
    JC_CHECK(c.parallel_sup_action == JC_PAR_SUP_REPORT);   /* stays report */
    jc_config_free(&c);

    l.json = json;
    l.a = a;
    err = capture_stderr(sup_load_run, &l);
    JC_CHECK(err != NULL && err[0] != '\0');
    if (err != NULL) {
        /* The error NAMES the value and the knob, and says it fell to report. */
        JC_CHECK(strstr(err, "explode") != NULL);
        JC_CHECK(strstr(err, "parallelSupervise") != NULL);
        JC_CHECK(strstr(err, "report") != NULL);
        free(err);
    }
    jc_arena_free(a);
}

void test_config_supervise(void)
{
    test_supervise_defaults();
    test_supervise_lenient();
    test_supervise_unknown_action();
}
