/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_checkin.c - the C89 port of checkin.sh (M1224, H13, the supervisor's
 * check-in of persistent timers, transient loop, current run's NAMED records, push
 * state, drive LOG's READY/WITHDRAWN lines, network prerequisite, outage watcher,
 * and supervision log's two copies). */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>

#include "jc_mem.h"
#include "jc_log.h"
#include "jc_snprintf.h"
#include "jc_proc.h"
#include "jc_harness_config.h"
#include "jc_harness_checkin.h"
#include "jc_hp_checkinred.h"

/* Forward declarations - stamp_of defined after get_timestamp to avoid reordering */
static void stamp_of(char *out, size_t cap, time_t t);

/* Run a command via popen and capture every line (for multi-line output). */
static int
run_cmd_get_all(const char *cmd, void (*emit)(const char *line, void *ctx), void *ctx);

/* Get the current date/time in the format expected by the driver. */
static void
get_timestamp(char *out, size_t cap)
{
    time_t now;

    now = time(NULL);
    stamp_of(out, cap, now);
}

/* Run a command via popen and capture output, stripping trailing newlines.
 * Redirects stderr to /dev/null to suppress command-not-found errors. */
static int
run_cmd_get_line(const char *cmd, char *out, size_t cap)
{
    FILE *f;
    char buf[4096];
    char cmd_with_stderr[8192];
    size_t len;

    if (cmd == NULL || cmd[0] == '\0') {
        out[0] = '\0';
        return 0;
    }

    /* Append 2>/dev/null to suppress stderr (e.g., "command not found") */
    jc_snprintf(cmd_with_stderr, sizeof(cmd_with_stderr), "%s 2>/dev/null", cmd);

    f = jc_proc_popen(cmd_with_stderr, "r");
    if (f == NULL) {
        out[0] = '\0';
        return 0;
    }

    if (fgets(buf, sizeof(buf), f) == NULL) {
        out[0] = '\0';
        jc_proc_pclose(f);
        return 0;
    }

    /* Copy to output */
    len = strlen(buf);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, buf, len);
    out[len] = '\0';

    /* Strip trailing newlines */
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = '\0';
    }

    jc_proc_pclose(f);
    return 0;
}

/* Run a command via popen, read every line, strip trailing newline/CR, call emit. */
static int
run_cmd_get_all(const char *cmd, void (*emit)(const char *line, void *ctx), void *ctx)
{
    FILE *f;
    char cmd_with_stderr[8192];
    char line[8192];
    size_t len;

    if (cmd == NULL || cmd[0] == '\0' || emit == NULL) {
        return -1;
    }

    jc_snprintf(cmd_with_stderr, sizeof(cmd_with_stderr), "%s 2>/dev/null", cmd);

    f = jc_proc_popen(cmd_with_stderr, "r");
    if (f == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        /* Strip trailing newline and CR */
        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        emit(line, ctx);
    }

    jc_proc_pclose(f);
    return 0;
}

/* Emit callback for follow section: redact line and print to stdout. */
static void
follow_emit(const char *line, void *ctx)
{
    char *out = (char *)ctx;
    if (jc_checkinred(line, out, 8192) == 0) {
        fputs(out, stdout);
        fputc('\n', stdout);
    }
}

/* Count lines in a file containing a pattern. */
static int
count_matching_lines(const char *path, const char *pattern)
{
    FILE *f;
    char line[4096];
    int count = 0;

    if (path == NULL || path[0] == '\0' || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strstr(line, pattern) != NULL) {
            count++;
        }
    }

    fclose(f);
    return count;
}

/* Get the last modification time of a file as a string (HH:MM:SS). */
static int
get_mtime_time(const char *path, char *out, size_t cap)
{
    struct stat st;
    time_t mtime;

    if (path == NULL || path[0] == '\0') {
        out[0] = '\0';
        return 0;
    }

    if (stat(path, &st) != 0) {
        out[0] = '\0';
        return 0;
    }

    mtime = st.st_mtime;
    strftime(out, cap, "%H:%M:%S", localtime(&mtime));
    return 0;
}

