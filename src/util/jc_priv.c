/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_priv.c - privileged-launcher detection (see jc_priv.h). */

#include "jc_priv.h"
#include "jc_platform.h"

#include <string.h>

static int pv_lc(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int pv_space(int c)
{
    return c == ' ' || c == '\t';
}

const char *jc_priv_kind_name(enum jc_priv_kind k)
{
    switch (k) {
    case JC_PRIV_SUDO:   return "sudo";
    case JC_PRIV_DOAS:   return "doas";
    case JC_PRIV_PKEXEC: return "pkexec";
    case JC_PRIV_SU:     return "su";
    case JC_PRIV_RUN0:   return "run0";
    default:             return "";
    }
}

/* A launcher bareword -> kind, or NONE. Compares the [p, end) slice exactly
 * (case-insensitively) against the launcher set. */
static enum jc_priv_kind launcher_kind(const char *p, jc_size n)
{
    struct { const char *name; enum jc_priv_kind kind; } tab[] = {
        { "sudo", JC_PRIV_SUDO }, { "sudoedit", JC_PRIV_SUDO },
        { "doas", JC_PRIV_DOAS }, { "pkexec", JC_PRIV_PKEXEC },
        { "su", JC_PRIV_SU },     { "run0", JC_PRIV_RUN0 }
    };
    unsigned int i;
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        jc_size ln = (jc_size)strlen(tab[i].name);
        jc_size j;
        if (ln != n) continue;
        for (j = 0; j < n; j++) {
            if (pv_lc((unsigned char)p[j]) != tab[i].name[j]) break;
        }
        if (j == n) return tab[i].kind;
    }
    return JC_PRIV_NONE;
}

/* Transparent wrappers whose FIRST arg is itself a command to run, so a
 * launcher right after one is still segment-leading (`nohup sudo x`). */
