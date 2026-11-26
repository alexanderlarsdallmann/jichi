/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_park.c - the park / restart verbs (M1213 T3, H12;
 * batch H12, drive of 2026-10-27 hardening-host drive's park-resume.sh
 * and restart-loop.sh; 9 cells).
 *
 * The implementation is the C89 port of the reference's park-resume.sh
 * and restart-loop.sh. */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "jc_harness_park.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

#include "jc_platform.h"  /* jc_status, jc_size, jc_mkdir_p, jc_is_dir */
#include "jc_mem.h"       /* struct jc_arena, jc_arena_strdup */
#include "jc_str.h"       /* struct jc_sb */
#include "jc_vec.h"       /* struct jc_vec */
#include "jc_snprintf.h"  /* jc_snprintf, never sprintf */
#include "jc_proc.h"      /* jc_proc_capture, jc_proc_secret_env_add, jc_proc_scrub_secret_env */
#include "jc_harness_config.h"
#include "jc_harness_page.h"

/* Paths hang off the config's dir; give the joins room. */
#define PARK_PATH_CAP 4352
/* The key file is small. */
#define PARK_KEY_CAP 8192
/* The model key's 12-char prefix, kept in memory for redaction. */
#define PARK_KEY_PREFIX 12
/* The doctor output's --live line is cut to 120. */
#define PARK_DOCTOR_LINE_CAP 121
/* The doctor output CAPTURE must hold the full output so the first --live line
 * is found in the whole buffer (the reference mprobe pipes unlimited output
 * through grep --live | head -1); 4096 is the page/probe verb's cap (M1213
 * review item 6: a 130-byte preamble + the ok line must fit). */
#define PARK_DOCTOR_CAP 4096
/* The captured command output cap (hook, unit state, etc). */
#define PARK_CMD_CAP 8192

/* The default values (park-resume.sh header). */
#define PARK_WAIT_INTERVAL_DEFAULT 30
#define PARK_READY_INTERVAL_DEFAULT 60
#define PARK_NOT_READY_INTERVAL_DEFAULT 300

/* The interval defaults for restart (restart-loop.sh header). */
#define RESTART_WAIT_INTERVAL_DEFAULT 30

/* Strip trailing newline(s) from a captured reading. */
static void park_strip_nl(char *s)
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

/* Print the last up-to-3 COMPLETE lines of the file, oldest first, each with its
 * trailing newline -- `tail -3` semantics (park-resume.sh:79, the port read the
 * FIRST 3, review item 5). Unbounded in lines and bytes (a 256KB read cap
 * would print nothing on a larger real log, a divergence from tail); a file
 * beyond the cap is refused with a line, not truncated. A missing or empty
 * file prints nothing, as the current code does. */
static void
park_print_tail3(const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t cap, total;
    char *start;
    int lines, i;

    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return;
    }
    rewind(f);
    if (size == 0) {
        fclose(f);
        return;
    }
    if (size > 33554432L) {  /* 32MB refusal, not a truncation */
        fclose(f);
        fprintf(stderr, "(harness.log is %ld bytes; last 3 lines not printed)\n", size);
        return;
    }
    cap = (size_t)size + 1;
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        fclose(f);
        return;
    }
    total = fread(buf, 1, cap, f);
    buf[total] = '\0';
    fclose(f);

    lines = 0;
    for (i = 0; i < (int)total; i++) {
        if (buf[i] == '\n') {
            lines++;
        }
    }
    if (total > 0 && buf[total - 1] != '\n') {
        lines++;
    }

    if (lines <= 3) {
        start = buf;
    } else {
        /* The start of the last 3 lines is the byte after the (lines - 3)th
         * newline; count newlines, do not assume one per byte. */
        int want = lines - 3;
        int seen = 0;
        start = buf;
        for (i = 0; i < (int)total; i++) {
            if (buf[i] == '\n') {
                seen++;
                if (seen == want) {
                    start = buf + i + 1;
                    break;
                }
            }
        }
    }
    while (*start != '\0') {
        fputc(*start, stderr);
        start++;
    }
    free(buf);
}

/* The pinned build line: `pinned: <readlink pins/current>` (park-resume.sh:99,
 * restart-loop.sh's own pinned line is the same verb's output, note b29). The
 * DECLARED CHANGED: readlink, falling back to the file CONTENT trimmed -- the
 * reference is a symlink, the fixture's pins/current may be a plain file. */
static void
park_print_pinned(const char *drive_dir, int *out_rc)
{
    char pins_path[PARK_PATH_CAP];
    char pinned_buf[64];
    char line[128];
    FILE *pf;
    size_t rd;

    if (jc_snprintf(pins_path, sizeof pins_path, "%s/pins/current", drive_dir)
        >= (int)sizeof pins_path) {
        *out_rc = 2;
        return;
    }
    if (readlink(pins_path, pinned_buf, sizeof pinned_buf - 1) < 0) {
        pf = fopen(pins_path, "rb");
        if (pf == NULL) {
            if (jc_snprintf(pinned_buf, sizeof pinned_buf, "unknown")
                >= (int)sizeof pinned_buf) {
                *out_rc = 2;
                return;
            }
        } else {
            rd = fread(pinned_buf, 1, sizeof pinned_buf - 1, pf);
            pinned_buf[rd] = '\0';
            fclose(pf);
            park_strip_nl(pinned_buf);
        }
    } else {
        pinned_buf[sizeof pinned_buf - 1] = '\0';
    }
    if (jc_snprintf(line, sizeof line, "pinned: %s", pinned_buf) >= (int)sizeof line) {
        *out_rc = 2;
        return;
    }
    fprintf(stderr, "%s\n", line);
    *out_rc = 0;
}