/* The checkin verb body. Implements all 37 cells. */
int
jc_harness_checkin(int argc, char *const *argv, struct jc_harness_config *cfg,
                   struct jc_arena *a)
{
    int push_flag = 0;
    int loop_unit_flag = 0;
    int once_unit_flag = 0;
    const char *loop_unit_argv = NULL;
    const char *once_unit_argv = NULL;
    int i;
    int rc;
    char loop_state[1024];
    char once_state[1024];
    char loop_cmd[2048];
    char once_cmd[2048];
    const char *once_unit;
    char podman_cmd[256];
    char podman_out[8192];
    const char *container_prefix;
    int found_container;
    char line_buf[1024];
    char *saveptr;
    char *tok;
    char stop_path[1024];
    struct stat st;
    char restart_loop_out[1024];
    char restart_state[1024];
    char restart_cmd[2048];
    char restart_last[4096];
    FILE *restart_f;
    char buf[4096];
    char pid_list_out[8192];
    char *line;
    char *exe_marker;
    char exe_target[1024];
    char expected_prefix[256];
    char harness_log_path[1024];
    FILE *harness_log;
    char **lines;
    int line_count;
    int line_capacity;
    size_t len;
    int start_line;
    char runs_path[1024];
    int current_run;
    DIR *runs_dir;
    struct dirent *de;
    int is_digits;
    int j;
    char run_path[1024];
    char meta_path[1024];
    FILE *meta_file;
    char line_buf2[4096];
    char telemetry_path[1024];
    int model_calls;
    char stream_path[1024];
    char stream_mtime[64];
    char redacted[8192];
    char inbox_path[1024];
    DIR *inbox_dir;
    struct dirent *de2;
    int first;
    char notes_path[1024];
    DIR *notes_dir;
    struct dirent *de3;
    char push_state[1024];
    char push_next[1024];
    char push_last_tick[1024];
    char push_unit;
    char push_timer_cmd[2048];
    char next_cmd[2048];
    char push_tick_path[1024];
    FILE *push_tick_f;
    char *last_line;
    size_t last_len;
    char **page_lines;
    int page_count;
    int start;
    char push_log_path[1024];
    char push_log_last[1024];
    FILE *push_log_f;
    char *last;
    char base[64];
    char follow_cmd[8200];
    char git_cmd[2048];
    FILE *git_f;
    char git_out[256];
    char branch[256];
    char log_cmd[2048];
    FILE *log_f;
    char log_line[2048];
    int count;
    char grep_cmd[4096];
    FILE *f2;
    char line2[1024];
    char vpn_out[1024];
    char vpn0_out[1024];
    char route_dev[64];
    char openconnect_out[1024];
    char route_out[1024];
    char *dev_pos;
    char *end;
    char page_state[1024];
    char page_last[1024];
    char page_watch_unit;
    char page_cmd[2048];
    char page_watch_path[1024];
    FILE *page_watch_f2;
    char *page_last2;
    char **page_lines2;
    int page_count2;
    char h_supervision_path[1024];
    char d_supervision_path[1024];
    struct stat h_st;
    struct stat d_st;
    int h_exists;
    int d_exists;
    char h_buf[4096];
    char d_buf[4096];
    FILE *h_f;
    FILE *d_f;
    int identical;
    size_t h_n;
    size_t d_n;

    /* Additional C89-compliant declarations - must be at top of block */
    const char *loop_unit;
    char timestamp[64];
    char proc_path[64];
    ssize_t exelen;
    const char *config_path;

    /* (void)a; - parameter is kept for signature compatibility */
    (void)a;