static int is_wrapper(const char *p, jc_size n)
{
    static const char *const w[] = {
        "nohup", "command", "exec", "time", "nice", "ionice", "stdbuf",
        "setsid", "nohup", "builtin", 0
    };
    int i;
    for (i = 0; w[i] != NULL; i++) {
        jc_size ln = (jc_size)strlen(w[i]);
        jc_size j;
        if (ln != n) continue;
        for (j = 0; j < n; j++) {
            if (pv_lc((unsigned char)p[j]) != w[i][j]) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

/* Does the bareword at p look like a VAR=value assignment? (an identifier
 * followed by '=' before any space) */
static int is_assignment(const char *p)
{
    jc_size i = 0;
    if (!((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z') ||
          p[0] == '_')) {
        return 0;
    }
    while (p[i] != '\0' && !pv_space((unsigned char)p[i])) {
        if (p[i] == '=') return 1;
        if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
              (p[i] >= '0' && p[i] <= '9') || p[i] == '_')) {
            return 0; /* not an identifier char before '=' */
        }
        i++;
    }
    return 0;
}

/* Advance past the bareword at *pp and any following whitespace. */
static void skip_token_and_ws(const char **pp)
{
    const char *p = *pp;
    while (*p != '\0' && !pv_space((unsigned char)*p) &&
           *p != '&' && *p != '|' && *p != ';' && *p != '\n' &&
           *p != '(' && *p != ')' && *p != '{' && *p != '}' && *p != '`') {
        p++;
    }
    while (pv_space((unsigned char)*p)) p++;
    *pp = p;
}

/* At a segment start, resolve the leading command word, skipping env
 * assignments, an `env ...` prefix, and transparent wrappers. Returns the
 * launcher kind if that word is a privilege launcher, else NONE. */
static enum jc_priv_kind check_segment(const char *p, const char **out_tok)
{
    int guard = 0;
    while (pv_space((unsigned char)*p)) p++;
    for (;;) {
        const char *tok;
        jc_size n;
        if (*p == '\0') return JC_PRIV_NONE;
        if (++guard > 64) return JC_PRIV_NONE; /* pathological input */

        /* VAR=value assignments before the command. */
        if (is_assignment(p)) {
            skip_token_and_ws(&p);
            continue;
        }
        /* Measure the bareword. */
        tok = p;
        n = 0;
        while (tok[n] != '\0' && !pv_space((unsigned char)tok[n]) &&
               tok[n] != '&' && tok[n] != '|' && tok[n] != ';' &&
               tok[n] != '\n' && tok[n] != '(' && tok[n] != ')' &&
               tok[n] != '{' && tok[n] != '}' && tok[n] != '`') {
            n++;
        }
        if (n == 0) return JC_PRIV_NONE;

        /* `env` prefix: skip env and its VAR=val / -opt args, then re-resolve. */
        if (n == 3 && pv_lc((unsigned char)tok[0]) == 'e' &&
            pv_lc((unsigned char)tok[1]) == 'n' &&
            pv_lc((unsigned char)tok[2]) == 'v') {
            skip_token_and_ws(&p);
            while (*p != '\0') {
                while (pv_space((unsigned char)*p)) p++;
                if (is_assignment(p) || *p == '-') {
                    skip_token_and_ws(&p);
                    continue;
                }
                break;
            }
            continue;
        }
        if (is_wrapper(tok, n)) {
            skip_token_and_ws(&p);
            continue;
        }
        {
            enum jc_priv_kind k = launcher_kind(tok, n);
            if (k != JC_PRIV_NONE && out_tok != NULL) {
                *out_tok = tok;
            }
            return k;
        }
    }
}

/* Does the command contain an unquoted chaining operator (so a prefix
 * allowlist could be evaded by appending `; sudo rm -rf`)? */
static int has_unquoted_chain(const char *s)
{
    int in_sq = 0, in_dq = 0;
    const char *p;
    for (p = s; *p != '\0'; p++) {
        char c = *p;
        if (in_sq) { if (c == '\'') in_sq = 0; continue; }
        if (in_dq) {
            if (c == '\\' && p[1] != '\0') { p++; continue; }
            if (c == '"') in_dq = 0;
            continue;
        }
        if (c == '\'') { in_sq = 1; continue; }
        if (c == '"') { in_dq = 1; continue; }
        if (c == ';' || c == '&' || c == '|' || c == '\n' || c == '`') {
            return 1;
        }
        if (c == '$' && p[1] == '(') return 1;
    }
    return 0;
}

int jc_priv_allowlisted(const char *command, const char *const *entries, int n)
{
    const char *c;
    int i;

    if (command == NULL || entries == NULL || n <= 0) {
        return 0;
    }
    c = command;
    while (*c == ' ' || *c == '\t') c++;
    /* A chained command can never be safely matched by a single prefix. */
    if (has_unquoted_chain(c)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        const char *e = entries[i];
        jc_size el;
        if (e == NULL) continue;
        while (*e == ' ' || *e == '\t') e++;
        el = (jc_size)strlen(e);
        while (el > 0 && (e[el - 1] == ' ' || e[el - 1] == '\t')) el--;
        if (el == 0) continue;
        if (strncmp(c, e, el) == 0 &&
            (c[el] == '\0' || c[el] == ' ' || c[el] == '\t')) {
            return 1;
        }
    }
    return 0;
}

enum jc_priv_kind jc_priv_detect(const char *command, const char **out_tok)
{
    const char *p;
    int in_sq = 0;
    int in_dq = 0;
    int seg_start = 1;

    if (out_tok != NULL) {
        *out_tok = NULL;
    }
    if (command == NULL) {
        return JC_PRIV_NONE;
    }
    for (p = command; *p != '\0'; p++) {
        char c = *p;
        if (in_sq) {
            if (c == '\'') in_sq = 0;
            continue;
        }
        if (in_dq) {
            if (c == '\\' && p[1] != '\0') { p++; continue; }
            if (c == '"') in_dq = 0;
            continue;
        }
        if (c == '\'') { in_sq = 1; seg_start = 0; continue; }
        if (c == '"') { in_dq = 1; seg_start = 0; continue; }

        /* Segment openers: operators, subshell/group/substitution starts. */
        if (c == '&' || c == '|' || c == ';' || c == '\n' ||
            c == '(' || c == '{' || c == '`') {
            seg_start = 1;
            continue;
        }
        if (c == '$' && p[1] == '(') {
            seg_start = 1;
            p++; /* skip '(' too */
            continue;
        }
        if (pv_space((unsigned char)c)) {
            continue;
        }
        if (seg_start) {
            enum jc_priv_kind k = check_segment(p, out_tok);
            if (k != JC_PRIV_NONE) {
                return k;
            }
            seg_start = 0;
        }
    }
    return JC_PRIV_NONE;
}