/* Run one shimmable command through the harness's capture idiom. */
static int park_run_cmd(const char *cmdline, const char *env_var, const char *env_val,
                        char *out_text, size_t cap_out, int *out_rc)
{
    char *argv[4];
    struct jc_vec envv;
    struct jc_sb capture;
    char ke[14 + PARK_KEY_CAP + 1];
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

/* Strip trailing whitespace in place, return start pointer. */
static char *park_trim(char *s)
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

/* The say() stamp with UTC offset. */
static int park_stamp(char *out, size_t cap)
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
    /* the zone offset */
    {
        long y = tmv.tm_year + 1900;
        long m = tmv.tm_mon + 1;
        long d = tmv.tm_mday;
        long era, yoe, doy, doe, days;
        long off_min;
        int sign;
        int off_h;
        int off_m;

        y -= (m <= 2) ? 1 : 0;
        era = (y >= 0 ? y : y - 399) / 400;
        yoe = y - era * 400;
        doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + doe - 719468;
        off_min = (long)(days * 86400L + tmv.tm_hour * 3600L
                         + tmv.tm_min * 60L + tmv.tm_sec) - (long)t;
        off_min /= 60;
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

/* b23 say(): one line to stdout AND appended to the out file. */
static void park_say(const char *out, const char *msg)
{
    char stamp[64];
    char line[PARK_PATH_CAP];

    printf("%s\n", msg);
    fflush(stdout);
    if (out == NULL || out[0] == '\0') {
        return;
    }
    if (park_stamp(stamp, sizeof stamp)) {
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

/* Count occurrences of substring in file. */
static int park_count_lines(const char *path, const char *substring)
{
    FILE *f;
    char buf[8192];
    int count = 0;
    char *p;

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    while (fgets(buf, sizeof buf, f) != NULL) {
        p = buf;
        while ((p = strstr(p, substring)) != NULL) {
            count++;
            p++;
        }
    }
    fclose(f);
    return count;
}

/* Get harness.log last line. Returns 1 on success, 0 on failure. */
static int park_log_last(const char *log_path, char *out, size_t cap)
{
    FILE *f;
    long fsize, start;
    char buf[8192];
    size_t n, i, ls;

    out[0] = '\0';

    f = fopen(log_path, "rb");
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        return 0;
    }
    start = fsize - (long)sizeof buf;
    if (start < 0) {
        start = 0;
    }
    if (fseek(f, start, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    n = fread(buf, 1, (size_t)(fsize - start), f);
    buf[n] = '\0';
    fclose(f);
    if (n == 0) {
        return 0;
    }

    /* Last content line: from after the previous newline to the end, with the
     * trailing newline stripped. The script's `tail -1` returns it even when
     * the file ends with a newline. */
    i = n;
    while (i > 0 && (buf[i - 1] == '\n' || buf[i - 1] == '\r')) {
        i--;
    }
    if (i == 0) {
        return 1;   /* readable, all whitespace */
    }
    ls = i;
    while (ls > 0 && buf[ls - 1] != '\n') {
        ls--;
    }
    if (ls == 0 && start > 0) {
        return 0;   /* last line longer than the chunk: unreadable */
    }
    if (i - ls >= cap) {
        return 0;
    }
    memcpy(out, buf + ls, i - ls);
    out[i - ls] = '\0';
    return 1;
}

/* The last two content lines of the log (the script's `tail -1` and
 * `tail -2 | head -1`). Returns 1 when the file is readable, 0 when it is
 * missing or empty; fills `last` and, when a previous line exists, `prev`.
 * Reads only the last sizeof buf bytes: the last two lines live at the file's
 * end, and a line that crosses the chunk boundary only truncates a PARTIAL
 * first line, which is never returned (the guard below bails to unreadable). */
static int park_log_tail2(const char *log_path, char *last, size_t last_cap,
                          char *prev, size_t prev_cap)
{
    FILE *f;
    long fsize, start;
    char buf[16384];
    size_t n, i, ls, pl, j;

    last[0] = '\0';
    prev[0] = '\0';

    f = fopen(log_path, "rb");
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        return 0;
    }
    start = fsize - (long)sizeof buf;
    if (start < 0) {
        start = 0;
    }
    if (fseek(f, start, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    n = fread(buf, 1, (size_t)(fsize - start), f);
    buf[n] = '\0';
    fclose(f);
    if (n == 0) {
        return 0;
    }

    i = n;
    while (i > 0 && (buf[i - 1] == '\n' || buf[i - 1] == '\r')) {
        i--;
    }
    if (i == 0) {
        return 1;   /* readable, all whitespace */
    }
    ls = i;
    while (ls > 0 && buf[ls - 1] != '\n') {
        ls--;
    }
    if (ls == 0 && start > 0) {
        return 0;   /* last line longer than the chunk: unreadable */
    }
    if (i - ls < last_cap) {
        memcpy(last, buf + ls, i - ls);
        last[i - ls] = '\0';
    } else {
        return 0;
    }
    j = ls;
    while (j > 0 && (buf[j - 1] == '\n' || buf[j - 1] == '\r')) {
        j--;
    }
    if (j > 0) {
        pl = j;
        while (pl > 0 && buf[pl - 1] != '\n') {
            pl--;
        }
        if (j - pl < prev_cap) {
            memcpy(prev, buf + pl, j - pl);
            prev[j - pl] = '\0';
        }
    }
    return 1;
}

/* Validate numeric flag: all digits, no negative. Returns 0 if valid, 2 if invalid. */
int park_validate_numeric(const char *val, const char *name)
{
    const char *p = val;
    if (p == NULL || *p == '\0') {
        fprintf(stderr, "jichi-harness: %s: usage: <non-negative-integer>\n", name);
        return 2;
    }
    if (*p == '-') {
        fprintf(stderr, "jichi-harness: %s: usage: <non-negative-integer> (got negative)\n", name);
        return 2;
    }
    while (*p) {
        if (!isdigit((unsigned char)*p)) {
            fprintf(stderr, "jichi-harness: %s: usage: <non-negative-integer> (got '%s')\n", name, val);
            return 2;
        }
        p++;
    }
    return 0;
}

/* Read the model key file and return the key (stripped) or NULL on error. */
static char *park_read_key(const char *key_path, char *buf, size_t cap)
{
    FILE *f;
    size_t n = 0;
    char *trimmed;

    if (cap == 0) {
        return NULL;
    }
    buf[0] = '\0';
    f = fopen(key_path, "rb");
    if (f == NULL) {
        return NULL;
    }
    while (n + 1 < cap) {
        int c = fgetc(f);
        if (c == EOF) {
            break;
        }
        buf[n++] = (char)c;
    }
    fclose(f);
    buf[n] = '\0';
    park_strip_nl(buf);

    trimmed = park_trim(buf);
    if (strlen(trimmed) == 0) {
        return NULL;
    }
    return trimmed;
}

/* The gateway probe: ONE keyless child. Returns 1 if reachable (any HTTP code except '' or '000'), 0 otherwise. */
static int park_gateway_probe(const char *cmd)
{
    char stdout_buf[16];
    int cmd_rc;

    if (cmd != NULL && cmd[0] != '\0') {
        if (park_run_cmd(cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) != 1) {
            fprintf(stderr, "gateway probe: http none\n");
            return 0;
        }
    } else {
        /* Default: podman run ... curl */
        char cmd_buf[PARK_PATH_CAP];
        if (jc_snprintf(cmd_buf, sizeof cmd_buf,
                        "podman run --rm --network=pasta:-T,1234 localhost/jichi-drive:2026-10-17 curl -s -o /dev/null --connect-timeout 20 -w '%%{http_code}' https://api.hrz.uni-giessen.de/v1/models") >= (int)sizeof cmd_buf) {
            fprintf(stderr, "gateway probe: http none\n");
            return 0;
        }
        if (park_run_cmd(cmd_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) != 1) {
            fprintf(stderr, "gateway probe: http none\n");
            return 0;
        }
    }
    park_strip_nl(stdout_buf);

    if (stdout_buf[0] == '\0' || strcmp(stdout_buf, "000") == 0) {
        fprintf(stderr, "gateway probe: http %s\n", stdout_buf[0] == '\0' ? "none" : stdout_buf);
        return 0;
    }

    /* Gateway reachable. */
    return 1;
}

/* The model probe: read key, run command with key in env. Returns 0 if ready, 1 if not ready, 2 on error. */
static int park_model_probe(const char *cmd, const char *key_path, char *key_prefix)
{
    char key_buf[PARK_KEY_CAP];
    char *key;
    char model_out[PARK_DOCTOR_CAP];
    int model_rc;
    char model_line[PARK_DOCTOR_LINE_CAP];
    char cmd_buf[PARK_PATH_CAP];

    key = park_read_key(key_path, key_buf, sizeof key_buf);
    if (key == NULL) {
        fprintf(stderr, "no readable key file (%s)\n", key_path);
        return 2;
    }

    /* Copy first 12 chars for redaction. */
    if (strlen(key) >= PARK_KEY_PREFIX) {
        memcpy(key_prefix, key, PARK_KEY_PREFIX);
        key_prefix[PARK_KEY_PREFIX] = '\0';
    } else {
        memcpy(key_prefix, key, strlen(key));
        key_prefix[strlen(key)] = '\0';
    }

    if (cmd != NULL && cmd[0] != '\0') {
        strncpy(cmd_buf, cmd, sizeof cmd_buf - 1);
        cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    } else {
        /* Default: podman doctor --live form */
        fprintf(stderr, "jichi-harness: park: model probe default form not implemented (needs config)\n");
        return 2;
    }

    if (park_run_cmd(cmd_buf, "JICHI_API_KEY", key, model_out, sizeof model_out, &model_rc) != 1) {
        fprintf(stderr, "doctor rc=1: no --live line\n");
        return 2;
    }
    park_strip_nl(model_out);

    /* Find first line with --live. */
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
            fprintf(stderr, "doctor rc=%d: no --live line\n", model_rc);
            return 2;
        }
    }

    /* REDACT: if l holds the key's first 12 characters, l = `(a --live line, redacted: it held the key's prefix)`. */
    if (strstr(model_line, key_prefix) != NULL) {
        strncpy(model_line, "(a --live line, redacted: it held the key's prefix)", sizeof model_line - 1);
        model_line[sizeof(model_line) - 1] = '\0';
        fprintf(stderr, "doctor rc=%d: %s\n", model_rc, model_line);
        return 2;  /* A redacted line cannot be ready. */
    }

    fprintf(stderr, "doctor rc=%d: %s\n", model_rc, model_line);

    /* READY: l starts `ok --live: tool calling observed`. */
    if (strncmp(model_line, "ok --live: tool calling observed", 32) == 0) {
        return 0;  /* Ready */
    }
    return 1;  /* Not ready */
}

/* The park verb body */
int
jc_harness_park_run(const char *config_path,
                    const char *state_path, const char *out_file,
                    int park_parked, int park_interval,
                    int park_wait_interval, int park_ready_interval,
                    int park_not_ready_interval, const char *park_post_start,
                    void *arena_ptr)
{
    struct jc_harness_config cfg;
    struct jc_arena *arena = (struct jc_arena *)arena_ptr;
    char *drive_dir = NULL;
    char line_buf[PARK_PATH_CAP];
    char unit_name[64];
    char unit_state[64];
    char harness_log[PARK_PATH_CAP];
    int n0, n1;
    int wait_interval;
    int ready_interval;
    int not_ready_interval;
    int post_start;
    char stop_path[PARK_PATH_CAP];
    char host_dir[PARK_PATH_CAP];
    char moved_stop[PARK_PATH_CAP];
    int loop_end_ok;
    char last_line[PARK_PATH_CAP];
    int ok;
    int rc;
    int failed_flag = 0;  /* A2.3: initialized for A2.3 rule (no -Wmaybe-uninitialized) */
    char gateway_cmd[PARK_PATH_CAP];
    char model_probe_cmd[PARK_PATH_CAP];
    char container_gone_cmd[PARK_PATH_CAP];
    char unit_run_cmd[PARK_PATH_CAP];
    char pid_list_cmd[PARK_PATH_CAP];
    char unit_failed_cmd[PARK_PATH_CAP];
    char key_prefix[PARK_KEY_PREFIX + 1];
    char stdout_buf[PARK_CMD_CAP];
    int cmd_rc;
    char container_gone_out[16];

    (void)park_wait_interval;  /* A2.1: park_interval is the armed-poll; this is legacy */
    (void)park_ready_interval;  /* A2.1: park_interval is the armed-poll; this is legacy */
    (void)park_not_ready_interval;  /* A2.1: park_interval is the armed-poll; this is legacy */
    (void)park_post_start;  /* A2.2: validated in body */
    (void)arena_ptr;  /* For A2.4 config parsing */

    /* --state and --out are optional (some cells don't use them). */

    /* Load the config. */
    if (jc_harness_config_load(config_path, &cfg, arena) != JC_OK) {
        fprintf(stderr, "jichi-harness: park: %s\n", cfg.message);
        return 2;
    }

    /* Extract config fields. */
    drive_dir = cfg.dir;
    /* SV:0033: no built-in unit name. The config MUST specify drive.loopUnit. */
    if (cfg.drive_loop_unit == NULL || cfg.drive_loop_unit[0] == '\0') {
        fprintf(stderr, "jichi-harness: park: the config names no drive.loopUnit; set it in the config\n");
        return 1;
    }
    strncpy(unit_name, cfg.drive_loop_unit, sizeof unit_name - 1);
    unit_name[sizeof(unit_name) - 1] = '\0';

    /* Flags with defaults and validation (A2.4: flag -> config -> default).
     * A2.1: park_interval is the armed-poll interval. When 0 (absent), use
     * config's drive_wait_interval or the reference's 30. */
    if (park_interval == 0) {
        if (cfg.drive_wait_interval != NULL && cfg.drive_wait_interval[0] != '\0') {
            wait_interval = atoi(cfg.drive_wait_interval);
        } else {
            wait_interval = PARK_WAIT_INTERVAL_DEFAULT;
        }
    } else {
        wait_interval = park_interval;
    }
    if (park_ready_interval == 0) {
        if (cfg.drive_ready_interval != NULL && cfg.drive_ready_interval[0] != '\0') {
            ready_interval = atoi(cfg.drive_ready_interval);
        } else {
            ready_interval = PARK_READY_INTERVAL_DEFAULT;
        }
    } else {
        ready_interval = park_ready_interval;
    }
    if (park_not_ready_interval == 0) {
        if (cfg.drive_not_ready_interval != NULL && cfg.drive_not_ready_interval[0] != '\0') {
            not_ready_interval = atoi(cfg.drive_not_ready_interval);
        } else {
            not_ready_interval = PARK_NOT_READY_INTERVAL_DEFAULT;
        }
    } else {
        not_ready_interval = park_not_ready_interval;
    }

    /* --post-start: optional duration in seconds (A2.2), absent = 0 (no sleep).
     * Explicit 0 is allowed (means "no sleep"); only reject negative. */
    post_start = 0;
    if (park_post_start != NULL && park_post_start[0] != '\0') {
        if (park_validate_numeric(park_post_start, "--post-start") != 0) {
            return 2;
        }
        post_start = atoi(park_post_start);
    }

    (void)state_path;
    (void)out_file;
    /* Get the unit state for the armed line. */
    if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
        if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                         stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            park_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    } else {
        /* Default: systemctl --user is-active <U> */
        if (jc_snprintf(line_buf, sizeof line_buf, "systemctl --user is-active %s", unit_name) >= (int)sizeof line_buf) {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else if (park_run_cmd(line_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            park_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    }

    /* Build the harness.log path. */
    if (jc_snprintf(harness_log, sizeof harness_log, "%s/harness.log", drive_dir) >= (int)sizeof harness_log) {
        fprintf(stderr, "jichi-harness: park: path too long\n");
        return 2;
    }

    /* Build the STOP path (used by both forms). */
    if (jc_snprintf(stop_path, sizeof stop_path, "%s/STOP", drive_dir) >= (int)sizeof stop_path) {
        fprintf(stderr, "jichi-harness: park: path too long\n");
        return 2;
    }

    if (park_parked) {
        /* --parked form: check the already-parked state instead of waiting. */
        /* Check unit state is NOT active and NOT failed, STOP present, harness.log ending with STOP lines. */

        /* Get unit state again for the check. */
        if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
            if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                             stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                park_strip_nl(stdout_buf);
                strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
                unit_state[sizeof(unit_state) - 1] = '\0';
            } else {
                strncpy(unit_state, "unknown", sizeof unit_state - 1);
                unit_state[sizeof(unit_state) - 1] = '\0';
            }
        }

        /* Check unit not failed. */
        if (cfg.host_page_unit_failed != NULL && cfg.host_page_unit_failed[0] != '\0') {
            strncpy(unit_failed_cmd, cfg.host_page_unit_failed, sizeof unit_failed_cmd - 1);
            unit_failed_cmd[sizeof(unit_failed_cmd) - 1] = '\0';
            if (park_run_cmd(unit_failed_cmd, NULL, NULL,
                             stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                /* E4: rc 0 == FAILED */
                failed_flag = (cmd_rc == 0) ? 1 : 0;
            } else {
                failed_flag = 0;  /* Assume not failed on error */
            }
        } else {
            /* Default: systemctl --user is-failed -q <U> */
            if (jc_snprintf(line_buf, sizeof line_buf, "systemctl --user is-failed -q %s", unit_name) >= (int)sizeof line_buf) {
                failed_flag = 0;
            } else if (park_run_cmd(line_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                failed_flag = (cmd_rc == 0) ? 1 : 0;
            } else {
                failed_flag = 0;
            }
        }

        /* Composite --parked check (park-resume.sh:68-73): one refusal for
         * unit active, unit failed, STOP absent, last line not "loop end", or
         * previous line not the STOP-ended line; else armed. The message names
         * the unit's is-active state, then the last two lines (tail -2). */
        {
            char prev_line[PARK_PATH_CAP];
            int tail2_ok;
            int stop_ok;
            int last_ok;
            int prev_ok;

            if (jc_snprintf(stop_path, sizeof stop_path, "%s/STOP", drive_dir) >= (int)sizeof stop_path) {
                fprintf(stderr, "jichi-harness: park: path too long\n");
                return 2;
            }
            stop_ok = (access(stop_path, F_OK) == 0);

            tail2_ok = park_log_tail2(harness_log, last_line, sizeof last_line,
                                      prev_line, sizeof prev_line);
            last_ok = (tail2_ok && strstr(last_line, "loop end") != NULL);
            prev_ok = (tail2_ok && strstr(prev_line, "STOP file present: loop ends") != NULL);

            if (strcmp(unit_state, "active") == 0 || failed_flag || !stop_ok || !last_ok || !prev_ok) {
                fprintf(stderr,
                        "NOT resuming (--parked): the loop is not parked on STOP (unit %s); harness.log ends:\n",
                        unit_state);
                if (tail2_ok) {
                    if (prev_line[0] != '\0') {
                        fprintf(stderr, "%s\n", prev_line);
                    }
                    fprintf(stderr, "%s\n", last_line);
                }
                return 1;
            }

            if (jc_snprintf(line_buf, sizeof line_buf,
                            "armed (--parked): unit %s inactive and not failed, STOP present, harness.log ends with the STOP lines",
                            unit_name) >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 0;
        }

    } else {
        /* Normal park form: count STOP-ended lines (n0), wait for loop to end, then resume check. */

        /* n0 = count of "STOP file present: loop ends" lines. */
        n0 = park_count_lines(harness_log, "STOP file present: loop ends");

        if (jc_snprintf(line_buf, sizeof line_buf,
                        "armed: unit %s %s; STOP-ended lines so far: %d; waiting for the loop to end",
                        unit_name, unit_state, n0) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);

        /* Wait UNCAPPED for the unit to end.
         * Loop while the unit is active; exit the moment it is NOT active
         * (inactive, failed, unknown, etc). */
        while (1) {
            /* Get unit state. */
            if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
                if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                                 stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                    park_strip_nl(stdout_buf);
                    strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
                    unit_state[sizeof(unit_state) - 1] = '\0';
                } else {
                    strncpy(unit_state, "unknown", sizeof unit_state - 1);
                    unit_state[sizeof(unit_state) - 1] = '\0';
                }
            } else {
                /* Default: systemctl --user is-active <U> */
                if (jc_snprintf(line_buf, sizeof line_buf, "systemctl --user is-active %s", unit_name) >= (int)sizeof line_buf) {
                    strncpy(unit_state, "unknown", sizeof unit_state - 1);
                    unit_state[sizeof(unit_state) - 1] = '\0';
                } else if (park_run_cmd(line_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                    park_strip_nl(stdout_buf);
                    strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
                    unit_state[sizeof(unit_state) - 1] = '\0';
                } else {
                    strncpy(unit_state, "unknown", sizeof unit_state - 1);
                    unit_state[sizeof(unit_state) - 1] = '\0';
                }
            }

            /* Stop waiting when unit is NOT active (any state except "active"). */
            if (strcmp(unit_state, "active") != 0) {
                break;
            }

            sleep((unsigned int)wait_interval);
        }

        /* n1 = recount the STOP lines. */
        n1 = park_count_lines(harness_log, "STOP file present: loop ends");

        /* The script's gate counts is-failed (park-resume.sh:80); the normal
         * form must read it too, or a failed unit whose log ended for the STOP
         * file (n1 == n0+1, last line "loop end") would resume a dead drive. */
        if (cfg.host_page_unit_failed != NULL && cfg.host_page_unit_failed[0] != '\0') {
            if (park_run_cmd(cfg.host_page_unit_failed, NULL, NULL,
                             stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                failed_flag = (cmd_rc == 0) ? 1 : 0;
            } else {
                failed_flag = 0;
            }
        }

        /* Last line must contain "loop end". */
        if (!park_log_last(harness_log, last_line, sizeof last_line)) {
            if (jc_snprintf(line_buf, sizeof line_buf,
                            "NOT resuming: the loop did not end for the STOP file (harness.log unreadable). The supervisor decides.") >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 1;
        }
        loop_end_ok = (strstr(last_line, "loop end") != NULL);

        if (jc_snprintf(line_buf, sizeof line_buf,
                        "the unit is %s; STOP-ended lines now: %d; harness.log's last 3 lines:",
                        unit_state, n1) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        /* Print the last 3 lines (tail -3, the reference park-resume.sh:79). */
        park_print_tail3(harness_log);

        /* Resume gate: n1 == n0+1 AND last line contains "loop end" AND unit not failed. */
        if (n1 != n0 + 1 || !loop_end_ok || failed_flag) {
            if (jc_snprintf(line_buf, sizeof line_buf,
                            "NOT resuming: the loop did not end for the STOP file (or the unit failed). The supervisor decides.") >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 1;
        }

        /* READY gate: 2 consecutive READY probes. */
        ok = 0;
        while (ok < 2) {
            /* Gateway probe. */
            if (cfg.host_page_gateway_probe != NULL && cfg.host_page_gateway_probe[0] != '\0') {
                strncpy(gateway_cmd, cfg.host_page_gateway_probe, sizeof gateway_cmd - 1);
                gateway_cmd[sizeof(gateway_cmd) - 1] = '\0';
            } else {
                gateway_cmd[0] = '\0';
            }

            if (!park_gateway_probe(gateway_cmd)) {
                /* Not ready - reset count and wait. */
                ok = 0;
                sleep((unsigned int)not_ready_interval);
                continue;
            }

            /* Model probe. */
            if (cfg.host_page_model_probe != NULL && cfg.host_page_model_probe[0] != '\0') {
                strncpy(model_probe_cmd, cfg.host_page_model_probe, sizeof model_probe_cmd - 1);
                model_probe_cmd[sizeof(model_probe_cmd) - 1] = '\0';
            } else {
                model_probe_cmd[0] = '\0';
            }

            rc = park_model_probe(model_probe_cmd, cfg.model_key_file, key_prefix);
            if (rc == 0) {
                /* Ready. */
                ok++;
                if (ok < 2) {
                    sleep((unsigned int)ready_interval);
                }
            } else if (rc == 1) {
                /* Not ready - reset count and wait. */
                ok = 0;
                sleep((unsigned int)not_ready_interval);
            } else {
                /* Error - treat as not ready. */
                ok = 0;
                sleep((unsigned int)not_ready_interval);
            }
        }

        if (jc_snprintf(line_buf, sizeof line_buf,
                        "the gateway and the lead's model answer again (2 consecutive probes)") >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);

        /* Wait for container to be gone. */
        if (cfg.host_page_container_gone != NULL && cfg.host_page_container_gone[0] != '\0') {
            strncpy(container_gone_cmd, cfg.host_page_container_gone, sizeof container_gone_cmd - 1);
            container_gone_cmd[sizeof(container_gone_cmd) - 1] = '\0';
        } else {
            /* Default: while podman container exists <name>; do sleep 5; done.
             * SV:0033: no built-in container name. The config MUST name it. */
            char container_name[64];
            if (cfg.drive_container_name != NULL && cfg.drive_container_name[0] != '\0') {
                strncpy(container_name, cfg.drive_container_name, sizeof container_name - 1);
                container_name[sizeof(container_name) - 1] = '\0';
            } else {
                fprintf(stderr, "jichi-harness: park: the config names no drive.containerName; set it in the config\n");
                return 1;
            }
            if (jc_snprintf(container_gone_cmd, sizeof container_gone_cmd,
                            "while podman container exists %s; do sleep 5; done", container_name) >= (int)sizeof container_gone_cmd) {
                return 2;
            }
        }

        while (1) {
            if (park_run_cmd(container_gone_cmd, NULL, NULL,
                             container_gone_out, sizeof container_gone_out, &cmd_rc) == 1) {
                /* The command's OWN rc is the check (the script: while podman
                 * container exists <name>; do sleep 5; done): a command that
                 * ran but exited non-zero still means the container is present. */
                if (cmd_rc == 0) {
                    break;
                }
            }
            sleep(5);
        }

        /* Move STOP aside. */
        if (cfg.host_dir != NULL && cfg.host_dir[0] != '\0') {
            strncpy(host_dir, cfg.host_dir, sizeof host_dir - 1);
            host_dir[sizeof(host_dir) - 1] = '\0';
        } else {
            strncpy(host_dir, drive_dir, sizeof host_dir - 1);
            host_dir[sizeof(host_dir) - 1] = '\0';
        }

        /* Create moved stop name with date. */
        {
            time_t t;
            struct tm *tmr;
            char date_buf[16];

            t = time(NULL);
            tmr = localtime(&t);
            if (tmr != NULL) {
                strftime(date_buf, sizeof date_buf, "%Y%m%d-%H%M", tmr);
            } else {
                memcpy(date_buf, "00000000-0000", 14);
            }
            if (jc_snprintf(moved_stop, sizeof moved_stop, "%s/STOP.used-%s", host_dir, date_buf) >= (int)sizeof moved_stop) {
                return 2;
            }
        }

        /* Rename STOP to moved_stop. */
        if (rename(stop_path, moved_stop) != 0) {
            if (jc_snprintf(line_buf, sizeof line_buf,
                            "STOP is still there: %s", strerror(errno)) >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 1;
        }

        if (jc_snprintf(line_buf, sizeof line_buf,
                        "STOP moved to %s", moved_stop) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);

        /* Double-check STOP is gone. */
        if (access(stop_path, F_OK) == 0) {
            if (jc_snprintf(line_buf, sizeof line_buf,
                            "NOT resuming: %s/STOP is still there", drive_dir) >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 1;
        }

        /* Start the unit. */
        if (cfg.host_page_unit_run != NULL && cfg.host_page_unit_run[0] != '\0') {
            strncpy(unit_run_cmd, cfg.host_page_unit_run, sizeof unit_run_cmd - 1);
            unit_run_cmd[sizeof(unit_run_cmd) - 1] = '\0';
        } else {
            /* Default: systemd-run --user --unit=<U> -p WorkingDirectory=<H> sh -c "exec <H>/run-drive.sh >> <H>/drive-loop.out 2>&1" */
            if (jc_snprintf(unit_run_cmd, sizeof unit_run_cmd,
                            "systemd-run --user --unit=%s -p WorkingDirectory=%s sh -c \"exec %s/run-drive.sh >> %s/drive-loop.out 2>&1\"",
                            unit_name, host_dir, drive_dir, drive_dir) >= (int)sizeof unit_run_cmd) {
                return 2;
            }
        }

        if (park_run_cmd(unit_run_cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) != 1) {
            if (jc_snprintf(line_buf, sizeof line_buf,
                            "the unit start failed (rc=%d)", cmd_rc) >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 1;
        }

        if (cmd_rc != 0) {
            if (jc_snprintf(line_buf, sizeof line_buf,
                            "the unit start failed (rc=%d)", cmd_rc) >= (int)sizeof line_buf) {
                return 2;
            }
            park_say(out_file, line_buf);
            return 1;
        }

        /* Post-start sleep. */
        if (post_start > 0) {
            sleep((unsigned int)post_start);
        }

        /* Get unit state again. */
        if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
            if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                             stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                park_strip_nl(stdout_buf);
                strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
                unit_state[sizeof(unit_state) - 1] = '\0';
            } else {
                strncpy(unit_state, "unknown", sizeof unit_state - 1);
                unit_state[sizeof(unit_state) - 1] = '\0';
            }
        }

        if (jc_snprintf(line_buf, sizeof line_buf,
                        "unit %s: %s", unit_name, unit_state) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);

        /* Record the pid. */
        if (cfg.host_page_pid_list != NULL && cfg.host_page_pid_list[0] != '\0') {
            strncpy(pid_list_cmd, cfg.host_page_pid_list, sizeof pid_list_cmd - 1);
            pid_list_cmd[sizeof(pid_list_cmd) - 1] = '\0';
        } else {
            /* Default: ps -eo pid,args grep */
            if (jc_snprintf(pid_list_cmd, sizeof pid_list_cmd,
                            "ps -eo pid,args | grep '[j]ichi-harness --config .* drive ' | awk '{print $1}'") >= (int)sizeof pid_list_cmd) {
                return 2;
            }
        }

        if (park_run_cmd(pid_list_cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) != 1) {
            return 2;
        }
        park_strip_nl(stdout_buf);

        if (jc_snprintf(line_buf, sizeof line_buf,
                        "loop pid %s started", stdout_buf) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);

        /* The pinned build line (park-resume.sh:99). */
        {
            int pinned_rc = 0;
            park_print_pinned(drive_dir, &pinned_rc);
            if (pinned_rc != 0) {
                return pinned_rc;
            }
        }

        return 0;
    }
}

/* The restart verb body */
int
jc_harness_restart_run(const char *config_path,
                       const char *state_path, const char *out_file,
                       int restart_wait_interval, const char *restart_post_start,
                       void *arena_ptr)
{
    struct jc_harness_config cfg;
    struct jc_arena *arena = (struct jc_arena *)arena_ptr;
    char *drive_dir = NULL;
    char line_buf[PARK_PATH_CAP];
    char unit_name[64];
    char unit_state[64];
    char harness_log[PARK_PATH_CAP];
    int n0, n1;
    int wait_interval;
    int post_start;
    char stop_path[PARK_PATH_CAP];
    char host_dir[PARK_PATH_CAP];
    char moved_stop[PARK_PATH_CAP];
    int loop_end_ok;
    char last_line[PARK_PATH_CAP];
    int failed_flag = 0;  /* A2.3: initialized for A2.3 rule */
    char container_gone_cmd[PARK_PATH_CAP];
    char unit_run_cmd[PARK_PATH_CAP];
    char pid_list_cmd[PARK_PATH_CAP];
    char stdout_buf[PARK_CMD_CAP];
    int cmd_rc;
    char container_gone_out[16];
    int rc;
    (void)rc;  /* declared but unused */
    (void)restart_wait_interval;  /* A2.1: only --wait-interval used; config fallback below */
    (void)arena_ptr;  /* For A2.4 config parsing */

    /* --state and --out are optional (some cells don't use them). */

    /* Load the config. */
    if (jc_harness_config_load(config_path, &cfg, arena) != JC_OK) {
        fprintf(stderr, "jichi-harness: restart: %s\n", cfg.message);
        return 2;
    }

    /* Extract config fields. */
    drive_dir = cfg.dir;
    /* SV:0033: no built-in unit name. The config MUST specify drive.loopUnit. */
    if (cfg.drive_loop_unit == NULL || cfg.drive_loop_unit[0] == '\0') {
        fprintf(stderr, "jichi-harness: restart: the config names no drive.loopUnit; set it in the config\n");
        return 1;
    }
    strncpy(unit_name, cfg.drive_loop_unit, sizeof unit_name - 1);
    unit_name[sizeof(unit_name) - 1] = '\0';

    /* Flags with defaults and validation (A2.4: flag -> config -> default).
     * A2.1 note: restart has no --interval flag (the reference's restart-loop.sh
     * has none), so restart_wait_interval is always 0 or from --wait-interval. */
    if (restart_wait_interval == 0) {
        if (cfg.drive_wait_interval != NULL && cfg.drive_wait_interval[0] != '\0') {
            wait_interval = atoi(cfg.drive_wait_interval);
        } else {
            wait_interval = RESTART_WAIT_INTERVAL_DEFAULT;
        }
    } else {
        wait_interval = restart_wait_interval;
    }

    /* --post-start: optional duration in seconds (A2.2), absent = 0 (no sleep).
     * Explicit 0 is allowed (means "no sleep"); only reject negative. */
    post_start = 0;
    if (restart_post_start != NULL && restart_post_start[0] != '\0') {
        if (park_validate_numeric(restart_post_start, "--post-start") != 0) {
            return 2;
        }
        post_start = atoi(restart_post_start);
    }

    (void)state_path;
    (void)out_file;
    /* Get the unit state for the armed line. */
    if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
        if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                         stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            park_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    } else {
        /* Default: systemctl --user is-active <U> */
        if (jc_snprintf(line_buf, sizeof line_buf, "systemctl --user is-active %s", unit_name) >= (int)sizeof line_buf) {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else if (park_run_cmd(line_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            park_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    }

    /* Build the harness.log path. */
    if (jc_snprintf(harness_log, sizeof harness_log, "%s/harness.log", drive_dir) >= (int)sizeof harness_log) {
        fprintf(stderr, "jichi-harness: restart: path too long\n");
        return 2;
    }

    /* Build the STOP path. */
    if (jc_snprintf(stop_path, sizeof stop_path, "%s/STOP", drive_dir) >= (int)sizeof stop_path) {
        fprintf(stderr, "jichi-harness: restart: path too long\n");
        return 2;
    }

    /* n0 = count of "STOP file present: loop ends" lines. */
    n0 = park_count_lines(harness_log, "STOP file present: loop ends");

    if (jc_snprintf(line_buf, sizeof line_buf,
                    "armed: unit %s %s; STOP-ended lines so far: %d; waiting for the loop to end",
                    unit_name, unit_state, n0) >= (int)sizeof line_buf) {
        return 2;
    }
    park_say(out_file, line_buf);

    /* Wait UNCAPPED for the unit to end (inactive, not failed). */
    while (1) {
        /* Get unit state. */
        if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
            if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                             stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
                park_strip_nl(stdout_buf);
                strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
                unit_state[sizeof(unit_state) - 1] = '\0';
            } else {
                strncpy(unit_state, "unknown", sizeof unit_state - 1);
                unit_state[sizeof(unit_state) - 1] = '\0';
            }
        } else {
        /* Default: systemctl --user is-active <U> */
        if (jc_snprintf(line_buf, sizeof line_buf, "systemctl --user is-active %s", unit_name) >= (int)sizeof line_buf) {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else if (park_run_cmd(line_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            park_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    }

    /* Stop waiting when unit is NOT active (any state except "active"). */
    if (strcmp(unit_state, "active") != 0) {
        break;
    }

    sleep((unsigned int)wait_interval);
    }

    /* n1 = recount the STOP lines. */
    n1 = park_count_lines(harness_log, "STOP file present: loop ends");

    /* The script's gate also counts is-failed for the restart verb
     * (park-resume.sh:80). A default systemctl form mirrors the wait loop
     * above, so a failed unit is caught even when no command is configured. */
    if (cfg.host_page_unit_failed != NULL && cfg.host_page_unit_failed[0] != '\0') {
        if (park_run_cmd(cfg.host_page_unit_failed, NULL, NULL,
                         stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            failed_flag = (cmd_rc == 0) ? 1 : 0;
        } else {
            failed_flag = 0;
        }
    } else if (jc_snprintf(line_buf, sizeof line_buf, "systemctl --user is-failed -q %s", unit_name) < (int)sizeof line_buf
               && park_run_cmd(line_buf, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
        failed_flag = (cmd_rc == 0) ? 1 : 0;
    }

    /* Last line must contain "loop end". */
    if (!park_log_last(harness_log, last_line, sizeof last_line)) {
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "NOT resuming: the loop did not end for the STOP file (harness.log unreadable). The supervisor decides.") >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        return 1;
    }
    loop_end_ok = (strstr(last_line, "loop end") != NULL);

    if (jc_snprintf(line_buf, sizeof line_buf,
                    "the unit is %s; STOP-ended lines now: %d; harness.log's last 3 lines:",
                    unit_state, n1) >= (int)sizeof line_buf) {
        return 2;
    }
    park_say(out_file, line_buf);
    /* Print the last 3 lines (tail -3, the reference park-resume.sh:79). */
    park_print_tail3(harness_log);

    /* Resume gate: n1 == n0+1 AND last line contains "loop end" AND unit not failed. */
    if (n1 != n0 + 1 || !loop_end_ok || failed_flag) {
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "NOT resuming: the loop did not end for the STOP file (or the unit failed). The supervisor decides.") >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        return 1;
    }

    /* Wait for container to be gone. */
    if (cfg.host_page_container_gone != NULL && cfg.host_page_container_gone[0] != '\0') {
        strncpy(container_gone_cmd, cfg.host_page_container_gone, sizeof container_gone_cmd - 1);
        container_gone_cmd[sizeof(container_gone_cmd) - 1] = '\0';
    } else {
        /* Default: while podman container exists <name>; do sleep 5; done.
         * SV:0033: no built-in container name. The config MUST name it. */
        char container_name[64];
        if (cfg.drive_container_name != NULL && cfg.drive_container_name[0] != '\0') {
            strncpy(container_name, cfg.drive_container_name, sizeof container_name - 1);
            container_name[sizeof(container_name) - 1] = '\0';
        } else {
            fprintf(stderr, "jichi-harness: restart: the config names no drive.containerName; set it in the config\n");
            return 1;
        }
        if (jc_snprintf(container_gone_cmd, sizeof container_gone_cmd,
                        "while podman container exists %s; do sleep 5; done", container_name) >= (int)sizeof container_gone_cmd) {
            return 2;
        }
    }

    while (1) {
        if (park_run_cmd(container_gone_cmd, NULL, NULL,
                         container_gone_out, sizeof container_gone_out, &cmd_rc) == 1) {
            /* The command's OWN rc is the check (the script: while podman
             * container exists <name>; do sleep 5; done): a command that ran
             * but exited non-zero still means the container is present. */
            if (cmd_rc == 0) {
                break;
            }
        }
        sleep(5);
    }

    /* Move STOP aside. */
    if (cfg.host_dir != NULL && cfg.host_dir[0] != '\0') {
        strncpy(host_dir, cfg.host_dir, sizeof host_dir - 1);
        host_dir[sizeof(host_dir) - 1] = '\0';
    } else {
        strncpy(host_dir, drive_dir, sizeof host_dir - 1);
        host_dir[sizeof(host_dir) - 1] = '\0';
    }

    /* Create moved stop name with date. */
    {
        time_t t;
        struct tm *tmr;
        char date_buf[16];

        t = time(NULL);
        tmr = localtime(&t);
        if (tmr != NULL) {
            strftime(date_buf, sizeof date_buf, "%Y%m%d-%H%M", tmr);
        } else {
            memcpy(date_buf, "00000000-0000", 14);
        }
        if (jc_snprintf(moved_stop, sizeof moved_stop, "%s/STOP.used-%s", host_dir, date_buf) >= (int)sizeof moved_stop) {
            return 2;
        }
    }

    /* Rename STOP to moved_stop. */
    if (rename(stop_path, moved_stop) != 0) {
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "STOP is still there: %s", strerror(errno)) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        return 1;
    }

    if (jc_snprintf(line_buf, sizeof line_buf,
                    "STOP moved to %s", moved_stop) >= (int)sizeof line_buf) {
        return 2;
    }
    park_say(out_file, line_buf);

    /* Double-check STOP is gone. */
    if (access(stop_path, F_OK) == 0) {
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "NOT resuming: %s/STOP is still there", drive_dir) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        return 1;
    }

    /* Start the unit. */
    if (cfg.host_page_unit_run != NULL && cfg.host_page_unit_run[0] != '\0') {
        strncpy(unit_run_cmd, cfg.host_page_unit_run, sizeof unit_run_cmd - 1);
        unit_run_cmd[sizeof(unit_run_cmd) - 1] = '\0';
    } else {
        /* Default: systemd-run --user --unit=<U> -p WorkingDirectory=<H> sh -c "exec <H>/run-drive.sh >> <H>/drive-loop.out 2>&1" */
        if (jc_snprintf(unit_run_cmd, sizeof unit_run_cmd,
                        "systemd-run --user --unit=%s -p WorkingDirectory=%s sh -c \"exec %s/run-drive.sh >> %s/drive-loop.out 2>&1\"",
                        unit_name, host_dir, drive_dir, drive_dir) >= (int)sizeof unit_run_cmd) {
            return 2;
        }
    }

    if (park_run_cmd(unit_run_cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) != 1) {
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "the unit start failed (rc=%d)", cmd_rc) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        return 1;
    }

    if (cmd_rc != 0) {
        if (jc_snprintf(line_buf, sizeof line_buf,
                        "the unit start failed (rc=%d)", cmd_rc) >= (int)sizeof line_buf) {
            return 2;
        }
        park_say(out_file, line_buf);
        return 1;
    }

    /* Post-start sleep. */
    if (post_start > 0) {
        sleep((unsigned int)post_start);
    }

    /* Get unit state again. */
    if (cfg.host_page_unit_state != NULL && cfg.host_page_unit_state[0] != '\0') {
        if (park_run_cmd(cfg.host_page_unit_state, NULL, NULL,
                         stdout_buf, sizeof stdout_buf, &cmd_rc) == 1) {
            park_strip_nl(stdout_buf);
            strncpy(unit_state, stdout_buf, sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        } else {
            strncpy(unit_state, "unknown", sizeof unit_state - 1);
            unit_state[sizeof(unit_state) - 1] = '\0';
        }
    }

    if (jc_snprintf(line_buf, sizeof line_buf,
                    "unit %s: %s", unit_name, unit_state) >= (int)sizeof line_buf) {
        return 2;
    }
    park_say(out_file, line_buf);

    /* Record the pid. */
    if (cfg.host_page_pid_list != NULL && cfg.host_page_pid_list[0] != '\0') {
        strncpy(pid_list_cmd, cfg.host_page_pid_list, sizeof pid_list_cmd - 1);
        pid_list_cmd[sizeof(pid_list_cmd) - 1] = '\0';
    } else {
        /* Default: ps -eo pid,args grep */
        if (jc_snprintf(pid_list_cmd, sizeof pid_list_cmd,
                        "ps -eo pid,args | grep '[j]ichi-harness --config .* drive ' | awk '{print $1}'") >= (int)sizeof pid_list_cmd) {
            return 2;
        }
    }

    if (park_run_cmd(pid_list_cmd, NULL, NULL, stdout_buf, sizeof stdout_buf, &cmd_rc) != 1) {
        return 2;
    }
    park_strip_nl(stdout_buf);

    if (jc_snprintf(line_buf, sizeof line_buf,
                    "loop pid %s started", stdout_buf) >= (int)sizeof line_buf) {
        return 2;
    }
    park_say(out_file, line_buf);

    /* The pinned build line (park-resume.sh:99). */
    {
        int pinned_rc = 0;
        park_print_pinned(drive_dir, &pinned_rc);
        if (pinned_rc != 0) {
            return pinned_rc;
        }
    }

    return 0;
}
