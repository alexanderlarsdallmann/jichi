/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_kinetic.c - kinetic-action shadow matching (see jc_kinetic.h). Pure. */

#include "jc_kinetic.h"
#include "jc_platform.h"

#include <string.h>

static int kn_space(int c)
{
    return c == ' ' || c == '\t';
}

/* A bareword ends at whitespace or a shell metachar (segment opener / group). */
static int kn_word_end(char c)
{
    return c == '\0' || kn_space((unsigned char)c) ||
           c == '&' || c == '|' || c == ';' || c == '\n' ||
           c == '(' || c == ')' || c == '{' || c == '}' || c == '`';
}

/* Length of the bareword at p (0 if none). */
static jc_size word_len(const char *p)
{
    jc_size n = 0;
    while (!kn_word_end(p[n])) n++;
    return n;
}

static void skip_ws(const char **pp)
{
    const char *p = *pp;
    while (kn_space((unsigned char)*p)) p++;
    *pp = p;
}

static void skip_word_and_ws(const char **pp)
{
    const char *p = *pp;
    p += word_len(p);
    while (kn_space((unsigned char)*p)) p++;
    *pp = p;
}

/* Case-insensitive compare of the [p,p+n) slice against a NUL-terminated
 * lowercase word. */
static int word_eq(const char *p, jc_size n, const char *w)
{
    jc_size i;
    if ((jc_size)strlen(w) != n) return 0;
    for (i = 0; i < n; i++) {
        int c = p[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (c != w[i]) return 0;
    }
    return 1;
}

static int is_assignment(const char *p)
{
    jc_size i = 0;
    if (!((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z') ||
          p[0] == '_')) {
        return 0;
    }
    while (!kn_word_end(p[i])) {
        if (p[i] == '=') return 1;
        if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
              (p[i] >= '0' && p[i] <= '9') || p[i] == '_')) {
            return 0;
        }
        i++;
    }
    return 0;
}

/* Transparent wrappers + interpreters: the word AFTER one of these is the
 * command we actually care about (`nohup sudo x`, `sh motor.sh`,
 * `python3 arm.py`). */
static int is_wrapper(const char *p, jc_size n)
{
    static const char *const w[] = {
        "nohup", "command", "exec", "time", "nice", "ionice", "stdbuf",
        "setsid", "builtin",
        "sh", "bash", "dash", "zsh", "ksh",
        "python", "python2", "python3", "perl", "ruby", "node", 0
    };
    int i;
    for (i = 0; w[i] != NULL; i++) {
        if (word_eq(p, n, w[i])) return 1;
    }
    return 0;
}

/* Resolve a segment's leading command word: skip VAR= assignments, an `env`
 * prefix (+ its opts/assignments), transparent wrappers/interpreters, and a
 * run of `-options` after them. Returns a pointer to the leading word (its
 * length via word_len), or NULL at end/segment-break. */
static const char *resolve_lead(const char *p)
{
    int guard = 0;
    skip_ws(&p);
    for (;;) {
        jc_size n;
        if (*p == '\0' || kn_word_end(*p)) return NULL;
        if (++guard > 64) return NULL; /* pathological input */

        if (is_assignment(p)) {
            skip_word_and_ws(&p);
            continue;
        }
        n = word_len(p);
        if (n == 0) return NULL;

        if (word_eq(p, n, "env")) {
            skip_word_and_ws(&p);
            while (*p != '\0') {
                skip_ws(&p);
                if (is_assignment(p) || *p == '-') {
                    skip_word_and_ws(&p);
                    continue;
                }
                break;
            }
            continue;
        }
        if (is_wrapper(p, n)) {
            skip_word_and_ws(&p);
            /* Skip options between an interpreter and its script arg
             * (`python3 -u arm.py`, `sh -e motor.sh`). */
            while (*p == '-') {
                skip_word_and_ws(&p);
            }
            continue;
        }
        return p;
    }
}