    /* Parse command line and extract --config path for later config loading.
     * Also parse checkin's flags (--push, --loop-unit <u>, --once-unit <o>). */
    config_path = NULL;
    for (i = 1; i < argc; i++) {
        /* Skip program name (first iteration, but argv[1] might be --config) */
        
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
                return 2;
            }
            config_path = argv[++i]; /* store path for config loading */
            continue;
        }
        if (strcmp(argv[i], "checkin") == 0) {
            /* Command name - skip */
            continue;
        }
        if (strcmp(argv[i], "--push") == 0) {
            push_flag = 1;
        } else if (strcmp(argv[i], "--loop-unit") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
                return 2;
            }
            loop_unit_flag = 1;
            loop_unit_argv = argv[++i];
        } else if (strcmp(argv[i], "--once-unit") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
                return 2;
            }
            once_unit_flag = 1;
            once_unit_argv = argv[++i];
        } else if (strcmp(argv[i], "--") == 0) {
            fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
            return 2;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
            return 2;
        } else {
            /* Positional after command is not allowed */
            fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
            return 2;
        }
    }

    /* C2b: loopUnit required (absent = refusal, rc 1, empty stdout, exact stderr line)
     * Try --loop-unit flag first, then cfg->drive_loop_unit */
    loop_unit = NULL;
    if (loop_unit_flag && loop_unit_argv != NULL && loop_unit_argv[0] != '\0') {
        loop_unit = loop_unit_argv;
    }
    /* If config_path is provided, load config and get loop_unit from it */
    if (config_path != NULL && config_path[0] != '\0') {
        memset(cfg, 0, sizeof(*cfg));
        if (jc_harness_config_load(config_path, cfg, a) != JC_OK) {
            fprintf(stderr, "checkin: the harness config could not be loaded: %s\n",
                    jc_harness_config_error(cfg));
            return 2;
        }
        /* If loop_unit wasn't set from --loop-unit, try from config */
        if (loop_unit == NULL || loop_unit[0] == '\0') {
            loop_unit = cfg->drive_loop_unit;
        }
    }
    
    if (loop_unit == NULL || loop_unit[0] == '\0') {
        /* If --config was not provided, this is a usage error */
        int config_provided = 0;
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--config") == 0) {
                config_provided = 1;
                break;
            }
        }
        if (!config_provided) {
            fprintf(stderr, "usage: jichi-harness --config <path> checkin [--push] [--loop-unit <u>] [--once-unit <o>]\n");
            return 2;
        }
        /* --config was provided but loop_unit is empty (C2b).
         * Refuse before the header: rc 1, empty stdout, exact stderr line. */
        fprintf(stderr, "checkin: the loop unit's name is not in the config (drive.loopUnit) or on the command line (--loop-unit); refused, printed nothing else\n");
        return 1;
    }

    /* C1: header */
    get_timestamp(timestamp, sizeof(timestamp));
    printf("== %s\n", timestamp);

    /* C2: loop unit and once unit states */
    /* Loop unit state command - only if loop_unit is set */
    if (loop_unit != NULL && loop_unit[0] != '\0') {
        if (cfg->host_unit_state != NULL && cfg->host_unit_state[0] != '\0') {
            strncpy(loop_cmd, cfg->host_unit_state, sizeof(loop_cmd) - 1);
            loop_cmd[sizeof(loop_cmd) - 1] = '\0';
        } else {
            jc_snprintf(loop_cmd, sizeof(loop_cmd), "systemctl --user is-active %s", loop_unit);
        }
        run_cmd_get_line(loop_cmd, loop_state, sizeof(loop_state));
        printf("%s: %s\n", loop_unit, loop_state);
    }

    /* Determine once unit: command line overrides config, else derived from loop */
    once_unit = cfg->drive_once_unit;
    if (once_unit_flag && once_unit_argv != NULL && once_unit_argv[0] != '\0') {
        once_unit = once_unit_argv;
    } else if (once_unit == NULL || once_unit[0] == '\0') {
        if (loop_unit != NULL && loop_unit[0] != '\0') {
            static char derived_once[256];
            jc_snprintf(derived_once, sizeof(derived_once), "%s-once", loop_unit);
            once_unit = derived_once;
        }
    }

    if (once_unit != NULL && once_unit[0] != '\0') {
        if (cfg->host_unit_state != NULL && cfg->host_unit_state[0] != '\0') {
            strncpy(once_cmd, cfg->host_unit_state, sizeof(once_cmd) - 1);
            once_cmd[sizeof(once_cmd) - 1] = '\0';
        } else {
            jc_snprintf(once_cmd, sizeof(once_cmd), "systemctl --user is-active %s", once_unit);
        }
        run_cmd_get_line(once_cmd, once_state, sizeof(once_state));
        printf("%s: %s\n", once_unit, once_state);
    }

    /* C3: container - run podman ps --format '{{.Names}} {{.Status}}' */
    jc_snprintf(podman_cmd, sizeof(podman_cmd), "podman ps --format '{{.Names}} {{.Status}}' 2>/dev/null");

    run_cmd_get_line(podman_cmd, podman_out, sizeof(podman_out));

    container_prefix = cfg->drive_container_name;
    /* C3: no fallback when container_prefix is NULL or empty */

    found_container = 0;
    if (container_prefix != NULL && container_prefix[0] != '\0') {
        saveptr = NULL;
        tok = strtok_r(podman_out, "\n", &saveptr);
        while (tok != NULL) {
            if (strncmp(tok, container_prefix, strlen(container_prefix)) == 0) {
                jc_snprintf(line_buf, sizeof(line_buf), "%s\n", tok);
                fputs(line_buf, stdout);
                found_container = 1;
            }
            tok = strtok_r(NULL, "\n", &saveptr);
        }

        if (!found_container) {
            printf("no %s- container\n", container_prefix);
        }
    }

    /* C4: STOP file */
    jc_snprintf(stop_path, sizeof(stop_path), "%s/STOP", cfg->dir);
    if (stat(stop_path, &st) == 0) {
        printf("STOP file present\n");
    }

    /* C5: restart-loop line - only when drive.restart_loop_unit set AND .out file exists */
    if (cfg->drive_restart_loop_unit != NULL && cfg->drive_restart_loop_unit[0] != '\0') {
        jc_snprintf(restart_loop_out, sizeof(restart_loop_out), "%s/restart-loop.out", cfg->host_push_home);
        if (access(restart_loop_out, F_OK) == 0) {
            if (cfg->host_unit_state != NULL && cfg->host_unit_state[0] != '\0') {
                strncpy(restart_cmd, cfg->host_unit_state, sizeof(restart_cmd) - 1);
                restart_cmd[sizeof(restart_cmd) - 1] = '\0';
            } else {
                jc_snprintf(restart_cmd, sizeof(restart_cmd), "systemctl --user is-active %s", cfg->drive_restart_loop_unit);
            }
            run_cmd_get_line(restart_cmd, restart_state, sizeof(restart_state));
            printf("%s: %s; ", cfg->drive_restart_loop_unit, restart_state);
            /* tail -1 restart-loop.out */
            restart_f = fopen(restart_loop_out, "rb");
            if (restart_f != NULL) {
                while (fgets(buf, sizeof(buf), restart_f) != NULL) {
                    len = strlen(buf);
                    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                        buf[--len] = '\0';
                    }
                    strncpy(restart_last, buf, sizeof(restart_last) - 1);
                    restart_last[sizeof(restart_last) - 1] = '\0';
                }
                fclose(restart_f);
            }
            printf("%s\n", restart_last);
        }
    }

    /* C6: loop pid line - parse host.checkin_pid_list output */
    if (cfg->host_checkin_pid_list != NULL && cfg->host_checkin_pid_list[0] != '\0') {
        run_cmd_get_line(cfg->host_checkin_pid_list, pid_list_out, sizeof(pid_list_out));
        /* Output format: "loop pid <pid> started <date>; exe -> <path>" */
        /* Only print when exe starts with <dir>/bin/jichi-harness */
        saveptr = NULL;
        line = strtok_r(pid_list_out, "\n", &saveptr);
        while (line != NULL) {
            /* Parse: loop pid 4242 started Mon Nov 23 07:00:00 2026; exe -> <path> */
            if (strncmp(line, "loop pid ", 9) == 0) {
                char *pid_str = line + 9;
                char *end1 = strchr(pid_str, ' ');
                if (end1 != NULL) {
                    long pid;
                    *end1 = '\0';
                    pid = strtol(pid_str, NULL, 10);
                    *end1 = ' ';
                    
                    /* Find "exe -> " */
                    exe_marker = strstr(line, "exe -> ");
                    if (exe_marker != NULL) {
                        /* Read /proc/<pid>/exe directly */
                        jc_snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exe", pid);
                        exelen = readlink(proc_path, exe_target, sizeof(exe_target) - 1);
                        if (exelen >= 0) {
                            exe_target[exelen] = '\0';
                            /* Check if it starts with <dir>/bin/jichi-harness */
                            jc_snprintf(expected_prefix, sizeof(expected_prefix), "%s/bin/jichi-harness", cfg->dir);
                            if (strncmp(exe_target, expected_prefix, strlen(expected_prefix)) == 0) {
                                printf("loop pid %ld exe -> %s\n", pid, exe_target);
                            }
                        }
                    }
                }
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    }

    /* C7: harness.log tail - last 6 lines */
    jc_snprintf(harness_log_path, sizeof(harness_log_path), "%s/harness.log", cfg->dir);
    harness_log = fopen(harness_log_path, "rb");
    if (harness_log != NULL) {
        printf("-- harness.log (tail)\n");
        /* Read all lines and keep last 6 */
        lines = NULL;
        line_count = 0;
        line_capacity = 0;

        while (fgets(buf, sizeof(buf), harness_log) != NULL) {
            if (line_count >= line_capacity) {
                line_capacity = line_capacity == 0 ? 8 : line_capacity * 2;
                lines = (char **)realloc(lines, line_capacity * sizeof(char *));
                if (lines == NULL) {
                    fclose(harness_log);
                    for (i = 0; i < line_count; i++) free(lines[i]);
                    free(lines);
                    return 1;
                }
            }
            len = strlen(buf);
            lines[line_count] = (char *)malloc(len + 1);
            if (lines[line_count] == NULL) {
                fclose(harness_log);
                for (i = 0; i < line_count; i++) free(lines[i]);
                free(lines);
                return 1;
            }
            memcpy(lines[line_count], buf, len + 1);  /* includes NUL */
            line_count++;
        }
        fclose(harness_log);

        /* Print last 6 lines (or fewer if less than 6 lines) */
        start_line = line_count - 6;
        if (start_line < 0) start_line = 0;
        
        for (i = start_line; i < line_count; i++) {
            if (jc_checkinred(lines[i], redacted, sizeof(redacted)) == 0) {
                fputs(redacted, stdout);
            }
            free(lines[i]);
        }
        /* Free the unprinted prefix lines (L3 leak fix) */
        for (i = 0; i < start_line; i++) {
            free(lines[i]);
        }
        free(lines);
    } else {
        /* File absent: print header only */
        printf("-- harness.log (tail)\n");
    }

    /* C9: current run selection - numerically-greatest digit-only entry */
    jc_snprintf(runs_path, sizeof(runs_path), "%s/runs", cfg->dir);
    current_run = -1;
    runs_dir = opendir(runs_path);
    if (runs_dir != NULL) {
        while ((de = readdir(runs_dir)) != NULL) {
            if (de->d_name[0] == '.') continue;
            /* Check if it's all digits */
            is_digits = 1;
            for (j = 0; de->d_name[j] != '\0'; j++) {
                if (de->d_name[j] < '0' || de->d_name[j] > '9') {
                    is_digits = 0;
                    break;
                }
            }
            if (is_digits) {
                int num = atoi(de->d_name);
                if (num > current_run) {
                    current_run = num;
                }
            }
        }
        closedir(runs_dir);
    }

    if (current_run >= 0) {
        jc_snprintf(run_path, sizeof(run_path), "%s/runs/%04d", cfg->dir, current_run);
        printf("-- run %04d\n", current_run);
    } else {
        /* C9 no-runs arm: the reference's ${n:-0} -- R points at runs/0000 so the
         * reads below fail softly (missing meta/telemetry/stream/notes), and the
         * line still prints -- run 0000. run_path must be set here or the reads
         * use an uninitialised stack buffer. */
        jc_snprintf(run_path, sizeof(run_path), "%s/runs/%04d", cfg->dir, 0);
        printf("-- run 0000\n");
        current_run = 0;
    }

    /* C10: run meta.txt */
    jc_snprintf(meta_path, sizeof(meta_path), "%s/meta.txt", run_path);
    meta_file = fopen(meta_path, "rb");
    if (meta_file != NULL) {
        while (fgets(line_buf2, sizeof(line_buf2), meta_file) != NULL) {
            if (jc_checkinred(line_buf2, redacted, sizeof(redacted)) == 0) {
                fputs(redacted, stdout);
            }
        }
        fclose(meta_file);
    }

    /* C11: model calls count and stream last written */
    jc_snprintf(telemetry_path, sizeof(telemetry_path), "%s/telemetry.jsonl", run_path);
    model_calls = count_matching_lines(telemetry_path, "\"event\":\"model_call\"");

    jc_snprintf(stream_path, sizeof(stream_path), "%s/stream.jsonl", run_path);
    get_mtime_time(stream_path, stream_mtime, sizeof(stream_mtime));

    printf("model calls: %d, stream last written %s\n", model_calls, stream_mtime);

    /* C12: follow command - multi-line output with two per-run arguments */
    if (cfg->host_checkin_follow != NULL && cfg->host_checkin_follow[0] != '\0') {
        /* Compose command: base + <run_path>/stream.jsonl 6 */
        jc_snprintf(follow_cmd, sizeof(follow_cmd), "%s %s/stream.jsonl 6", cfg->host_checkin_follow, run_path);
        run_cmd_get_all(follow_cmd, follow_emit, redacted);
    }

    /* C13: inbox and notes */
    jc_snprintf(inbox_path, sizeof(inbox_path), "%s/inbox", cfg->dir);
    printf("-- inbox: ");
    inbox_dir = opendir(inbox_path);
    if (inbox_dir != NULL) {
        first = 1;
        while ((de2 = readdir(inbox_dir)) != NULL) {
            if (de2->d_name[0] == '.') continue;
            if (!first) printf(" ");
            printf("%s", de2->d_name);
            first = 0;
        }
        closedir(inbox_dir);
    }
    printf("\n");

    printf("-- notes of this run: ");
    if (current_run >= 0) {
        jc_snprintf(notes_path, sizeof(notes_path), "%s/notes", run_path);
        notes_dir = opendir(notes_path);
        if (notes_dir != NULL) {
            first = 1;
            while ((de3 = readdir(notes_dir)) != NULL) {
                if (de3->d_name[0] == '.') continue;
                if (!first) printf(" ");
                printf("%s", de3->d_name);
                first = 0;
            }
            closedir(notes_dir);
        }
    }
    printf("\n");

    /* C14: push timer */
    /* Form: -- push timer: <state>, next <next>; last tick: <last_tick> */
    push_unit = (cfg->drive_push_unit != NULL && cfg->drive_push_unit[0] != '\0');
    
    if (push_unit) {
        /* State and next from unitState */
        if (cfg->host_unit_state != NULL && cfg->host_unit_state[0] != '\0') {
            strncpy(push_timer_cmd, cfg->host_unit_state, sizeof(push_timer_cmd) - 1);
            push_timer_cmd[sizeof(push_timer_cmd) - 1] = '\0';
        } else {
            jc_snprintf(push_timer_cmd, sizeof(push_timer_cmd), "systemctl --user is-active %s", cfg->drive_push_unit);
        }
        run_cmd_get_line(push_timer_cmd, push_state, sizeof(push_state));
        
        /* Next from systemctl show */
        jc_snprintf(next_cmd, sizeof(next_cmd), "systemctl --user show %s -p NextElapseUSecRealtime --value", cfg->drive_push_unit);
        run_cmd_get_line(next_cmd, push_next, sizeof(push_next));
    } else {
        push_state[0] = '\0';
        push_next[0] = '\0';
    }
    
    /* Last tick from push-tick.out (cut to 160) */
    jc_snprintf(push_tick_path, sizeof(push_tick_path), "%s/push-tick.out", cfg->host_push_home);
    push_last_tick[0] = '\0';
    push_tick_f = fopen(push_tick_path, "rb");
    if (push_tick_f != NULL) {
        last_line = NULL;
        last_len = 0;
        while (fgets(buf, sizeof(buf), push_tick_f) != NULL) {
            len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                buf[--len] = '\0';
            }
            if (last_line != NULL) free(last_line);
            last_line = (char *)malloc(len + 1);
            memcpy(last_line, buf, len + 1);  /* includes NUL */
            last_len = len;
        }
        fclose(push_tick_f);
        
        if (last_line != NULL) {
            /* Cut to 160 chars */
            if (last_len > 160) {
                last_line[160] = '\0';
            }
            strncpy(push_last_tick, last_line, sizeof(push_last_tick) - 1);
            push_last_tick[sizeof(push_last_tick) - 1] = '\0';
            free(last_line);
        }
    }
    
    printf("-- push timer: %s, next %s; last tick: %s\n", push_state, push_next, push_last_tick);

    /* C15: PAGE lines from push-tick.out (tail -2, indent 3) */
    push_tick_f = fopen(push_tick_path, "rb");
    if (push_tick_f != NULL) {
        page_lines = NULL;
        page_count = 0;

        while (fgets(buf, sizeof(buf), push_tick_f) != NULL) {
            if (strstr(buf, "PAGE:") != NULL) {
                if (page_lines == NULL) {
                    page_lines = (char **)malloc(sizeof(char *));
                } else {
                    page_lines = (char **)realloc(page_lines, (page_count + 1) * sizeof(char *));
                }
                if (page_lines == NULL) {
                    fclose(push_tick_f);
                    for (i = 0; i < page_count; i++) free(page_lines[i]);
                    free(page_lines);
                    return 1;
                }
                len = strlen(buf);
                page_lines[page_count] = (char *)malloc(len + 1);
                memcpy(page_lines[page_count], buf, len + 1);  /* includes NUL */
                page_count++;
            }
        }
        fclose(push_tick_f);
        
        /* Print tail -2 (last 2 lines), indent 3 */
        start = page_count - 2;
        if (start < 0) start = 0;
        
        for (i = start; i < page_count; i++) {
            len = strlen(page_lines[i]);
            /* Cut to 200 chars, preserving newline if present */
            if (len > 200 && (page_lines[i][len - 1] == '\n' || page_lines[i][len - 1] == '\r')) {
                /* Keep newline, NUL at position 201 */
                page_lines[i][200] = page_lines[i][len - 1];
                page_lines[i][201] = '\0';
                len = 201;
            } else if (len > 200) {
                page_lines[i][200] = '\0';
                len = 200;
            }
            printf("   %s", page_lines[i]);
            free(page_lines[i]);
        }
        /* Free the unprinted prefix lines (L1 leak fix) */
        for (i = 0; i < start; i++) {
            free(page_lines[i]);
        }
        free(page_lines);
    }

    /* C16: --push arm */
    if (push_flag) {
        if (cfg->host_checkin_push != NULL && cfg->host_checkin_push[0] != '\0') {
            /* Run the push command, capture exit status */
            rc = system(cfg->host_checkin_push);
            /* Get exit status properly */
            if (WIFEXITED(rc)) {
                rc = WEXITSTATUS(rc);
            } else {
                rc = 127;
            }
            
            /* tail -1 push.log for parens */
            jc_snprintf(push_log_path, sizeof(push_log_path), "%s/push.log", cfg->host_push_home);
            push_log_last[0] = '\0';
            push_log_f = fopen(push_log_path, "rb");
            if (push_log_f != NULL) {
                last = NULL;
                while (fgets(buf, sizeof(buf), push_log_f) != NULL) {
                    len = strlen(buf);
                    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                        buf[--len] = '\0';
                    }
                    if (last != NULL) free(last);
                    last = (char *)malloc(len + 1);
                    memcpy(last, buf, len + 1);  /* includes NUL */
                }
                fclose(push_log_f);
                if (last != NULL) {
                    strncpy(push_log_last, last, sizeof(push_log_last) - 1);
                    push_log_last[sizeof(push_log_last) - 1] = '\0';
                    free(last);
                }
            }
            
            printf("-- push.sh rc=%d (%s)\n", rc, push_log_last);
        }
    }

    /* C17: team commits (prints only when host_push_repo AND drive_branch_prefix are set and non-empty) */
    if (cfg->host_push_repo != NULL && cfg->drive_branch_prefix != NULL && cfg->drive_branch_prefix[0] != '\0') {
        base[0] = '\0';
        
        /* Get refs/remotes/origin/master via popen */
        jc_snprintf(git_cmd, sizeof(git_cmd), "git -C %s rev-parse --short refs/remotes/origin/master 2>/dev/null", cfg->host_push_repo);
        git_f = jc_proc_popen(git_cmd, "r");
        if (git_f != NULL) {
            if (fgets(git_out, sizeof(git_out), git_f) != NULL) {
                /* Strip trailing newlines */
                size_t len = strlen(git_out);
                while (len > 0 && (git_out[len - 1] == '\n' || git_out[len - 1] == '\r')) {
                    git_out[--len] = '\0';
                }
                strncpy(base, git_out, sizeof(base) - 1);
                base[sizeof(base) - 1] = '\0';
            }
            jc_proc_pclose(git_f);
        }
        
        /* Header with base */
        if (base[0] == '\0') {
            printf("-- the team's commits since master  (push.git's origin/master, as push.sh fetched it)\n");
        } else {
            printf("-- the team's commits since master %s (push.git's origin/master, as push.sh fetched it)\n", base);
        }
        
        /* Log from base to branch - branch is exactly drive_branch_prefix + "/main" */
        strncpy(branch, cfg->drive_branch_prefix, sizeof(branch) - 1);
        branch[sizeof(branch) - 1] = '\0';
        strncat(branch, "/main", sizeof(branch) - 1);
        branch[sizeof(branch) - 1] = '\0';
        
        jc_snprintf(log_cmd, sizeof(log_cmd), "git -C %s log --format='%%h %%an <%%ae> %%s' %s..refs/heads/%s 2>/dev/null", 
                   cfg->host_push_repo, base, branch);
        
        log_f = jc_proc_popen(log_cmd, "r");
        if (log_f != NULL) {
            count = 0;
            while (fgets(log_line, sizeof(log_line), log_f) != NULL && count < 20) {
                /* Cut to 200 chars, preserving newline if present */
                len = strlen(log_line);
                if (len > 200 && (log_line[len - 1] == '\n' || log_line[len - 1] == '\r')) {
                    /* Keep newline, NUL at position 201 */
                    log_line[200] = log_line[len - 1];
                    log_line[201] = '\0';
                    len = 201;
                } else if (len > 200) {
                    log_line[200] = '\0';
                    len = 200;
                }
                printf("%s", log_line);
                count++;
            }
            jc_proc_pclose(log_f);
        }
    }

    /* C18: READY lines */
    if (cfg->drive_log_path != NULL && cfg->drive_log_path[0] != '\0') {
        /* Pattern: the note C18 union (reference arms + the structured READY line; first two alternatives anchored: an in-line READY is not a READY line) */
        /* Use grep -n to get line numbers */
        jc_snprintf(grep_cmd, sizeof(grep_cmd), "grep -n -E '^BATCH .* READY at |^READY |READY at [0-9a-f]{7}|WITHDRAWN|superseded, 20' %s | tail -8 | cut -c1-200", cfg->drive_log_path);
        
        f2 = jc_proc_popen(grep_cmd, "r");
        if (f2 != NULL) {
            while (fgets(line2, sizeof(line2), f2) != NULL) {
                printf("%s", line2);
            }
            jc_proc_pclose(f2);
        }
    }

    /* C19: vpn line */
    vpn_out[0] = '\0';
    vpn0_out[0] = '\0';
    route_dev[0] = '\0';
    openconnect_out[0] = '\0';
    
    if (cfg->host_checkin_vpn != NULL && cfg->host_checkin_vpn[0] != '\0') {
        run_cmd_get_line(cfg->host_checkin_vpn, vpn_out, sizeof(vpn_out));
    }
    if (cfg->host_checkin_vpn0 != NULL && cfg->host_checkin_vpn0[0] != '\0') {
        run_cmd_get_line(cfg->host_checkin_vpn0, vpn0_out, sizeof(vpn0_out));
    }
    
    /* Extract device from route command */
    if (cfg->host_checkin_route != NULL && cfg->host_checkin_route[0] != '\0') {
        run_cmd_get_line(cfg->host_checkin_route, route_out, sizeof(route_out));
        /* Look for "dev <device>" in output */
        dev_pos = strstr(route_out, "dev ");
        if (dev_pos != NULL) {
            dev_pos += 4;
            end = strchr(dev_pos, ' ');
            if (end != NULL) {
                size_t len = end - dev_pos;
                if (len >= sizeof(route_dev)) len = sizeof(route_dev) - 1;
                strncpy(route_dev, dev_pos, len);
                route_dev[len] = '\0';
            }
        }
    }
    
    if (cfg->host_checkin_openconnect != NULL && cfg->host_checkin_openconnect[0] != '\0') {
        run_cmd_get_line(cfg->host_checkin_openconnect, openconnect_out, sizeof(openconnect_out));
    }
    
    printf("-- vpn: %s; vpn0 %s; route to gateway: %s; openconnect pids: %s\n",
           vpn_out, vpn0_out, route_dev, openconnect_out);

    /* C20: page-watch line */
    /* Form: -- page-watch: <state>; last: <last_line> */
    page_watch_unit = (cfg->drive_page_watch_unit != NULL && cfg->drive_page_watch_unit[0] != '\0');
    
    if (page_watch_unit) {
        if (cfg->host_unit_state != NULL && cfg->host_unit_state[0] != '\0') {
            strncpy(page_cmd, cfg->host_unit_state, sizeof(page_cmd) - 1);
            page_cmd[sizeof(page_cmd) - 1] = '\0';
        } else {
            jc_snprintf(page_cmd, sizeof(page_cmd), "systemctl --user is-active %s", cfg->drive_page_watch_unit);
        }
        run_cmd_get_line(page_cmd, page_state, sizeof(page_state));
    } else {
        page_state[0] = '\0';
    }
    
    page_last[0] = '\0';
    jc_snprintf(page_watch_path, sizeof(page_watch_path), "%s/page-watch.out", cfg->host_push_home);
    page_watch_f2 = fopen(page_watch_path, "rb");
    if (page_watch_f2 != NULL) {
        page_last2 = NULL;
        while (fgets(buf, sizeof(buf), page_watch_f2) != NULL) {
            len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
                buf[--len] = '\0';
            }
            if (page_last2 != NULL) free(page_last2);
            page_last2 = (char *)malloc(len + 1);
            memcpy(page_last2, buf, len + 1);  /* includes NUL */
        }
        fclose(page_watch_f2);
        if (page_last2 != NULL) {
            /* Cut to 170 chars */
            len = strlen(page_last2);
            if (len > 170) {
                page_last2[170] = '\0';
            }
            strncpy(page_last, page_last2, sizeof(page_last) - 1);
            page_last[sizeof(page_last) - 1] = '\0';
            free(page_last2);
        }
    }
    
    printf("-- page-watch: %s; last: %s\n", page_state, page_last);

    /* C20b: PAGE lines from page-watch.out (tail -2, indent 3) */
    page_watch_f2 = fopen(page_watch_path, "rb");
    if (page_watch_f2 != NULL) {
        page_lines2 = NULL;
        page_count2 = 0;
        
        while (fgets(buf, sizeof(buf), page_watch_f2) != NULL) {
            if (strstr(buf, "PAGE:") != NULL) {
                if (page_lines2 == NULL) {
                    page_lines2 = (char **)malloc(sizeof(char *));
                } else {
                    page_lines2 = (char **)realloc(page_lines2, (page_count2 + 1) * sizeof(char *));
                }
                if (page_lines2 == NULL) {
                    fclose(page_watch_f2);
                    for (i = 0; i < page_count2; i++) free(page_lines2[i]);
                    free(page_lines2);
                    return 1;
                }
                len = strlen(buf);
                page_lines2[page_count2] = (char *)malloc(len + 1);
                memcpy(page_lines2[page_count2], buf, len + 1);  /* includes NUL */
                page_count2++;
            }
        }
        fclose(page_watch_f2);
        
        /* Print tail -2 (last 2 lines), indent 3 */
        start = page_count2 - 2;
        if (start < 0) start = 0;
        
        for (i = start; i < page_count2; i++) {
            len = strlen(page_lines2[i]);
            /* Cut to 200 chars, preserving newline if present */
            if (len > 200 && (page_lines2[i][len - 1] == '\n' || page_lines2[i][len - 1] == '\r')) {
                /* Keep newline, NUL at position 201 */
                page_lines2[i][200] = page_lines2[i][len - 1];
                page_lines2[i][201] = '\0';
                len = 201;
            } else if (len > 200) {
                page_lines2[i][200] = '\0';
                len = 200;
            }
            printf("   %s", page_lines2[i]);
            free(page_lines2[i]);
        }
        /* Free the unprinted prefix lines (L2 leak fix) */
        for (i = 0; i < start; i++) {
            free(page_lines2[i]);
        }
        free(page_lines2);
    }

    /* C21: supervision log comparison */
    if (cfg->host_dir != NULL && cfg->dir != NULL) {
        jc_snprintf(h_supervision_path, sizeof(h_supervision_path), "%s/supervision-log.md", cfg->host_dir);
        jc_snprintf(d_supervision_path, sizeof(d_supervision_path), "%s/supervision-log.md", cfg->dir);

        h_exists = (stat(h_supervision_path, &h_st) == 0);
        d_exists = (stat(d_supervision_path, &d_st) == 0);

        if (h_exists && d_exists) {
            /* Compare byte-for-byte */
            h_f = fopen(h_supervision_path, "rb");
            d_f = fopen(d_supervision_path, "rb");

            if (h_f != NULL && d_f != NULL) {
                identical = 1;
                while (!feof(h_f) && !feof(d_f)) {
                    h_n = fread(h_buf, 1, sizeof(h_buf), h_f);
                    d_n = fread(d_buf, 1, sizeof(d_buf), d_f);
                    if (h_n != d_n || memcmp(h_buf, d_buf, h_n) != 0) {
                        identical = 0;
                        break;
                    }
                }
                fclose(h_f);
                fclose(d_f);

                if (identical) {
                    printf("supervision log: copies identical\n");
                } else {
                    printf("supervision log: THE COPY IN THE DRIVE DIR DIFFERS\n");
                }
            } else {
                if (h_f) fclose(h_f);
                if (d_f) fclose(d_f);
                printf("supervision log: THE COPY IN THE DRIVE DIR DIFFERS\n");
            }
        } else if (h_exists) {
            printf("supervision log: drive missing\n");
        } else if (d_exists) {
            printf("supervision log: host missing\n");
        }
        /* Both missing: nothing printed */
    }

    /* C22: writes nothing (we're reading only, no writes) */

    return 0;
}

