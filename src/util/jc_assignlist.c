/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_assignlist.c - the one assignment listing (M626; see the header for why
 * it left main.c). The collect loop is byte-for-byte the M529 original except
 * that it takes (cwd, arena) instead of the app, so the TUI can call it. */
#include "jc_assignlist.h"
#include "jc_vec.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(char * const *)a, *(char * const *)b);
}

void jc_assignlist_collect(const char *cwd, struct jc_arena *a,
                           struct jc_vec *rows)
{
    struct jc_vec names;
    char dir[1100];
    char solp[1300];
    char ppath[1160];
    char *progress = NULL;
    char *hintlog = NULL;
    jc_size i;

    jc_snprintf(dir, sizeof(dir), "%s/docs/assignments", cwd);
    jc_snprintf(ppath, sizeof(ppath), "%s/.jichi/progress.jsonl", cwd);
    if (jc_read_file(ppath, &progress, NULL, a) != JC_OK) {
        progress = NULL; /* no record yet -- every status renders "-" */
    }
    /* M502: the hint log is a separate sink, so it is read separately and can
     * never be mistaken for an attempt. */
    jc_snprintf(ppath, sizeof(ppath), "%s/.jichi/hints.jsonl", cwd);
    if (jc_read_file(ppath, &hintlog, NULL, a) != JC_OK) {
        hintlog = NULL;
    }
    jc_vec_init(&names, sizeof(char *));
    jc_list_dir(dir, &names, a);
    if (names.len > 1) {
        qsort(names.data, (size_t)names.len, sizeof(char *), name_cmp);
    }
    for (i = 0; i < names.len; i++) {
        const char *nm = *(char **)jc_vec_at(&names, i);
        jc_size len = (jc_size)strlen(nm);
        struct jc_assign_row row;
        char *text = NULL;

        if (len < 4 || strcmp(nm + len - 3, ".md") != 0) {
            continue; /* not a markdown file (fixture dirs are skipped too) */
        }
        if (len >= 12 && strcmp(nm + len - 12, ".solution.md") == 0) {
            continue; /* the solution sibling is listed against its assignment */
        }
        if (strcmp(nm, "INDEX.md") == 0) {
            continue; /* the set's map, not an assignment */
        }
        memset(&row, 0, sizeof(row));
        row.name = nm;
        jc_snprintf(solp, sizeof(solp), "%s/%.*s.solution.md", dir,
                    (int)(len - 3), nm);
        row.has_sol = jc_file_exists(solp);
        jc_snprintf(solp, sizeof(solp), "%s/%s", dir, nm);
        if (jc_read_file(solp, &text, NULL, a) == JC_OK) {
            jc_assign_parse(text, &row.spec, a); /* tolerate failure */
        }
        jc_progress_scan(progress, nm, &row.prog);
        jc_progress_hints_scan(hintlog, nm, &row.hints);
        jc_vec_push(rows, &row);
    }
    jc_vec_free(&names);
}

/* The fold's key equality: two NULLs are the same (stage-less) bucket. */
static int stage_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

static void totals_add(struct jc_vec *totals, const struct jc_assign_row *r,
                       const char *key)
{
    struct jc_stage_total *t = NULL;
    jc_size i;
    for (i = 0; i < totals->len; i++) {
        struct jc_stage_total *c =
            (struct jc_stage_total *)jc_vec_at(totals, i);
        if (stage_eq(c->stage, key)) {
            t = c;
            break;
        }
    }
    if (t == NULL) {
        struct jc_stage_total fresh;
        memset(&fresh, 0, sizeof(fresh));
        fresh.stage = key;
        jc_vec_push(totals, &fresh);
        t = (struct jc_stage_total *)jc_vec_at(totals, totals->len - 1);
    }
    t->pts_avail += r->spec.points;
    t->total++;
    if (r->prog.passed) {
        t->pts_earned += r->spec.points;
        t->passed++;
    }
}