/* basename of the [s,s+n) slice: the part after the last '/'. */
static void base_of(const char *s, jc_size n, const char **b, jc_size *bn)
{
    jc_size i;
    jc_size start = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '/') start = i + 1;
    }
    *b = s + start;
    *bn = n - start;
}

/* Does the segment beginning at `seg` (its leading word already resolved)
 * token-prefix-match `prefix`? Token 0 matches by exact string OR basename;
 * tokens 1.. must match exactly (whitespace-separated). */
static int seg_matches_prefix(const char *seg, const char *prefix)
{
    const char *sp = seg;
    const char *pp = prefix;
    int first = 1;

    while (*pp == ' ' || *pp == '\t') pp++;
    if (*pp == '\0') return 0;

    for (;;) {
        const char *st;
        const char *pt;
        jc_size sn, pn;

        skip_ws(&sp);
        while (*pp == ' ' || *pp == '\t') pp++;
        if (*pp == '\0') return 1;            /* all prefix tokens matched */
        if (*sp == '\0' || kn_word_end(*sp)) return 0; /* segment ran out */

        st = sp; sn = word_len(sp);
        pt = pp; pn = 0;
        while (pt[pn] != '\0' && !kn_space((unsigned char)pt[pn])) pn++;

        if (first) {
            const char *sb;
            const char *pb;
            jc_size sbn, pbn;
            int ok = (sn == pn && strncmp(st, pt, sn) == 0);
            if (!ok) {
                base_of(st, sn, &sb, &sbn);
                base_of(pt, pn, &pb, &pbn);
                ok = (sbn == pbn && sbn > 0 && strncmp(sb, pb, sbn) == 0);
            }
            if (!ok) return 0;
            first = 0;
        } else {
            if (sn != pn || strncmp(st, pt, sn) != 0) return 0;
        }
        sp += sn;
        pp = pt + pn;
    }
}

int jc_kinetic_shell_match(const char *command, const char *const *prefixes,
                           int n, const char **out_hit)
{
    const char *p;
    int in_sq = 0, in_dq = 0, seg_start = 1;

    if (out_hit != NULL) *out_hit = NULL;
    if (command == NULL || prefixes == NULL || n <= 0) return 0;

    for (p = command; *p != '\0'; p++) {
        char c = *p;
        if (in_sq) { if (c == '\'') in_sq = 0; continue; }
        if (in_dq) {
            if (c == '\\' && p[1] != '\0') { p++; continue; }
            if (c == '"') in_dq = 0;
            continue;
        }
        if (c == '\'') { in_sq = 1; seg_start = 0; continue; }
        if (c == '"') { in_dq = 1; seg_start = 0; continue; }
        if (c == '&' || c == '|' || c == ';' || c == '\n' ||
            c == '(' || c == '{' || c == '`') {
            seg_start = 1;
            continue;
        }
        if (c == '$' && p[1] == '(') { seg_start = 1; p++; continue; }
        if (kn_space((unsigned char)c)) continue;
        if (seg_start) {
            const char *lead = resolve_lead(p);
            if (lead != NULL) {
                int i;
                for (i = 0; i < n; i++) {
                    if (prefixes[i] != NULL && prefixes[i][0] != '\0' &&
                        seg_matches_prefix(lead, prefixes[i])) {
                        if (out_hit != NULL) *out_hit = prefixes[i];
                        return 1;
                    }
                }
            }
            seg_start = 0;
        }
    }
    return 0;
}

int jc_kinetic_name_allowlisted(const char *name, const char *const *entries,
                                int n)
{
    int i;
    if (name == NULL || entries == NULL || n <= 0) return 0;
    for (i = 0; i < n; i++) {
        const char *e = entries[i];
        jc_size el;
        if (e == NULL) continue;
        while (*e == ' ' || *e == '\t') e++;
        el = (jc_size)strlen(e);
        while (el > 0 && (e[el - 1] == ' ' || e[el - 1] == '\t')) el--;
        if (el == 0) continue;
        if ((jc_size)strlen(name) == el && strncmp(name, e, el) == 0) {
            return 1;
        }
    }
    return 0;
}