/* =============================================================================
 * Helpers duplicated from jc_harness_drive.c (stamp_of, utc_from_tm)
 * These provide C89-safe timestamp formatting without %F/%T/%z.
 * =============================================================================
 */

/* timegm (glibc's UTC mktime), but the build's _POSIX_C_SOURCE does not
 * expose it, so this hand-rolls it the way the note names (CHANGED 4: no
 * locale, no date child). The day count is the standard proleptic-Gregorian
 * days-from-civil algorithm (era / year-of-era / day-of-year); it is checked
 * against 1970-01-01 = 0 and 2000-01-01 = 10957 (the century leap). */
static time_t
utc_from_tm(const struct tm *tmv)
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

/* A given epoch as "YYYY-MM-DD HH:MM:SS +HHMM" (behaviour 14's log prefix,
 * the `date '+%F %T %z'` equivalent without a date child): strftime for the
 * wall clock, then the LOCAL offset = utc_of(wall) - t, where utc_of is the
 * file's own hand-rolled timegm, utc_from_tm (glibc's timegm is not exposed
 * under the build's global _POSIX_C_SOURCE): local = UTC + off, so reading
 * the local wall clock as UTC lands off SECONDS from t. Verified against
 * date(1) at UTC, -0400 (America/New_York) and +1400 (Pacific/Kiritimati).
 * A negative offset (west of UTC) formats "-HHMM". */
