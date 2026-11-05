/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_eventlog.c - opt-in JSONL structured-event sink (see jc_eventlog.h). */

#include "jc_eventlog.h"
#include "jc_version.h"
#include "jc_utf8.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_proc.h"   /* jc_fd_cloexec (M472) */

#include <stdlib.h>
#include <string.h>
#include <limits.h>   /* LONG_MAX: M860's band clamp */

const char *jc_eventlog_path(const struct jc_eventlog *log)
{
    if (log == NULL || log->f == NULL || log->path[0] == '\0') {
        return NULL;
    }
    return log->path;
}

int jc_eventlog_level_parse(const char *s)
{
    if (s == NULL || s[0] == '\0' || strcmp(s, "off") == 0) {
        return JC_EVENTLOG_OFF;
    }
    if (strcmp(s, "metrics") == 0) {
        return JC_EVENTLOG_METRICS;
    }
    if (strcmp(s, "full") == 0) {
        return JC_EVENTLOG_FULL;
    }
    return -1;
}

const char *jc_eventlog_level_name(int level)
{
    switch (level) {
    case JC_EVENTLOG_METRICS: return "metrics";
    case JC_EVENTLOG_FULL:    return "full";
    default:                  return "off";
    }
}

void jc_eventlog_disable(struct jc_eventlog *log)
{
    if (log == NULL) {
        return;
    }
    log->f = NULL;
    log->sid[0] = '\0';
    log->workspace[0] = '\0';
    log->seq = 0;
    log->task = 0;           /* M800 */
    log->task_run[0] = '\0'; /* M800 */
    log->level = JC_EVENTLOG_OFF;
    log->owns_f = 0;
    log->path[0] = '\0'; /* M292 */
}

void jc_eventlog_set_workspace(struct jc_eventlog *log, const char *ws)
{
    if (log == NULL || log->f == NULL) {
        return;
    }
    if (ws != NULL && ws[0] != '\0') {
        jc_snprintf(log->workspace, sizeof(log->workspace), "%s", ws);
    } else {
        log->workspace[0] = '\0';
    }
}

/* Create the parent directory of `path` (mkdir -p). Best-effort; ignores
 * failure (the fopen below will report the real error). */
static void make_parent_dir(const char *path)
{
    char dir[1024];
    const char *slash = strrchr(path, '/');
    jc_size n;
    if (slash == NULL || slash == path) {
        return; /* no parent component, or root */
    }
    n = (jc_size)(slash - path);
    if (n >= sizeof(dir)) {
        return;
    }
    memcpy(dir, path, n);
    dir[n] = '\0';
    /* 0700 on what WE create; a directory the user already had is not ours to
     * re-permission (M488 -- as root this turned /tmp into 0700 root-only). */
    (void)jc_mkdir_p_private(dir);
}

jc_status jc_eventlog_open(struct jc_eventlog *log, const char *path,
                          const char *sid, int level)
{
    jc_eventlog_disable(log);
    if (log == NULL) {
        return JC_ERR_INVALID;
    }
    if (level <= JC_EVENTLOG_OFF || path == NULL || path[0] == '\0') {
        return JC_ERR_INVALID; /* stays disabled */
    }
    make_parent_dir(path);
    log->f = fopen(path, "ab");
    if (log->f == NULL) {
        return JC_ERR_IO; /* stays disabled (f == NULL) */
    }
    /* Owner-only: the full tier records prompts + tool I/O (M132). */
    jc_make_private(path);
    /* ...and not handed to a child. M132's mode is a fact about the FILE; an
     * inherited descriptor bypasses it entirely, and a model-issued shell was
     * measured holding this one writable (M472). */
    jc_fd_cloexec(fileno(log->f));
    log->owns_f = 1;
    log->level = level;
    jc_snprintf(log->path, sizeof(log->path), "%s", path); /* M292 */
    if (sid != NULL) {
        jc_snprintf(log->sid, sizeof(log->sid), "%s", sid);
    }
    return JC_OK;
}

void jc_eventlog_close(struct jc_eventlog *log)
{
    if (log == NULL) {
        return;
    }
    if (log->f != NULL && log->owns_f) {
        fclose(log->f);
    }
    jc_eventlog_disable(log);
}

cJSON *jc_eventlog_begin(struct jc_eventlog *log, const char *event)
{
    cJSON *o;
    if (log == NULL || log->f == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "v", (double)JC_EVENTLOG_SCHEMA);
    /* M290: the BUILD that produced this event, distinct from `v` (the event
     * schema). Stamped on EVERY event rather than once in a header, for the same
     * reason `ws` is: the reader filters (by workspace, by --since), logs get
     * concatenated and shared, and a header is lost by exactly the operations
     * that make the version matter. ~13 bytes/event -- 0.5% on a 34 MB log. */
    cJSON_AddStringToObject(o, "jichi", JC_VERSION);
    cJSON_AddNumberToObject(o, "ts", jc_now_seconds());
    if (log->sid[0] != '\0') {
        cJSON_AddStringToObject(o, "sid", log->sid);
    }
    if (log->workspace[0] != '\0') {
        cJSON_AddStringToObject(o, "ws", log->workspace);
    }
    cJSON_AddNumberToObject(o, "seq", (double)log->seq++);
    /* M800: attribute a spawn_parallel child's events, so a reader can tell two
     * concurrent children apart. Absent in the lead (task == 0). */
    if (log->task > 0) {
        cJSON_AddNumberToObject(o, "task", (double)log->task);
        if (log->task_run[0] != '\0') {
            cJSON_AddStringToObject(o, "task_run", log->task_run);
        }
    }
    cJSON_AddStringToObject(o, "event", event != NULL ? event : "");
    return o;
}

