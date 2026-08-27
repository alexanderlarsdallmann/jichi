/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_str.c - growable string builder (see jc_str.h). */

#include "jc_str.h"
#include "jc_snprintf.h"
#include <stdlib.h>
#include <string.h>

void jc_sb_init(struct jc_sb *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void jc_sb_free(struct jc_sb *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void jc_sb_clear(struct jc_sb *b)
{
    b->len = 0;
    if (b->data != NULL) {
        b->data[0] = '\0';
    }
}

void jc_sb_clear_shrink(struct jc_sb *b, jc_size max_cap)
{
    if (b->cap > max_cap) {
        jc_sb_free(b);
        jc_sb_init(b);
        return;
    }
    jc_sb_clear(b);
}

jc_status jc_sb_reserve(struct jc_sb *b, jc_size extra)
{
    jc_size need;
    jc_size newcap;
    char *p;

    /* Overflow guards (M472). Neither of these was reachable when they were
     * added -- every caller's length derives from a real buffer -- and that is
     * exactly the argument for having them: the alternative is a whole-program
     * claim about every caller of a general-purpose primitive, re-made each time
     * someone adds one. Two `if`s replace that claim with a local guarantee.
     *
     * Without the first, an `extra` near SIZE_MAX wraps `need` small, the early
     * return below reports success, and the caller's memcpy writes the
     * un-wrapped length past the end of the buffer.
     *
     * Without the second, `need` above SIZE_MAX/2 makes the doubling loop wrap to
     * 0 and spin forever -- a hang, not a crash. SIZE_MAX/2 is 9 exabytes on
     * 64-bit but 2 GB on the 32-bit ARM rows, which is a margin rather than an
     * impossibility. */
    if (extra > (jc_size)-1 - b->len - 1) {
        return JC_ERR_TOOBIG;
    }
    need = b->len + extra + 1; /* +1 for terminator */
    if (need <= b->cap) {
        return JC_OK;
    }
    if (need > ((jc_size)-1) / 2) {
        /* Cannot double into range. Ask for exactly what is needed and let the
         * allocator refuse it, rather than looping. */
        newcap = need;
    } else {
        newcap = (b->cap == 0) ? 64 : b->cap;
        while (newcap < need) {
            newcap *= 2;
        }
    }
    p = (char *)realloc(b->data, newcap);
    if (p == NULL) {
        return JC_ERR_OOM;
    }
    b->data = p;
    b->cap = newcap;
    return JC_OK;
}

jc_status jc_sb_append_n(struct jc_sb *b, const char *s, jc_size n)
{
    jc_status st;
    if (n == 0) {
        return JC_OK;
    }
    st = jc_sb_reserve(b, n);
    if (st != JC_OK) {
        return st;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return JC_OK;
}

jc_status jc_sb_append(struct jc_sb *b, const char *s)
{
    return jc_sb_append_n(b, s, strlen(s));
}

jc_status jc_sb_append_char(struct jc_sb *b, char c)
{
    jc_status st = jc_sb_reserve(b, 1);
    if (st != JC_OK) {
        return st;
    }
    b->data[b->len] = c;
    b->len++;
    b->data[b->len] = '\0';
    return JC_OK;
}

jc_status jc_sb_append_fmt(struct jc_sb *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    jc_status st;

    /* First pass: measure. */
    va_start(ap, fmt);
    n = jc_vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return JC_ERR_INVALID;
    }

    st = jc_sb_reserve(b, (jc_size)n);
    if (st != JC_OK) {
        return st;
    }

    /* Second pass: render into the reserved space. */
    va_start(ap, fmt);
    jc_vsnprintf(b->data + b->len, (jc_size)n + 1, fmt, ap);
    va_end(ap);
    b->len += (jc_size)n;
    return JC_OK;
}

char *jc_sb_finish(struct jc_sb *b)
{
    char *p;
    if (b->data == NULL) {
        /* Return an empty owned string for consistency. */
        p = (char *)malloc(1);
        if (p != NULL) {
            p[0] = '\0';
        }
        return p;
    }
    p = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return p;
}

char *jc_strdup(const char *s)
{
    char *p;
    jc_size n;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n + 1);
    return p;
}

int jc_str_close_enough(jc_size unknown_len, int distance)
{
    int threshold = (int)(unknown_len / 2);

    if (threshold < 2) {
        threshold = 2;
    }
    if (threshold > 4) {
        threshold = 4;
    }
    return distance >= 0 && distance <= threshold;
}

const char *jc_str_closest(const char *unknown, const char *const *cands)
{
    const char *best = NULL;
    int best_d = 1000000;
    jc_size i;

    if (unknown == NULL || unknown[0] == '\0' || cands == NULL) {
        return NULL;
    }
    for (i = 0; cands[i] != NULL; i++) {
        const char *c = (cands[i][0] == '/') ? cands[i] + 1 : cands[i];
        int d = jc_str_edit_distance(unknown, c);
        if (d >= 0 && d < best_d) {
            best_d = d;
            best = c;
        }
    }
    if (best != NULL && jc_str_close_enough(strlen(unknown), best_d)) {
        return best;
    }
    return NULL;
}

int jc_str_edit_distance(const char *a, const char *b)
{
    jc_size la, lb, i, j;
    int prev[64];
    int curr[64];

    if (a == NULL || b == NULL) {
        return -1;
    }
    la = strlen(a);
    lb = strlen(b);
    if (la > 63 || lb > 63) {
        return (int)(la > lb ? la - lb : lb - la);
    }
    for (j = 0; j <= lb; j++) {
        prev[j] = (int)j;
    }
    for (i = 1; i <= la; i++) {
        curr[0] = (int)i;
        for (j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int m = del < ins ? del : ins;
            curr[j] = m < sub ? m : sub;
        }
        for (j = 0; j <= lb; j++) {
            prev[j] = curr[j];
        }
    }
    return prev[lb];
}

char jc_group_sep_audience(char configured, int accessible)
{
    return accessible ? (char)0 : configured;
}

void jc_group_num(double v, char sep, char *buf, jc_size cap)
{
    char raw[40];
    int n, i;
    jc_size o = 0;
    if (buf == NULL || cap == 0) {
        return;
    }
    if (v < 0.0) {
        v = -v;
    }
    n = jc_snprintf(raw, sizeof raw, "%.0f", v);
    if (n < 0) {
        buf[0] = '\0';
        return;
    }
    for (i = 0; i < n; i++) {
        if (sep != 0 && i != 0 && ((n - i) % 3 == 0) && o + 1 < cap) {
            buf[o++] = sep;
        }
        if (o + 1 < cap) {
            buf[o++] = raw[i];
        }
    }
    buf[o] = '\0';
}

int jc_envvar_name_valid(const char *name)
{
    const char *p;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) {
        return 0;
    }
    for (p = name + 1; *p != '\0'; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) {
            return 0;
        }
    }
    return 1;
}

/* --- M534: the one boolean dialect (see jc_str.h) ------------------------- */

static int bool_word_ci(const char *s, const char *want)
{
    jc_size i;
    for (i = 0; s[i] != '\0' && want[i] != '\0'; i++) {
        int a = (unsigned char)s[i];
        if (a >= 'A' && a <= 'Z') {
            a = a - 'A' + 'a';
        }
        if (a != (unsigned char)want[i]) {
            return 0;
        }
    }
    return s[i] == '\0' && want[i] == '\0';
}

int jc_bool_from_word(const char *s, int *out)
{
    if (s == NULL || s[0] == '\0' || out == NULL) {
        return 0;
    }
    if (bool_word_ci(s, "true") || bool_word_ci(s, "yes") ||
        bool_word_ci(s, "on") || bool_word_ci(s, "1")) {
        *out = 1;
        return 1;
    }
    if (bool_word_ci(s, "false") || bool_word_ci(s, "no") ||
        bool_word_ci(s, "off") || bool_word_ci(s, "0")) {
        *out = 0;
        return 1;
    }
    return 0;
}
