/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_progress.c - the learner's progress record: scan / row / append (C5,
 * M174). See jc_progress.h. The scan and row helpers are pure; append is the
 * one place that knows the line format `grade --record` introduced (M173b). */

#include "jc_progress.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *jc_progress_base(const char *p)
{
    const char *s;

    if (p == NULL) {
        return NULL;
    }
    s = strrchr(p, '/');
    return (s != NULL) ? s + 1 : p;
}

/* Fold one record line (already NUL-terminated) into the standing. */
static void scan_line(const char *line, const char *want_base,
                      struct jc_progress *out)
{
    cJSON *o;
    cJSON *spec;
    cJSON *passed;
    cJSON *pct;

    o = cJSON_Parse(line);
    if (o == NULL) {
        return; /* the learner owns the file; tolerate hand edits */
    }
    spec = cJSON_GetObjectItem(o, "spec");
    if (spec == NULL || spec->valuestring == NULL ||
        strcmp(jc_progress_base(spec->valuestring), want_base) != 0) {
        cJSON_Delete(o);
        return;
    }
    out->attempts++;
    passed = cJSON_GetObjectItem(o, "passed");
    if (passed != NULL && cJSON_IsTrue(passed)) {
        out->passed = 1;
    }
    pct = cJSON_GetObjectItem(o, "pct");
    if (pct != NULL && cJSON_IsNumber(pct) &&
        (int)pct->valuedouble > out->best_pct) {
        out->best_pct = (int)pct->valuedouble;
    }
    cJSON_Delete(o);
}

void jc_progress_scan(const char *jsonl, const char *spec_name,
                      struct jc_progress *out)
{
    const char *p;
    const char *base;

    memset(out, 0, sizeof(*out));
    base = jc_progress_base(spec_name);
    if (jsonl == NULL || base == NULL || base[0] == '\0') {
        return;
    }
    p = jsonl;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size len = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (len > 0 && len < 4096) {
            char *buf = (char *)malloc(len + 1);
            if (buf != NULL) {
                memcpy(buf, p, len);
                buf[len] = '\0';
                scan_line(buf, base, out);
                free(buf);
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
}

void jc_progress_row(const char *name, const char *phase, int points,
                     int has_solution, const struct jc_progress *prog,
                     char *buf, jc_size cap)
{
    char pts[16];
    char status[48];

    if (points > 0) {
        jc_snprintf(pts, sizeof(pts), "%dpt", points);
    } else {
        jc_snprintf(pts, sizeof(pts), "-");
    }
    if (prog != NULL && prog->passed) {
        jc_snprintf(status, sizeof(status), "passed");
    } else if (prog != NULL && prog->attempts > 0) {
        jc_snprintf(status, sizeof(status), "attempted (best %d%%)",
                    prog->best_pct);
    } else {
        jc_snprintf(status, sizeof(status), "-");
    }
    jc_snprintf(buf, cap, "%-34s %-15s %5s  %-21s%s",
                name != NULL ? name : "?",
                (phase != NULL && phase[0] != '\0') ? phase : "-",
                pts, status, has_solution ? " (+solution)" : "");
    /* Strip the pad after the last column so rows carry no trailing blanks. */
    {
        jc_size n = (jc_size)strlen(buf);
        while (n > 0 && buf[n - 1] == ' ') {
            buf[--n] = '\0';
        }
    }
}

void jc_progress_row_header(char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%-34s %-15s %5s  %s",
                "assignment", "phase", "pts", "status");
}

jc_status jc_progress_append(const char *dir, const char *spec, int passed,
                             int pct, int tests_run, int tests_failed,
                             int hints)
{
    char sub[1100];
    char path[1160];
    cJSON *o;
    char *line;
    FILE *f;

    if (dir == NULL || spec == NULL) {
        return JC_ERR_INVALID;
    }
    jc_snprintf(sub, sizeof(sub), "%s/.jichi", dir);
    jc_mkdir_p(sub);
    jc_snprintf(path, sizeof(path), "%s/progress.jsonl", sub);

    o = cJSON_CreateObject();
    if (o == NULL) {
        return JC_ERR_OOM;
    }
    cJSON_AddNumberToObject(o, "ts", (double)(long)time(NULL));
    cJSON_AddStringToObject(o, "spec", spec);
    cJSON_AddBoolToObject(o, "passed", passed ? 1 : 0);
    cJSON_AddNumberToObject(o, "pct", (double)pct);
    cJSON_AddNumberToObject(o, "tests_run", (double)tests_run);
    cJSON_AddNumberToObject(o, "tests_failed", (double)tests_failed);
    if (hints >= 0) {
        cJSON_AddNumberToObject(o, "hints", (double)hints);
    }
    line = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (line == NULL) {
        return JC_ERR_OOM;
    }
    f = fopen(path, "a");
    if (f == NULL) {
        free(line);
        return JC_ERR_IO;
    }
    fprintf(f, "%s\n", line);
    fclose(f);
    free(line);
    return JC_OK;
}

/* --- M502: the hint log ---------------------------------------------------- */

jc_status jc_progress_hint_append(const char *dir, const char *spec, int rung)
{
    char sub[1100];
    char path[1160];
    cJSON *o;
    char *line;
    FILE *f;

    if (dir == NULL || spec == NULL || rung <= 0) {
        return JC_ERR_INVALID;
    }
    jc_snprintf(sub, sizeof(sub), "%s/.jichi", dir);
    jc_mkdir_p(sub);
    jc_snprintf(path, sizeof(path), "%s/hints.jsonl", sub);

    o = cJSON_CreateObject();
    if (o == NULL) {
        return JC_ERR_OOM;
    }
    cJSON_AddNumberToObject(o, "ts", (double)(long)time(NULL));
    cJSON_AddStringToObject(o, "spec", spec);
    cJSON_AddNumberToObject(o, "rung", (double)rung);
    line = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (line == NULL) {
        return JC_ERR_OOM;
    }
    f = fopen(path, "a");
    if (f == NULL) {
        free(line);
        return JC_ERR_IO;
    }
    fprintf(f, "%s\n", line);
    free(line);
    fclose(f);
    return JC_OK;
}

static void scan_hint_line(const char *line, const char *want_base,
                           struct jc_hints *out)
{
    cJSON *o;
    cJSON *spec;
    cJSON *rung;

    o = cJSON_Parse(line);
    if (o == NULL) {
        return;                 /* the learner owns the file; tolerate edits */
    }
    spec = cJSON_GetObjectItem(o, "spec");
    if (spec == NULL || spec->valuestring == NULL ||
        strcmp(jc_progress_base(spec->valuestring), want_base) != 0) {
        cJSON_Delete(o);
        return;
    }
    out->pulls++;
    rung = cJSON_GetObjectItem(o, "rung");
    if (rung != NULL && cJSON_IsNumber(rung) &&
        (int)rung->valuedouble > out->max_rung) {
        out->max_rung = (int)rung->valuedouble;
    }
    cJSON_Delete(o);
}

void jc_progress_hints_scan(const char *jsonl, const char *spec_name,
                            struct jc_hints *out)
{
    const char *p;
    const char *base;

    memset(out, 0, sizeof(*out));
    base = jc_progress_base(spec_name);
    if (jsonl == NULL || base == NULL || base[0] == '\0') {
        return;
    }
    p = jsonl;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size len = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (len > 0 && len < 4096) {
            char *buf = (char *)malloc(len + 1);
            if (buf != NULL) {
                memcpy(buf, p, len);
                buf[len] = '\0';
                scan_hint_line(buf, base, out);
                free(buf);
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
}
