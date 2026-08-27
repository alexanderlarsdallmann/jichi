/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_diff.c - line-level unified-diff rendering (see jc_diff.h). */

#include "jc_diff.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#define D_RED "\x1b[31m"
#define D_GRN "\x1b[32m"
#define D_CYN "\x1b[36m"
#define D_RST "\x1b[0m"

/* Largest LCS table (in cells) we will allocate before falling back. */
#define DIFF_LCS_CELL_CAP 4000000L
#define DIFF_DEFAULT_MAX_LINES 400

enum { OP_EQ, OP_DEL, OP_ADD };

struct line { const char *p; jc_size len; };
struct op { int tag; const char *p; jc_size len; int oldno; int newno; };

static int lines_equal(const struct line *a, const struct line *b)
{
    return a->len == b->len && memcmp(a->p, b->p, a->len) == 0;
}

/* Split `t` into lines (excluding the '\n'); a trailing newline does not create
 * a final empty line. Returns a malloc'd array (NULL when 0 lines). */
static struct line *split_lines(const char *t, int *n_out)
{
    int n = 0;
    const char *p;
    struct line *arr;

    if (t == NULL || t[0] == '\0') {
        *n_out = 0;
        return NULL;
    }
    for (p = t; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        n++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
        if (*p == '\0') {
            break;
        }
    }
    arr = (struct line *)malloc((size_t)n * sizeof(*arr));
    if (arr == NULL) {
        *n_out = 0;
        return NULL;
    }
    n = 0;
    for (p = t; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        arr[n].p = p;
        arr[n].len = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);
        n++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
        if (*p == '\0') {
            break;
        }
    }
    *n_out = n;
    return arr;
}

/* Append one diff line (prefix + text + newline), colored by tag. */
static void emit_line(struct jc_sb *sb, int color, char prefix,
                      const char *text, jc_size len)
{
    if (color) {
        jc_sb_append(sb, prefix == '+' ? D_GRN : prefix == '-' ? D_RED : "");
    }
    jc_sb_append_n(sb, &prefix, 1);
    jc_sb_append_n(sb, text, len);
    if (color && prefix != ' ') {
        jc_sb_append(sb, D_RST);
    }
    jc_sb_append_n(sb, "\n", 1);
}

/* Fill `ops` (capacity ops_cap) with the LCS edit script of the middle
 * old[p..p+mm) vs new[p..p+nn). Returns the op count. */
static int lcs_middle(const struct line *old, const struct line *new_,
                      int p, int mm, int nn, struct op *ops, int base)
{
    int *dp;
    int i, j, k;
    long cells = (long)(mm + 1) * (long)(nn + 1);

    /* Trivial sides, or a middle too large for the DP: no real LCS needed. */
    if (mm == 0 || nn == 0 || cells > DIFF_LCS_CELL_CAP) {
        k = base;
        for (i = 0; i < mm; i++) {
            ops[k].tag = OP_DEL; ops[k].p = old[p + i].p;
            ops[k].len = old[p + i].len; k++;
        }
        for (j = 0; j < nn; j++) {
            ops[k].tag = OP_ADD; ops[k].p = new_[p + j].p;
            ops[k].len = new_[p + j].len; k++;
        }
        return k - base;
    }

    dp = (int *)malloc((size_t)cells * sizeof(int));
    if (dp == NULL) {
        return lcs_middle(old, new_, p, 0, mm + nn, ops, base); /* degrade */
    }
    for (i = mm; i >= 0; i--) {
        for (j = nn; j >= 0; j--) {
            if (i == mm || j == nn) {
                dp[i * (nn + 1) + j] = 0;
            } else if (lines_equal(&old[p + i], &new_[p + j])) {
                dp[i * (nn + 1) + j] = dp[(i + 1) * (nn + 1) + (j + 1)] + 1;
            } else {
                int a = dp[(i + 1) * (nn + 1) + j];
                int b = dp[i * (nn + 1) + (j + 1)];
                dp[i * (nn + 1) + j] = a > b ? a : b;
            }
        }
    }
    i = 0; j = 0; k = base;
    while (i < mm && j < nn) {
        if (lines_equal(&old[p + i], &new_[p + j])) {
            ops[k].tag = OP_EQ; ops[k].p = old[p + i].p;
            ops[k].len = old[p + i].len; k++; i++; j++;
        } else if (dp[(i + 1) * (nn + 1) + j] >= dp[i * (nn + 1) + (j + 1)]) {
            ops[k].tag = OP_DEL; ops[k].p = old[p + i].p;
            ops[k].len = old[p + i].len; k++; i++;
        } else {
            ops[k].tag = OP_ADD; ops[k].p = new_[p + j].p;
            ops[k].len = new_[p + j].len; k++; j++;
        }
    }
    while (i < mm) {
        ops[k].tag = OP_DEL; ops[k].p = old[p + i].p;
        ops[k].len = old[p + i].len; k++; i++;
    }
    while (j < nn) {
        ops[k].tag = OP_ADD; ops[k].p = new_[p + j].p;
        ops[k].len = new_[p + j].len; k++; j++;
    }
    free(dp);
    return k - base;
}