static void
stamp_of(char *out, size_t cap, time_t t)
{
    struct tm tmv;
    struct tm tmc;
    long off;
    long mag;
    int oh, om;
    size_t n;

    if (localtime_r(&t, &tmv) == NULL) {
        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = 70; tmv.tm_mon = 0; tmv.tm_mday = 1;
        tmv.tm_hour = 0; tmv.tm_min = 0; tmv.tm_sec = 0;
        off = 0;
    } else {
        tmc = tmv;
        off = (long)utc_from_tm(&tmc) - (long)t;
    }
    n = strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tmv);
    if (n == 0) {
        out[0] = '\0';
        return;
    }
    mag = (off < 0) ? -off : off;
    oh = (int)(mag / 3600);
    om = (int)((mag % 3600) / 60);
    /* date(1) '+%F %T %z' prints the offset with a LEADING SPACE
     * ("2026-10-27 00:33:29 +0000"); match it. */
    if (oh < 100 && cap > n + 7) {
        out[n] = ' ';
        out[n + 1] = (off < 0) ? '-' : '+';
        out[n + 2] = (char)('0' + oh / 10);
        out[n + 3] = (char)('0' + oh % 10);
        out[n + 4] = (char)('0' + om / 10);
        out[n + 5] = (char)('0' + om % 10);
        out[n + 6] = '\0';
    }
}