void jc_assignlist_totals(const struct jc_vec *rows, struct jc_vec *totals)
{
    jc_size i;
    int have_nostage = 0;
    for (i = 0; i < rows->len; i++) {
        const struct jc_assign_row *r =
            (const struct jc_assign_row *)jc_vec_at((struct jc_vec *)rows, i);
        if (r->spec.stage == NULL) {
            have_nostage = 1; /* folded in a second pass, so it groups LAST */
            continue;
        }
        totals_add(totals, r, r->spec.stage);
    }
    if (have_nostage) {
        for (i = 0; i < rows->len; i++) {
            const struct jc_assign_row *r =
                (const struct jc_assign_row *)
                jc_vec_at((struct jc_vec *)rows, i);
            if (r->spec.stage == NULL) {
                totals_add(totals, r, NULL);
            }
        }
    }
}

int jc_assignlist_has_stage(const struct jc_vec *rows)
{
    jc_size i;
    for (i = 0; i < rows->len; i++) {
        const struct jc_assign_row *r =
            (const struct jc_assign_row *)jc_vec_at((struct jc_vec *)rows, i);
        if (r->spec.stage != NULL) {
            return 1;
        }
    }
    return 0;
}

void jc_assignlist_stage_line(const struct jc_stage_total *t, char *buf,
                              jc_size cap)
{
    jc_snprintf(buf, cap, "-- %s  (%d/%d pts, %d/%d passed)",
                t->stage != NULL ? t->stage : "(no stage)",
                t->pts_earned, t->pts_avail, t->passed, t->total);
}

void jc_assignlist_total_line(const struct jc_vec *totals, char *buf,
                              jc_size cap)
{
    int pe = 0, pa = 0, ps = 0, n = 0;
    jc_size i;
    for (i = 0; i < totals->len; i++) {
        const struct jc_stage_total *t =
            (const struct jc_stage_total *)
            jc_vec_at((struct jc_vec *)totals, i);
        pe += t->pts_earned;
        pa += t->pts_avail;
        ps += t->passed;
        n += t->total;
    }
    jc_snprintf(buf, cap, "total: %d/%d pts, %d/%d passed", pe, pa, ps, n);
}

static void print_row(const struct jc_assign_row *r)
{
    char row[512];
    jc_progress_row(r->name, r->spec.phase, r->spec.points, r->has_sol,
                    &r->prog, row, sizeof(row));
    /* Appended rather than a column, so the pinned row layout (and the tests
     * over it) is unchanged, and a learner who pulled no hints sees nothing
     * extra. */
    if (r->hints.pulls > 0) {
        printf("  %s  hints %d (deepest rung %d)\n", row,
               r->hints.pulls, r->hints.max_rung);
    } else {
        printf("  %s\n", row);
    }
}

void jc_assignlist_print(const struct jc_vec *rows, const char *stage_filter,
                         const char *dim, const char *reset)
{
    char line[512];
    jc_size i;
    jc_size g;

    jc_progress_row_header(line, sizeof(line));
    printf("  %s%s%s\n", dim, line, reset);

    if (!jc_assignlist_has_stage(rows)) {
        /* Flat, exactly as before M626: a workspace whose specs carry no
         * stage: keys (any non-curriculum project) must not change shape. */
        for (i = 0; i < rows->len; i++) {
            print_row((const struct jc_assign_row *)
                      jc_vec_at((struct jc_vec *)rows, i));
        }
        return;
    }
    {
        struct jc_vec totals;
        jc_vec_init(&totals, sizeof(struct jc_stage_total));
        jc_assignlist_totals(rows, &totals);
        for (g = 0; g < totals.len; g++) {
            const struct jc_stage_total *t =
                (const struct jc_stage_total *)jc_vec_at(&totals, g);
            if (stage_filter != NULL &&
                (t->stage == NULL || strcmp(t->stage, stage_filter) != 0)) {
                continue;
            }
            jc_assignlist_stage_line(t, line, sizeof(line));
            printf("  %s%s%s\n", dim, line, reset);
            for (i = 0; i < rows->len; i++) {
                const struct jc_assign_row *r =
                    (const struct jc_assign_row *)
                    jc_vec_at((struct jc_vec *)rows, i);
                if (stage_eq(r->spec.stage, t->stage)) {
                    print_row(r);
                }
            }
        }
        if (stage_filter == NULL) {
            jc_assignlist_total_line(&totals, line, sizeof(line));
            printf("  %s%s%s\n", dim, line, reset);
        }
        jc_vec_free(&totals);
    }
}