int jc_diff_unified(const char *old_text, const char *new_text,
                    int context, int color, int max_out_lines,
                    struct jc_sb *sb)
{
    int om, nm;
    struct line *old = split_lines(old_text, &om);
    struct line *new_ = split_lines(new_text, &nm);
    struct op *ops;
    char *keep;
    int nops = 0;
    int p = 0, s = 0, mm, nn;
    int i, oldn, newn, changed = 0, emitted = 0, truncated = 0;

    if (context < 0) {
        context = 3;
    }
    if (max_out_lines <= 0) {
        max_out_lines = DIFF_DEFAULT_MAX_LINES;
    }

    /* Common prefix / suffix of equal lines. */
    while (p < om && p < nm && lines_equal(&old[p], &new_[p])) {
        p++;
    }
    while (s < om - p && s < nm - p &&
           lines_equal(&old[om - 1 - s], &new_[nm - 1 - s])) {
        s++;
    }
    mm = om - p - s;
    nn = nm - p - s;

    ops = (struct op *)malloc((size_t)(om + nm + 1) * sizeof(*ops));
    if (ops == NULL) {
        free(old); free(new_);
        return 0;
    }

    for (i = 0; i < p; i++) {
        ops[nops].tag = OP_EQ; ops[nops].p = old[i].p;
        ops[nops].len = old[i].len; nops++;
    }
    nops += lcs_middle(old, new_, p, mm, nn, ops, nops);
    for (i = 0; i < s; i++) {
        ops[nops].tag = OP_EQ; ops[nops].p = old[om - s + i].p;
        ops[nops].len = old[om - s + i].len; nops++;
    }

    /* Assign 1-based old/new line numbers and count changes. */
    oldn = 1; newn = 1;
    for (i = 0; i < nops; i++) {
        ops[i].oldno = oldn;
        ops[i].newno = newn;
        if (ops[i].tag == OP_EQ) { oldn++; newn++; }
        else if (ops[i].tag == OP_DEL) { oldn++; changed++; }
        else { newn++; changed++; }
    }
    if (changed == 0) {
        free(ops); free(old); free(new_);
        return 0;
    }

    /* Mark ops to keep: changes and `context` equal lines around them. */
    keep = (char *)calloc((size_t)nops, 1);
    if (keep == NULL) {
        free(ops); free(old); free(new_);
        return 0;
    }
    for (i = 0; i < nops; i++) {
        if (ops[i].tag != OP_EQ) {
            int lo = i - context < 0 ? 0 : i - context;
            int hi = i + context >= nops ? nops - 1 : i + context;
            int k;
            for (k = lo; k <= hi; k++) {
                keep[k] = 1;
            }
        }
    }

    /* Emit hunks: maximal runs of kept ops. */
    i = 0;
    while (i < nops) {
        int a, b, oc, nc, os, ns_;
        int k;
        if (!keep[i]) { i++; continue; }
        a = i;
        while (i < nops && keep[i]) { i++; }
        b = i;
        oc = 0; nc = 0;
        for (k = a; k < b; k++) {
            if (ops[k].tag != OP_ADD) { oc++; }
            if (ops[k].tag != OP_DEL) { nc++; }
        }
        os = oc > 0 ? ops[a].oldno : ops[a].oldno - 1;
        ns_ = nc > 0 ? ops[a].newno : ops[a].newno - 1;
        if (os < 0) { os = 0; }
        if (ns_ < 0) { ns_ = 0; }
        if (emitted + 1 > max_out_lines) { truncated = 1; break; }
        if (color) { jc_sb_append(sb, D_CYN); }
        jc_sb_append_fmt(sb, "@@ -%d,%d +%d,%d @@", os, oc, ns_, nc);
        if (color) { jc_sb_append(sb, D_RST); }
        jc_sb_append_n(sb, "\n", 1);
        emitted++;
        for (k = a; k < b; k++) {
            char pre = ops[k].tag == OP_DEL ? '-' :
                       ops[k].tag == OP_ADD ? '+' : ' ';
            if (emitted + 1 > max_out_lines) { truncated = 1; break; }
            emit_line(sb, color, pre, ops[k].p, ops[k].len);
            emitted++;
        }
        if (truncated) { break; }
    }
    if (truncated) {
        jc_sb_append(sb, "... (diff truncated)\n");
    }

    free(keep);
    free(ops);
    free(old);
    free(new_);
    return changed;
}
