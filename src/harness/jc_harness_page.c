/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_page.c - the page / page-watch / probe verbs (M1213, H12;
 * batch H12, drive of 2026-10-27 hardening-host drive's page-watch.sh,
 * 25 cells; SV:0433's option (a), page stretch S1).
 *
 * The implementation is the C89 port of the reference:
 *   docs/internal/drive/2026-10-27-hardening-host/page-watch.sh (page-watch.sh)
 *   docs/internal/drive/2026-10-27-hardening-host/park-resume.sh (ready()/mprobe())
 *
 * The three verb bodies per b-row in notes/briefs-2026-11-23-M1213-T2b.md.
 *
 * Key idioms copied from jc_harness_repin.c:
 *   - the say() line with %F %T %z prefix (b23)
 *   - key-by-env child (JICHI_API_KEY, b20)
 *   - jc_proc_capture for sh -c children
 *   - the usage refusal rc 2
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>   /* pid_t */
#include <sys/wait.h>    /* WIFEXITED / WEXITSTATUS */

#include "jc_platform.h"  /* jc_status, jc_size, jc_mkdir_p, jc_is_dir */
#include "jc_mem.h"       /* struct jc_arena, jc_arena_strdup */
#include "jc_str.h"       /* struct jc_sb */
#include "jc_vec.h"       /* struct jc_vec */
#include "jc_snprintf.h"  /* jc_snprintf, never sprintf */
#include "jc_proc.h"      /* jc_proc_capture, jc_proc_secret_env_add, jc_proc_scrub_secret_env */
#include "jc_harness_config.h"
#include "jc_harness_page.h"
#include "jc_hp_pagestretch.h"

/* Paths hang off the config's dir; give the joins room. */
#define PAGE_PATH_CAP 4352
/* The stretch line is pure ASCII: `open last_ok first lastf n cls run` */
#define PAGE_STRETCH_CAP 512
/* The key file is small. */
#define PAGE_KEY_CAP 8192
/* The model key's 12-char prefix, kept in memory for redaction. */
#define PAGE_KEY_PREFIX 12
/* The doctor output's --live line is cut to 120. */
#define PAGE_DOCTOR_LINE_CAP 121
/* The captured command output cap (the hook). */
#define PAGE_CMD_CAP 8192

/* The default values (b11, page-watch.sh header). */
#define PAGE_AFTER_DEFAULT 1800
#define PAGE_FRESH_DEFAULT 600
#define PAGE_INTERVAL_DEFAULT 60

/* The say() stamp's UTC-of-wall (the repin's rr_utc_from_tm). */
static time_t
page_utc_from_tm(const struct tm *tmv)
{
    long y = tmv->tm_year + 1900;
    long m = tmv->tm_mon + 1;
    long d = tmv->tm_mday;
    long era, yoe, doy, doe, days;
    y -= (m <= 2) ? 1 : 0;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + doe - 719468;
    return (time_t)(days * 86400L + tmv->tm_hour * 3600L
                    + tmv->tm_min * 60L + tmv->tm_sec);
}

/* A C89-clean `date '+%F %T %z'` (b23's say() stamp). Fills out (>= 24) with
 * the stamp; returns 1 on success, 0 when the clock is unusable. */
static int
page_stamp(char *out, size_t cap)
{
    struct tm tmv;
    struct tm *tmr;
    time_t t;
    int n;

    if (cap < 24) {
        return 0;
    }
    t = time(NULL);
    tmr = localtime_r(&t, &tmv);
    if (tmr == NULL) {
        return 0;
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", tmr);
    if (n == 0) {
        return 0;
    }
    /* the zone offset: +HHMM / -HHMM / +0000 (the %z form). */
    {
        int off_min;
        int sign;
        int off_h;
        int off_m;
        off_min = (int)((long)page_utc_from_tm(tmr) - (long)t) / 60;
        sign = (off_min < 0) ? 1 : 0;
        if (off_min < 0) {
            off_min = -off_min;
        }
        off_h = off_min / 60;
        off_m = off_min % 60;
        n += jc_snprintf(out + n, cap - (size_t)n, " %c%02d%02d",
                         sign ? '-' : '+', off_h, off_m);
    }
    return 1;
}

/* b23 say(): one line to stdout AND appended to the out file, prefixed with
 * the drive's local stamp `date '+%F %T %z'`. The out file GROWS. */
static void
page_say(const char *out, const char *msg)
{
    char stamp[64];
    char line[PAGE_PATH_CAP];

    printf("%s\n", msg);
    fflush(stdout);
    if (out == NULL || out[0] == '\0') {
        return;
    }
    if (page_stamp(stamp, sizeof stamp)) {
        FILE *f = fopen(out, "ab");
        if (f != NULL) {
            if (jc_snprintf(line, sizeof line, "%s %s\n", stamp, msg) >= 0) {
                fputs(line, f);
                fflush(f);
            }
            fclose(f);
        }
    }
}

/* Run one shimmable command through the harness's capture idiom (jc_spawn's
 * sh -c + the built-in secret-name scrub). `cmdline` is the full command
 * line; the child's combined output is captured (capped at cap_out) and its
 * rc returned. 1 on success, 0 on a spawn fault, -2 on the deadline kill. */
static int
page_run_cmd(const char *cmdline, const char *env_var, const char *env_val,
             char *out_text, size_t cap_out, int *out_rc)
{
    char *argv[4];
    struct jc_vec envv;
    struct jc_sb capture;
    char ke[14 + PAGE_KEY_CAP + 1];
    char *kvp = ke;
    int rc;
    size_t len;

    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = (char *)cmdline;
    argv[3] = NULL;
    jc_vec_init(&envv, sizeof(char *));
    if (env_var != NULL && env_var[0] != '\0' && env_val != NULL) {
        if (14 + strlen(env_val) + 1 > sizeof ke) {
            jc_vec_free(&envv);
            return 0;
        }
        jc_snprintf(ke, sizeof ke, "%s=%s", env_var, env_val);
        if (jc_vec_push(&envv, &kvp) != JC_OK) {
            jc_vec_free(&envv);
            return 0;
        }
    }
    jc_sb_init(&capture);
    rc = (int)jc_proc_capture(argv, &envv, NULL, &capture, (jc_size)cap_out,
                              600L, NULL);
    jc_vec_free(&envv);
    if (rc == -1 || rc == -2) {
        jc_sb_free(&capture);
        return (rc == -2) ? -2 : 0;
    }
    len = capture.len;
    while (len > 0 && (capture.data[len - 1] == '\n' ||
                       capture.data[len - 1] == '\r')) {
        len--;
    }
    if (len > cap_out - 1) {
        len = cap_out - 1;
    }
    if (capture.data != NULL && len > 0) {
        memcpy(out_text, capture.data, len);
    }
    out_text[len] = '\0';
    jc_sb_free(&capture);
    *out_rc = rc;
    return 1;
}

/* Strip trailing newline(s) from a captured reading. */
static void
page_strip_nl(char *s)
{
    size_t n;
    if (s == NULL || s[0] == '\0') {
        return;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        n--;
    }
    s[n] = '\0';
}

/* Read a file fully, trim trailing newlines, return 1 on success. */
static int
page_read_file(const char *path, char *out, size_t cap, int *missing_ok)
{
    FILE *f;
    size_t n = 0;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (missing_ok != NULL) {
        *missing_ok = 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        if (missing_ok != NULL) {
            *missing_ok = 1;
        }
        return 0;
    }
    while (n + 1 < cap) {
        int c = fgetc(f);
        if (c == EOF) {
            break;
        }
        out[n++] = (char)c;
    }
    fclose(f);
    out[n] = '\0';
    page_strip_nl(out);
    return 1;
}

/* Write a string (+ trailing newline) to a file. */
static void
page_write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    if (text != NULL && text[0] != '\0') {
        fputs(text, f);
        fputc('\n', f);
    }
    fclose(f);
}

/* Trim leading/trailing whitespace in place, return start pointer. */
static char *
page_trim(char *s)
{
    char *p;
    if (s == NULL || s[0] == '\0') {
        return s;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    p = s + strlen(s) - 1;
    while (p > s && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        *p-- = '\0';
    }
    return s;
}


/* The three verbs' main functions */

/* jc_harness_page_run -- one check: the reference's PAGE_ONCE form (b12's CHANGED split).
 * Returns 0 on success, 2 on usage refusal. */
int
jc_harness_page_run(const char *config_path,
                    const char *state_path, const char *out_path,
                    long now, int after_s, int fresh_s,
                    int park,
                    void *a)
{
    struct jc_harness_config cfg;
    struct jc_arena *arena = (struct jc_arena *)a;
    char *drive_dir = NULL;
    char *state_file = NULL;
    char *out_file = NULL;
    char stretch_line[PAGE_STRETCH_CAP];
    int open_flag;
    unsigned long last_ok, first, lastf;
    int n;
    char cls[PAGE_PATH_CAP];
    char run[16];
    char state_val[64];
    char line_buf[PAGE_PATH_CAP];
    char unit_name[64];
    char unit_state[64];
    char after_str[32];
    char fresh_str[32];
    char hook_line[PAGE_PATH_CAP];
    char hook_output[PAGE_CMD_CAP];
    int hook_rc;

    /* Load the config. */
    if (jc_harness_config_load(config_path, &cfg, arena) != JC_OK) {
        fprintf(stderr, "jichi-harness: page: %s\n", cfg.message);
        return 2;
    }

    /* Extract config fields. */
    drive_dir = cfg.dir;
    /* SV:0033: no built-in unit name. The config MUST name the loop unit. */
    if (cfg.drive_loop_unit == NULL || cfg.drive_loop_unit[0] == '\0') {
        fprintf(stderr, "jichi-harness: page: the config names no drive.loopUnit; set it in the config\n");
        return 1;
    }
    strncpy(unit_name, cfg.drive_loop_unit, sizeof unit_name - 1);
    unit_name[sizeof(unit_name) - 1] = '\0';

    /* Flags with defaults. */
    if (after_s == 0) {
        after_s = PAGE_AFTER_DEFAULT;
    }
    if (fresh_s == 0) {
        fresh_s = PAGE_FRESH_DEFAULT;
    }

    /* Validate --state and --out are present. */
    if (state_path == NULL || state_path[0] == '\0') {
        fprintf(stderr, "jichi-harness: page: usage: --state <f>\n");
        return 2;
    }
    if (out_path == NULL || out_path[0] == '\0') {
        fprintf(stderr, "jichi-harness: page: usage: --out <f>\n");
        return 2;
    }

    state_file = (char *)state_path;
    out_file = (char *)out_path;

    /* Validate numeric flags. */
    if (after_s < 0) {
        fprintf(stderr, "jichi-harness: page: usage: --after-s <non-negative-integer>\n");
        return 2;
    }
    if (fresh_s < 0) {
        fprintf(stderr, "jichi-harness: page: usage: --fresh-s <non-negative-integer>\n");
        return 2;
    }

    /* Get the unit state (for the armed line). */
    if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
        char stdout_buf[PAGE_PATH_CAP];
        int cmd_rc;
        if (page_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                         stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            page_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    } else {
        /* Default: systemctl --user is-active <U> */
        char cmd[PAGE_PATH_CAP];
        char stdout_buf[PAGE_PATH_CAP];
        int cmd_rc;
        if (jc_snprintf(cmd, sizeof cmd, "systemctl --user is-active %s", unit_name) >= (int)sizeof cmd) {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else if (page_run_cmd(cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            page_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    }

    /* Build the armed line. */
    if (after_s == PAGE_AFTER_DEFAULT) {
        strncpy(after_str, "1800", sizeof after_str - 1);
        after_str[sizeof(after_str) - 1] = '\0';
    } else {
        jc_snprintf(after_str, sizeof after_str, "%d", after_s);
    }
    if (fresh_s == PAGE_FRESH_DEFAULT) {
        strncpy(fresh_str, "600", sizeof fresh_str - 1);
        fresh_str[sizeof(fresh_str) - 1] = '\0';
    } else {
        jc_snprintf(fresh_str, sizeof fresh_str, "%d", fresh_s);
    }
    if (jc_snprintf(line_buf, sizeof line_buf,
                    "armed: page after %ss of continuous failure (latest failure within %ss); loop unit %s %s",
                    after_str, fresh_str, unit_name, unit_state) >= (int)sizeof line_buf) {
        return 2;
    }
    page_say(out_file, line_buf);

    /* Stretch: jc_pagestretch_line(drive_dir, arena, stretch_line, sizeof stretch_line). */
    if (jc_pagestretch_line(drive_dir, arena, stretch_line, sizeof stretch_line) != 0) {
        fprintf(stderr, "jichi-harness: page: stretch failed\n");
        return 2;
    }

    /* Parse the stretch line: `open last_ok first lastf n cls run` */
    open_flag = 0;
    last_ok = 0;
    first = 0;
    lastf = 0;
    n = 0;
    cls[0] = '\0';
    run[0] = '\0';
    if (sscanf(stretch_line, "%d %lu %lu %lu %d %511s %15s",
               &open_flag, &last_ok, &first, &lastf, &n, cls, run) != 7) {
        /* Parse failed - treat as closed */
        open_flag = 0;
    }

    /* The judging time: --now when the caller gave one (> 0), else the clock.
     * Until the 0.15.0 release audit (2026-11-25) a missing --now left 0 here,
     * so `now - first` was negative, the AFTER test never passed and the verb
     * never paged unattended; every driver cell passed --now. */
    if (now <= 0) {
        now = (long)time(NULL);
    }

    /* Condition: open==1 AND now-first >= AFTER AND now-lastf <= FRESH */
    if (open_flag != 1 ||
        (now - (long)first) < after_s ||
        (now - (long)lastf) > fresh_s) {
        /* No page */
        return 0;
    }

    /* Once per stretch: read the state file. A missing file reads empty,
     * which differs from every last_ok, so the stretch's first page fires.
     * Page only when the contents differ from THIS stretch's last_ok -- "0"
     * included: a stretch with no successful call at all has last_ok 0, its
     * first page writes "0", and that must hold it (page-watch.sh:47 compares
     * the two strings and nothing else). Until 2026-11-25 a state of "0" skipped
     * the comparison, and such a stretch paged on every run (the independent
     * review of d56465e2; harness_page.sh cells 39 and 40). */
    page_read_file(state_file, state_val, sizeof state_val, NULL);
    if (state_val[0] != '\0') {
        /* State file has a value - check if it matches last_ok */
        char last_ok_str[32];
        jc_snprintf(last_ok_str, sizeof last_ok_str, "%lu", last_ok);
        if (strcmp(state_val, last_ok_str) == 0) {
            /* Same stretch - don't page again */
            return 0;
        }
    }

    /* PAGE line (b9). */
    {
        long minutes = (now - (long)first) / 60;
        char hhmm[16];
        struct tm *tmr;
        time_t t_first = (time_t)first;

        tmr = localtime(&t_first);
        if (tmr != NULL) {
            strftime(hhmm, sizeof hhmm, "%H:%M", tmr);
        } else {
            strncpy(hhmm, "00:00", sizeof hhmm - 1);
            hhmm[sizeof(hhmm) - 1] = '\0';
        }
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "PAGE: model calls failing for %ld min since %s (run %s, %d failed: %s); parking the drive",
                        minutes, hhmm, run, n, cls) >= (int)sizeof line_buf) {
            return 2;
        }
    }

    /* say() it to BOTH stdout and the --out file (append). */
    page_say(out_file, line_buf);

    /* Hook (b10): cfg->host_page_command. */
    if (cfg.host_page_command != NULL && cfg.host_page_command[0] != '\0') {
        if (jc_snprintf(hook_line, sizeof hook_line, "%s", cfg.host_page_command) >= (int)sizeof hook_line) {
            return 2;
        }
        if (page_run_cmd(hook_line, NULL, NULL, hook_output, sizeof hook_output, &hook_rc) != 1) {
            /* A hook failure is LOGGED (its combined output, one line prefixed `pageCommand: `) */
            fprintf(stderr, "pageCommand: hook failed\n");
        } else if (hook_rc != 0) {
            /* Hook ran successfully but returned non-zero - log the failure */
            fprintf(stderr, "pageCommand: hook failed\n");
        }
    }

    /* State write: printf '%s\n' <last_ok> to the --state file. */
    {
        char last_ok_str[32];
        jc_snprintf(last_ok_str, sizeof last_ok_str, "%lu", last_ok);
        page_write_file(state_file, last_ok_str);
    }

    /* --park half: run or print the park commands. */
    if (park) {
        /* WITH --park: create STOP at absolute path and print the two audit lines.
         * Per the resolved design, this does NOT run the park verb synchronously
         * (park blocks and there is no systemd in the container). */
        char stop_path[PAGE_PATH_CAP];
        char park_cmd[PAGE_PATH_CAP];
        char line_buf[PAGE_PATH_CAP];
        int n;

        /* First: compute the absolute stop path. */
        n = jc_snprintf(stop_path, sizeof stop_path, "%s/STOP", drive_dir);
        if (n < 0 || n >= (int)sizeof stop_path) {
            return 2;
        }

        /* Second: create the STOP file with the already-present else-pair.
         * If it exists, print "STOP already present"; else create and print "STOP created:..." */
        if (access(stop_path, F_OK) == 0) {
            /* STOP already present - do not re-create. */
            page_say(out_file, "STOP already present");
        } else {
            /* Create STOP file. */
            FILE *f = fopen(stop_path, "wb");
            if (f == NULL) {
                page_say(out_file, "park step failed: stop creation");
                return 1;
            }
            fclose(f);
            page_say(out_file, "STOP created: the loop ends after this run");
        }

        /* Third: print the two audit lines in reference order:
         * (1) park verb line, (2) STOP line. */
        n = jc_snprintf(park_cmd, sizeof park_cmd,
                        "jichi-harness --config \"%s\" park --interval 60", config_path);
        if (n < 0 || n >= (int)sizeof park_cmd) {
            return 2;
        }
        n = jc_snprintf(line_buf, sizeof line_buf, "touch \"%s\"", stop_path);
        if (n < 0 || n >= (int)sizeof line_buf) {
            return 2;
        }
        page_say(out_file, park_cmd);
        page_say(out_file, line_buf);

        return 0;
    } else {
        /* WITHOUT --park: print the two commands it would run, with absolute stop_path.
         * Create NOTHING, print the park line FIRST, then the STOP line. */
        char stop_path[PAGE_PATH_CAP];
        char park_cmd[PAGE_PATH_CAP];
        char touch_line[PAGE_PATH_CAP];
        int n;

        n = jc_snprintf(stop_path, sizeof stop_path, "%s/STOP", drive_dir);
        if (n < 0 || n >= (int)sizeof stop_path) {
            return 2;
        }
        n = jc_snprintf(park_cmd, sizeof park_cmd,
                        "jichi-harness --config \"%s\" park --interval 60", config_path);
        if (n < 0 || n >= (int)sizeof park_cmd) {
            return 2;
        }
        n = jc_snprintf(touch_line, sizeof touch_line, "would: touch \"%s\"", stop_path);
        if (n < 0 || n >= (int)sizeof touch_line) {
            return 2;
        }
        /* Print in reference order: park line FIRST, then the touch line. */
        page_say(out_file, park_cmd);
        page_say(out_file, touch_line);
        return 0;
    }
}

/* jc_harness_page_watch_run -- the loop (b13). */
int
jc_harness_page_watch_run(const char *config_path,
                          const char *state_path, const char *out_path,
                          long now, int after_s, int fresh_s,
                          int interval,
                          void *a)
{
    /* This is the loop version of page_run. For the driver, it runs the loop
     * with a stand-in unitState that answers active once then inactive.
     * In a real environment, this would be a background loop. */
    struct jc_harness_config cfg;
    struct jc_arena *arena = (struct jc_arena *)a;
    char *drive_dir = NULL;
    char *state_file = NULL;
    char *out_file = NULL;
    char unit_name[64];
    char unit_state[64];
    char after_str[32];
    char fresh_str[32];
    char line_buf[PAGE_PATH_CAP];  /* For the driver: active exactly once */

    /* Load the config. */
    if (jc_harness_config_load(config_path, &cfg, arena) != JC_OK) {
        fprintf(stderr, "jichi-harness: page-watch: %s\n", cfg.message);
        return 2;
    }

    /* Extract config fields. */
    drive_dir = cfg.dir;

    /* SV:0033: no built-in unit name. The config MUST name the loop unit. */
    if (cfg.drive_loop_unit == NULL || cfg.drive_loop_unit[0] == '\0') {
        fprintf(stderr, "jichi-harness: page-watch: the config names no drive.loopUnit; set it in the config\n");
        return 1;
    }
    strncpy(unit_name, cfg.drive_loop_unit, sizeof unit_name - 1);
    unit_name[sizeof(unit_name) - 1] = '\0';

    /* Flags with defaults. */
    if (after_s == 0) {
        after_s = PAGE_AFTER_DEFAULT;
    }
    if (fresh_s == 0) {
        fresh_s = PAGE_FRESH_DEFAULT;
    }
    if (interval == 0) {
        interval = PAGE_INTERVAL_DEFAULT;
    }

    /* Validate --state and --out are present. */
    if (state_path == NULL || state_path[0] == '\0') {
        fprintf(stderr, "jichi-harness: page-watch: usage: --state <f>\n");
        return 2;
    }
    if (out_path == NULL || out_path[0] == '\0') {
        fprintf(stderr, "jichi-harness: page-watch: usage: --out <f>\n");
        return 2;
    }

    state_file = (char *)state_path;
    out_file = (char *)out_path;

    /* Validate numeric flags. */
    if (after_s < 0) {
        fprintf(stderr, "jichi-harness: page-watch: usage: --after-s <non-negative-integer>\n");
        return 2;
    }
    if (fresh_s < 0) {
        fprintf(stderr, "jichi-harness: page-watch: usage: --fresh-s <non-negative-integer>\n");
        return 2;
    }
    if (interval < 0) {
        fprintf(stderr, "jichi-harness: page-watch: usage: --interval <non-negative-integer>\n");
        return 2;
    }

    /* Get the unit state for the armed line. */
    if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
        char stdout_buf[PAGE_PATH_CAP];
        int cmd_rc;
        if (page_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                         stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            page_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    } else {
        /* Default: systemctl --user is-active <U> */
        char cmd[PAGE_PATH_CAP];
        char stdout_buf[PAGE_PATH_CAP];
        int cmd_rc;
        if (jc_snprintf(cmd, sizeof cmd, "systemctl --user is-active %s", unit_name) >= (int)sizeof cmd) {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else if (page_run_cmd(cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            page_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    }

    /* Build the armed line. */
    if (after_s == PAGE_AFTER_DEFAULT) {
        strncpy(after_str, "1800", sizeof after_str - 1);
        after_str[sizeof(after_str) - 1] = '\0';
    } else {
        jc_snprintf(after_str, sizeof after_str, "%d", after_s);
    }
    if (fresh_s == PAGE_FRESH_DEFAULT) {
        strncpy(fresh_str, "600", sizeof fresh_str - 1);
        fresh_str[sizeof(fresh_str) - 1] = '\0';
    } else {
        jc_snprintf(fresh_str, sizeof fresh_str, "%d", fresh_s);
    }
    if (jc_snprintf(line_buf, sizeof line_buf,
                    "armed: page after %ss of continuous failure (latest failure within %ss); loop unit %s %s",
                    after_str, fresh_str, unit_name, unit_state) >= (int)sizeof line_buf) {
        return 2;
    }
    page_say(out_file, line_buf);

    /* Loop forever (no cap). The driver's stand-in answers active once. */
    while (1) {
        char guard_cmd[PAGE_PATH_CAP];
        char guard_stdout[16];
        int guard_rc;

        /* Guard: cfg->host_page_unit_state's trimmed stdout == `active`. */
        if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
            if (page_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                             guard_stdout, sizeof guard_stdout, &guard_rc) == 1) {
                page_strip_nl(guard_stdout);
            } else {
                strncpy(guard_stdout, "unknown", sizeof guard_stdout - 1);
                guard_stdout[sizeof(guard_stdout) - 1] = '\0';
            }
        } else {
            /* Default: systemctl --user is-active <U> */
            if (jc_snprintf(guard_cmd, sizeof guard_cmd, "systemctl --user is-active %s", unit_name) >= (int)sizeof guard_cmd) {
                strncpy(guard_stdout, "unknown", sizeof guard_stdout - 1);
                guard_stdout[sizeof(guard_stdout) - 1] = '\0';
            } else if (page_run_cmd(guard_cmd, NULL, NULL, guard_stdout, sizeof guard_stdout, &guard_rc) == 1) {
                page_strip_nl(guard_stdout);
            } else {
                strncpy(guard_stdout, "unknown", sizeof guard_stdout - 1);
                guard_stdout[sizeof(guard_stdout) - 1] = '\0';
            }
        }

        /* The guard reads the WORD, not the rc. */
        if (strcmp(guard_stdout, "active") == 0) {
            /* Active - run the page check. */
            char stretch_line[PAGE_STRETCH_CAP];
            int open_flag;
            unsigned long last_ok, first, lastf;
            int n;
            char cls[PAGE_PATH_CAP];
            char run[16];
            char state_val[64];
            char page_line[PAGE_PATH_CAP];
            char last_ok_str[32];
            /* This pass's judging time: --now when given (> 0), else the clock,
             * read on EVERY pass -- the loop runs for days (the 0.15.0 release
             * audit: a missing --now left 0 and the loop never paged). */
            long tnow = (now > 0) ? now : (long)time(NULL);

            /* Stretch. */
            if (jc_pagestretch_line(drive_dir, arena, stretch_line, sizeof stretch_line) != 0) {
                /* Continue on stretch failure */
                goto sleep_loop;
            }

            /* Parse the stretch line. */
            open_flag = 0;
            last_ok = 0;
            first = 0;
            lastf = 0;
            n = 0;
            cls[0] = '\0';
            run[0] = '\0';
            if (sscanf(stretch_line, "%d %lu %lu %lu %d %511s %15s",
                       &open_flag, &last_ok, &first, &lastf, &n, cls, run) != 7) {
                open_flag = 0;
            }

            /* Condition: open==1 AND now-first >= AFTER AND now-lastf <= FRESH */
            if (open_flag != 1 ||
                (tnow - (long)first) < after_s ||
                (tnow - (long)lastf) > fresh_s) {
                goto sleep_loop;
            }

            /* Once per stretch: read the state file (a missing file reads
             * empty and pages). Compared with last_ok whatever it holds, "0"
             * included -- the same rule as the page verb above. */
            page_read_file(state_file, state_val, sizeof state_val, NULL);
            if (state_val[0] != '\0') {
                char ok_str[32];
                jc_snprintf(ok_str, sizeof ok_str, "%lu", last_ok);
                if (strcmp(state_val, ok_str) == 0) {
                    goto sleep_loop;  /* Same stretch - don't re-page */
                }
            }

            /* PAGE line. */
            {
                long minutes = (tnow - (long)first) / 60;
                char hhmm[16];
                struct tm *tmr;
                time_t t_first = (time_t)first;

                tmr = localtime(&t_first);
                if (tmr != NULL) {
                    strftime(hhmm, sizeof hhmm, "%H:%M", tmr);
                } else {
                    strncpy(hhmm, "00:00", sizeof hhmm - 1);
            hhmm[sizeof(hhmm) - 1] = '\0';
                }
                if (jc_snprintf(page_line, sizeof page_line,
                                "PAGE: model calls failing for %ld min since %s (run %s, %d failed: %s); parking the drive",
                                minutes, hhmm, run, n, cls) >= (int)sizeof page_line) {
                    goto sleep_loop;
                }
            }

            page_say(out_file, page_line);

            /* State write. */
            jc_snprintf(last_ok_str, sizeof last_ok_str, "%lu", last_ok);
            page_write_file(state_file, last_ok_str);
        }

    sleep_loop:
        /* Sleep --interval. */
        sleep((unsigned int)interval);
    }

    return 0;
}

/* jc_harness_probe_run -- the reference's ready()/mprobe() (b14-b19). */
int
jc_harness_probe_run(const char *config_path,
                     const char *model, int probe, int model_probe,
                     int gateway_probe,
                     void *a)
{
    struct jc_harness_config cfg;
    struct jc_arena *arena = (struct jc_arena *)a;
    char *model_name = NULL;
    char key_file[PAGE_PATH_CAP];
    char key_buf[PAGE_KEY_CAP];
    char gateway_url[PAGE_PATH_CAP];
    char model_probe_cmd[PAGE_PATH_CAP];
    char gateway_out[16];
    int gateway_rc;
    /* Full doctor output (121-byte cap applies to the printed line only, not capture). */
    char model_out[4096];
    int model_rc;
    char model_line[PAGE_DOCTOR_LINE_CAP];
    char key_prefix[PAGE_KEY_PREFIX + 1];
    size_t key_len;

    /* Load the config. */
    if (jc_harness_config_load(config_path, &cfg, arena) != JC_OK) {
        fprintf(stderr, "jichi-harness: probe: %s\n", cfg.message);
        return 2;
    }

    /* --model default: cfg->drive_lead_model else `qwen27b`. */
    if (model != NULL && model[0] != '\0') {
        model_name = (char *)model;
    } else if (cfg.drive_lead_model != NULL && cfg.drive_lead_model[0] != '\0') {
        model_name = (char *)cfg.drive_lead_model;
    } else {
        model_name = "qwen27b";
    }

    /* The model key file path. */
    if (cfg.model_key_file != NULL && cfg.model_key_file[0] != '\0') {
        strncpy(key_file, cfg.model_key_file, sizeof key_file - 1);
        key_file[sizeof(key_file) - 1] = '\0';
    } else {
        fprintf(stderr, "jichi-harness: probe: no modelKeyFile in config\n");
        return 2;
    }

    /* Gateway URL default. */
    if (cfg.host_page_gateway != NULL && cfg.host_page_gateway[0] != '\0') {
        strncpy(gateway_url, cfg.host_page_gateway, sizeof gateway_url - 1);
        gateway_url[sizeof(gateway_url) - 1] = '\0';
    } else {
        strncpy(gateway_url, "https://api.hrz.uni-giessen.de/v1/models", sizeof gateway_url - 1);
        gateway_url[sizeof(gateway_url) - 1] = '\0';
    }

    /* Exactly ONE of --probe / --model-probe / --gateway-probe is passed. */
    if ((probe ? 1 : 0) + (model_probe ? 1 : 0) + (gateway_probe ? 1 : 0) != 1) {
        fprintf(stderr, "jichi-harness: probe: usage: exactly one of --probe / --model-probe / --gateway-probe\n");
        return 2;
    }

    /* --model-probe: run ONLY the model half, skip gateway half entirely. */
    if (model_probe) {
        goto model_only;
    }

    /* Gateway half (b14): ONE child (shimmable host.pageGatewayProbe). */
    if (cfg.host_page_gateway_probe != NULL && cfg.host_page_gateway_probe[0] != '\0') {
        if (page_run_cmd(cfg.host_page_gateway_probe, NULL, NULL,
                         gateway_out, sizeof gateway_out, &gateway_rc) != 1) {
            fprintf(stderr, "gateway probe: http none\n");
            return 1;
        }
    } else {
        /* Default: podman run ... curl -s -o /dev/null --connect-timeout 20 -w '%{http_code}' <gw> */
        char cmd[PAGE_PATH_CAP];
        if (jc_snprintf(cmd, sizeof cmd,
                        "podman run --rm --network=pasta:-T,1234 localhost/jichi-drive:2026-10-17 curl -s -o /dev/null --connect-timeout 20 -w '%%{http_code}' %s",
                        gateway_url) >= (int)sizeof cmd) {
            fprintf(stderr, "gateway probe: http none\n");
            return 1;
        }
        if (page_run_cmd(cmd, NULL, NULL, gateway_out, sizeof gateway_out, &gateway_rc) != 1) {
            fprintf(stderr, "gateway probe: http none\n");
            return 1;
        }
    }
    page_strip_nl(gateway_out);

    /* b14: '' or 000 = not reachable. 401/5xx = reachable. */
    if (gateway_out[0] == '\0' || strcmp(gateway_out, "000") == 0) {
        fprintf(stderr, "gateway probe: http %s\n", gateway_out[0] == '\0' ? "none" : gateway_out);
        return 1;
    }

    /* Gateway reachable. If --gateway-probe alone, we're done. */
    if (gateway_probe) {
        fprintf(stderr, "gateway probe: http %s\n", gateway_out);
        return 0;
    }

    /* Model half (b15-b18): read the key. */
    model_only:
    if (!page_read_file(key_file, key_buf, sizeof key_buf, NULL)) {
        fprintf(stderr, "no readable key file (%s)\n", key_file);
        return 1;
    }
    key_len = strlen(key_buf);
    /* Strip trailing newline again (page_read_file does it, but also leading whitespace). */
    {
        char *trimmed = page_trim(key_buf);
        key_len = strlen(trimmed);
    }
    if (key_len == 0) {
        fprintf(stderr, "the key file is empty\n");
        return 1;
    }
    /* Copy the first 12 chars for redaction. */
    if (key_len >= PAGE_KEY_PREFIX) {
        memcpy(key_prefix, key_buf, PAGE_KEY_PREFIX);
        key_prefix[PAGE_KEY_PREFIX] = '\0';
    } else {
        memcpy(key_prefix, key_buf, key_len);
        key_prefix[key_len] = '\0';
    }

    /* Model probe command. */
    if (cfg.host_page_model_probe != NULL && cfg.host_page_model_probe[0] != '\0') {
        /* The config line is the full command - just use it */
        strncpy(model_probe_cmd, cfg.host_page_model_probe, sizeof model_probe_cmd - 1);
        model_probe_cmd[sizeof(model_probe_cmd) - 1] = '\0';
    } else {
        /* Default: podman doctor --live form */
        if (jc_snprintf(model_probe_cmd, sizeof model_probe_cmd,
                        "podman run --rm --init --userns=keep-id --security-opt=no-new-privileges --cap-drop=all --network=pasta:-T,1234 -v %s:%s:rw -v %s/conf:%s/conf:ro -v %s/bin:%s/bin:ro -e JICHI_API_KEY -e HOME=%s/home -e TMPDIR=/tmp -e TZ=Europe/Berlin -e PATH=%s/bin:/usr/local/bin:/usr/bin:/bin -w %s localhost/jichi-drive:2026-10-17 %s/bin/jichi --config %s/conf/jichi-config.json --model %s doctor --live",
                        cfg.dir, cfg.dir, cfg.dir, cfg.dir, cfg.dir, cfg.dir,
                        cfg.dir, cfg.dir, cfg.dir,
                        cfg.dir, cfg.dir, model_name) >= (int)sizeof model_probe_cmd) {
            fprintf(stderr, "no readable key file (%s)\n", key_file);
            return 1;
        }
    }

    /* Run the model probe with the key in env. */
    if (page_run_cmd(model_probe_cmd, "JICHI_API_KEY", key_buf,
                     model_out, sizeof model_out, &model_rc) != 1) {
        fprintf(stderr, "doctor rc=1: no --live line\n");
        return 1;
    }
    page_strip_nl(model_out);

    /* l = the first output line holding `--live`, leading ws stripped, cut to 120. */
    {
        char *p = model_out;
        char *end = model_out + strlen(model_out);
        char *live_start = NULL;
        char *live_end = NULL;

        /* Find first line with --live. */
        while (p < end) {
            char *nl = strchr(p, '\n');
            if (nl != NULL) {
                *nl = '\0';
            }
            if (strstr(p, "--live") != NULL) {
                live_start = p;
                live_end = nl ? nl : end;
                break;
            }
            if (nl == NULL) break;
            p = nl + 1;
        }

        if (live_start != NULL) {
            /* Trim leading whitespace. */
            while (*live_start == ' ' || *live_start == '\t') {
                live_start++;
            }
            /* Cut to 120. */
            if ((size_t)(live_end - live_start) > 120) {
                live_end = live_start + 120;
            }
            /* Copy to model_line. */
            {
                size_t len = (size_t)(live_end - live_start);
                if (len > sizeof(model_line) - 1) {
                    len = sizeof(model_line) - 1;
                }
                memcpy(model_line, live_start, len);
                model_line[len] = '\0';
            }
        } else {
            strncpy(model_line, "no --live line", sizeof model_line - 1);
            model_line[sizeof(model_line) - 1] = '\0';
            model_rc = 1;  /* No --live line, treat as failure */
        }
    }

    /* REDACT (E1, BEFORE any ready test): if l holds the key's first 12 characters, l = `(a --live line, redacted: it held the key's prefix)`. */
    if (strstr(model_line, key_prefix) != NULL) {
        strncpy(model_line, "(a --live line, redacted: it held the key's prefix)", sizeof model_line - 1);
            model_line[sizeof(model_line) - 1] = '\0';
        /* A redacted line cannot be ready (E1). */
    }

    /* say `doctor rc=<r>: <l or no --live line>`. */
    fprintf(stderr, "doctor rc=%d: %s\n", model_rc, model_line);

    /* b18: READY-half-2: l STARTS `ok --live: tool calling observed`. */
    if (model_rc == 0 && strncmp(model_line, "ok --live: tool calling observed", 32) == 0) {
        /* Ready. */
    } else {
        /* Not ready. */
        if (model_probe) {
            return 1;
        }
        /* --probe form: gateway not reachable -> rc 1 (already returned above).
         * Gateway reachable -> run the model half, rc = the model half's. */
        return 1;
    }

    /* --probe form: ready. */
    if (probe) {
        fprintf(stderr, "ready: the gateway and the lead's model answer\n");
        return 0;
    }

    /* --model-probe form: the model half alone, say + rc as the reference. */
    if (model_probe) {
        return 0;
    }

    return 2;
}
