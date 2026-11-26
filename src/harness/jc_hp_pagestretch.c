/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_pagestretch.c - the model-failure stretch (b1-b5 of notes/M1213.md,
 * M1213 S1, batch H12; SV:0433's option (a)). Productized translation of the
 * lead's C89 oracle docs/internal/drive/2026-10-27-hardening-host/
 * page_stretch_oracle.c (NOT product code; proven 9/9 against
 * page_stretch.py, run 0101 LOG). The LOGIC is byte-identical; the housekeeping
 * differences: every snprintf becomes jc_snprintf (identical %d/%g/%.0f/%s
 * bytes except the three time fields use ps_round0 + %lu because jc_snprintf's
 * %.0f truncates where the reference rounds half-to-even (the sttb 105 vs 106
 * tooth, M1213 S1-b)), the dynamic call_t list + grows becomes arena allocation
 * (the oracle's 512-qualifying-name bound is kept), every cJSON_Parse is
 * matched by a cJSON_Delete, nothing is written to stdout/stderr, and the one
 * line goes to line_out (cut-to-cap semantics per the header). */
#include "jc_hp_pagestretch.h"

#include <dirent.h>   /* opendir / readdir / closedir (the runs set) */
#include <stdio.h>    /* FILE / fopen / fgets / fclose */
#include <string.h>   /* strlen / strcmp / strcpy (arena name copy) */
#include <sys/stat.h> /* stat / S_ISREG (the isfile skip) */

#include "cJSON.h"      /* jichi's own cJSON (b5; the API-compatible original) */
#include "jc_mem.h"     /* jc_arena_alloc */
#include "jc_snprintf.h" /* jc_snprintf, never snprintf */

/* The reference's '%.0f' rounds HALF-TO-EVEN (50.5 -> 50, 105.5 -> 106).
 * jc_snprintf's %f TRUNCATES (its render_double keeps the floor and the
 * digits), so the three time fields are rounded here and emitted via %lu.
 * Non-negative inputs only (the stretch's times are all >= 0); out-of-domain values are clamped, not refused: a negative time (reachable as mins when latency_ms exceeds 1000*ts) prints 0, a time above 4294967295 prints that cap; the reference would print the value -- the contract pins neither arm (review finding 2, the same audit commits). */
static unsigned long
ps_round0(double d)
{
    unsigned long ip, r;
    double frac;
    if (d != d) { return 0; }          /* NaN (cannot occur: ts is numeric) */
    if (d < 0.0) { return 0; }
    if (d > 4294967295.0) { return 4294967295UL; }
    ip = (unsigned long)d;             /* floor for d >= 0 */
    frac = d - (double)ip;
    r = ip + 1;
    if (frac < 0.5) { r = ip; }
    else if (frac == 0.5 && (ip % 2UL) == 0UL) { r = ip; } /* half to even */
    return r;
}

#define PS_MAX_RUNS 512 /* the oracle's qualifying-name bound */
#define PS_MAX_CALLS 512 /* a bound, not the reference: the oracle reallocs unbounded; at most 512 counting model_call lines are kept, beyond that the line is computed over the first 512 (review finding 1 of the S1/S1-b audit, commits 92787ae64/25a178283) */

typedef struct { double ts, sent; char cls[64]; } ps_call_t;

/* A 4-digit name (b1's "len == 4 and every character '0'-'9'"). */
static int ps_is4digit(const char *n)
{
    int i;
    if (strlen(n) != 4) return 0;
    for (i = 0; i < 4; i++)
        if (n[i] < '0' || n[i] > '9') return 0;
    return 1;
}

/* The reference's "%s" % value for one cls member (b2): None when the member
 * is absent or JSON-null; the integer's digits via %d when a JSON number that
 * is an integral value; %g when a JSON number that is not; the string as-is
 * when a JSON string. The None/None form is KEPT, not cleaned (b5's note). */
static void ps_member_str(const cJSON *m, char *out, size_t cap)
{
    if (m == NULL || cJSON_IsNull(m)) {
        jc_snprintf(out, cap, "None");
    } else if (cJSON_IsNumber(m)) {
        double v = m->valuedouble;
        if (v == (double)(int)v)
            jc_snprintf(out, cap, "%d", (int)v);
        else
            jc_snprintf(out, cap, "%g", v);
    } else if (cJSON_IsString(m)) {
        jc_snprintf(out, cap, "%s", m->valuestring);
    } else {
        jc_snprintf(out, cap, "None");
    }
}

int jc_pagestretch_line(const char *drive_dir, struct jc_arena *a,
                        char *line_out, size_t cap)
{
    char *runsdir;
    DIR *d;
    struct dirent *e;
    char (*names)[16];
    int nn = 0, nruns, i, q;
    char runs[2][16];
    ps_call_t *calls;
    int ncall = 0;
    double last_ok = 0.0;
    ps_call_t *fails;
    int nf = 0;
    int rc;
    const char *lastrun;

    if (drive_dir == NULL || a == NULL || line_out == NULL) return -1;

    calls = (ps_call_t *)jc_arena_alloc(a, (jc_size)PS_MAX_CALLS
                                       * sizeof(ps_call_t));
    fails = (ps_call_t *)jc_arena_alloc(a, (jc_size)PS_MAX_CALLS
                                       * sizeof(ps_call_t));
    names = (char (*)[16])jc_arena_alloc(a, (jc_size)PS_MAX_RUNS
                                        * sizeof(names[0]));
    if (calls == NULL || fails == NULL || names == NULL) return -1;

    /* b1: the runs set -- the LAST TWO 4-digit names of <drive>/runs, sorted. */
    runsdir = (char *)jc_arena_alloc(a, strlen(drive_dir) + 8);
    if (runsdir == NULL) return -1;
    jc_snprintf(runsdir, strlen(drive_dir) + 8, "%s/runs", drive_dir);
    nruns = 0;
    d = opendir(runsdir);
    if (d != NULL) {
        nn = 0;
        while ((e = readdir(d)) != NULL && nn < PS_MAX_RUNS) {
            if (ps_is4digit(e->d_name)) {
                jc_snprintf(names[nn], 16, "%s", e->d_name);
                nn++;
            }
        }
        for (i = 0; i + 1 < nn; i++)
            for (q = 0; q + 1 < nn - i; q++)
                if (strcmp(names[q], names[q + 1]) > 0) {
                    char t[16];
                    memcpy(t, names[q], 16);
                    memcpy(names[q], names[q + 1], 16);
                    memcpy(names[q + 1], t, 16);
                }
        nruns = nn < 2 ? nn : 2;
        for (i = 0; i < nruns; i++)
            jc_snprintf(runs[i], 16, "%s", names[nn - nruns + i]);
        closedir(d);
    }

    /* b2: the model_call lines of the two runs (missing / non-regular files
     * skipped, os.path.isfile; a non-numeric ts skipped). */
    for (i = 0; i < nruns; i++) {
        char f[1024 + 16];
        FILE *fp;
        char line[8192];
        struct stat st;
        cJSON *ev;
        cJSON *ts;

        jc_snprintf(f, sizeof f, "%s/runs/%s/telemetry.jsonl",
                    drive_dir, runs[i]);
        if (stat(f, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        fp = fopen(f, "rb");
        if (fp == NULL) continue;
        while (fgets(line, (int)sizeof line, fp) != NULL) {
            cJSON *j = cJSON_Parse(line);
            if (j == NULL) continue;
            ev = cJSON_GetObjectItem(j, "event");
            ts = cJSON_GetObjectItem(j, "ts");
            if (!cJSON_IsString(ev)
                    || strcmp(ev->valuestring, "model_call") != 0
                    || !cJSON_IsNumber(ts)) {
                cJSON_Delete(j);
                continue;
            }
            {
                double lat = 0.0;
                cJSON *lm;
                cJSON *okj;
                cJSON *stj;
                cJSON *fcj;
                char cls[64];
                char sv[32], fv[32];
                if (ncall < PS_MAX_CALLS) {
                    lm = cJSON_GetObjectItem(j, "latency_ms");
                    okj = cJSON_GetObjectItem(j, "ok");
                    stj = cJSON_GetObjectItem(j, "status");
                    fcj = cJSON_GetObjectItem(j, "fail_class");
                    if (cJSON_IsNumber(lm)) lat = lm->valuedouble;
                    if (cJSON_IsTrue(okj))
                        jc_snprintf(cls, sizeof cls, "ok");
                    else {
                        ps_member_str(stj, sv, sizeof sv);
                        ps_member_str(fcj, fv, sizeof fv);
                        jc_snprintf(cls, sizeof cls, "%s/%s", sv, fv);
                    }
                    calls[ncall].ts = ts->valuedouble;
                    calls[ncall].sent = ts->valuedouble - lat / 1000.0;
                    jc_snprintf(calls[ncall].cls, sizeof calls[ncall].cls,
                                "%s", cls);
                    ncall++;
                }
            }
            cJSON_Delete(j);
        }
        fclose(fp);
    }

    /* b3: a STABLE sort of the combined list by (ts, sent, cls) -- the
     * oracle's bubble sort on the three keys; equal keys keep input order.
     * (qsort is NOT stable, so it is not used.) */
    for (i = 0; i + 1 < ncall; i++)
        for (q = 0; q + 1 < ncall - i; q++) {
            int s = 0;
            if (calls[q].ts < calls[q + 1].ts) s = -1;
            else if (calls[q].ts > calls[q + 1].ts) s = 1;
            else if (calls[q].sent < calls[q + 1].sent) s = -1;
            else if (calls[q].sent > calls[q + 1].sent) s = 1;
            else s = strcmp(calls[q].cls, calls[q + 1].cls);
            if (s > 0) {
                ps_call_t t = calls[q];
                calls[q] = calls[q + 1];
                calls[q + 1] = t;
            }
        }

    /* last_ok = the LARGEST ts of any ok call (0 when there is none). */
    for (i = 0; i < ncall; i++)
        if (strcmp(calls[i].cls, "ok") == 0) last_ok = calls[i].ts;

    /* b3: the fails -- not-ok calls with ts > last_ok (in sorted order). */
    for (i = 0; i < ncall; i++)
        if (strcmp(calls[i].cls, "ok") != 0 && calls[i].ts > last_ok)
            fails[nf++] = calls[i];

    lastrun = nruns != 0 ? runs[nruns - 1] : "-";

    /* b4: the ONE stretch line. */
    if (nf == 0) {
        rc = jc_snprintf(line_out, cap, "0 %lu 0 0 0 - %s\n",
                         ps_round0(last_ok), lastrun);
    } else {
        char **seen = NULL;
        int *cnt = NULL, *ord = NULL, ns = 0, k, j;
        double mins, maxt;
        char cls[2048];
        int p = 0;
        /* Dynamic allocation for the class set -- the reference's Counter is unbounded. */
        seen = (char **)jc_arena_alloc(a, (jc_size)nf * sizeof(char *));
        cnt = (int *)jc_arena_alloc(a, (jc_size)nf * sizeof(int));
        ord = (int *)jc_arena_alloc(a, (jc_size)nf * sizeof(int));
        if (seen == NULL || cnt == NULL || ord == NULL) {
            return -1;
        }
        for (k = 0; k < nf; k++) {
            seen[k] = (char *)jc_arena_alloc(a, (jc_size)64);
            if (seen[k] == NULL) {
                return -1;
            }
        }
        for (i = 0; i < nf; i++) {
            k = -1;
            for (j = 0; j < ns; j++)
                if (strcmp(seen[j], fails[i].cls) == 0) { k = j; break; }
            if (k < 0) {
                jc_snprintf(seen[ns], 64, "%s", fails[i].cls);
                cnt[ns] = 0;
                k = ns++;
            }
            cnt[k]++;
        }
        for (i = 0; i < ns; i++) ord[i] = i;
        /* most common first; on equal counts FIRST-SEEN order (b5's
         * Counter.most_common tie rule) -- a stable bubble sort on count. */
        for (i = 0; i + 1 < ns; i++)
            for (j = 0; j + 1 < ns - i; j++)
                if (cnt[ord[j + 1]] > cnt[ord[j]]) {
                    int t = ord[j];
                    ord[j] = ord[j + 1];
                    ord[j + 1] = t;
                }
        mins = fails[0].sent;
        maxt = fails[0].ts;
        for (i = 1; i < nf; i++) {
            if (fails[i].sent < mins) mins = fails[i].sent;
            if (fails[i].ts > maxt) maxt = fails[i].ts;
        }
        for (i = 0; i < ns; i++) {
            int r;
            if (p >= (int)sizeof cls) {
                return -1;
            }
            r = jc_snprintf(cls + p, (size_t)(sizeof cls - (size_t)p),
                            "%s%s:%d", i ? "," : "", seen[ord[i]],
                            cnt[ord[i]]);
            if (r < 0 || (int)(sizeof cls - (size_t)p) <= r) {
                return -1;
            }
            p += r;
        }
        rc = jc_snprintf(line_out, cap,
                         "1 %lu %lu %lu %d %s %s\n",
                         ps_round0(last_ok), ps_round0(mins), ps_round0(maxt), nf, cls, lastrun);
    }
    return (rc < 0 || (size_t)rc >= cap) ? -1 : 0;
}