void jc_eventlog_set_task(struct jc_eventlog *log, long task)
{
    if (log == NULL) {
        return;
    }
    log->task = task;
    if (task > 0) {
        if (log->sid[0] != '\0') {
            jc_snprintf(log->task_run, sizeof(log->task_run), "%s-t%ld",
                        log->sid, task);
        } else {
            jc_snprintf(log->task_run, sizeof(log->task_run), "t%ld", task);
        }
        /* Band this child's seq away from the lead's and its siblings'. */
        log->seq = task * JC_EVENTLOG_CHILD_SEQ_STRIDE;
    } else {
        log->task_run[0] = '\0';
    }
}

void jc_eventlog_set_task_run(struct jc_eventlog *log, const char *run,
                              long task, long band)
{
    long maxband = LONG_MAX / JC_EVENTLOG_CHILD_SEQ_STRIDE - 1;

    if (log == NULL) {
        return;
    }
    log->task = task;
    if (task <= 0) {
        log->task_run[0] = '\0';
        return;
    }
    if (run != NULL && run[0] != '\0') {
        jc_snprintf(log->task_run, sizeof(log->task_run), "%s-t%ld", run, task);
    } else if (log->sid[0] != '\0') {
        jc_snprintf(log->task_run, sizeof(log->task_run), "%s-t%ld",
                    log->sid, task);
    } else {
        jc_snprintf(log->task_run, sizeof(log->task_run), "t%ld", task);
    }
    if (band < 1) {
        band = task;
    }
    if (band > maxband) {
        band = maxband;
    }
    log->seq = band * JC_EVENTLOG_CHILD_SEQ_STRIDE;
}

void jc_eventlog_end(struct jc_eventlog *log, cJSON *o)
{
    if (o == NULL) {
        return;
    }
    if (log != NULL && log->f != NULL) {
        char *line = cJSON_PrintUnformatted(o);
        if (line != NULL) {
            /* M889 (the demos drive's N51): the sink repairs what a producer cut
             * mid-character -- a byte-counted `%.70s` in a tool's result reached
             * this line unchanged, and a strict reader rejected it. Catch it here
             * because a promise about every producer covers only the code already
             * written (ANECDOTES #22, and `dup_content` for messages). A JSON line
             * holds non-ASCII bytes only inside strings, so a U+FFFD in place of
             * a stray byte keeps it valid JSON. */
            char *fixed = NULL;
            if (jc_utf8_sanitize(line, (jc_size)strlen(line), &fixed, NULL)) {
                free(line);
                line = fixed;
            }
            fputs(line, log->f);
            fputc('\n', log->f);
            fflush(log->f);
            free(line);
        }
    }
    cJSON_Delete(o);
}

int jc_eventlog_full(const struct jc_eventlog *log)
{
    return log != NULL && log->f != NULL && log->level >= JC_EVENTLOG_FULL;
}

void jc_eventlog_add_text(cJSON *o, const char *key, const char *s, jc_size max)
{
    jc_size len;
    char *buf;
    char *red = NULL;
    if (o == NULL || key == NULL || s == NULL) {
        return;
    }
    /* Scrub any registered secret before it lands on disk (M132). The full tier
     * records raw prompts + tool I/O, which may echo a key; "***" is never
     * longer than the secret, so strlen(s)+1 always fits the redacted copy.
     * M1137 (C-19): via the shared helper, failing CLOSED -- if the scrub
     * cannot allocate, the key is DROPPED (the same degrade as the truncation
     * leg's own malloc failure below, by class), never written unredacted. */
    if (jc_redact_active()) {
        red = jc_redact_scrub(s, (jc_size)strlen(s));
        if (red == NULL) {
            return;
        }
        s = red;
    }
    len = (jc_size)strlen(s);
    if (max == 0 || len <= max) {
        cJSON_AddStringToObject(o, key, s);
        free(red);
        return;
    }
    /* Truncate to at most `max` bytes, backing off a split UTF-8 sequence so the
     * emitted JSON string stays valid UTF-8. This local idiom was generalised
     * into jc_utf8_trunc_len at M191, when the same cut on a model-bound string
     * (mid-turn elision) turned out to wedge a whole run. */
    len = jc_utf8_trunc_len(s, max);
    buf = (char *)malloc(len + 24);
    if (buf == NULL) {
        free(red);
        return;
    }
    memcpy(buf, s, len);
    jc_snprintf(buf + len, 24, "...[+%lu B]",
                (unsigned long)((jc_size)strlen(s) - len));
    cJSON_AddStringToObject(o, key, buf);
    free(buf);
    free(red);
}
