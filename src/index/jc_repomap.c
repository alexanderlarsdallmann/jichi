/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_repomap.c - repository map. See include/jc_repomap.h.
 *
 * jc_repomap_scan is a pure, language-keyed heuristic that pulls top-level
 * definitions out of a source file by scanning lines -- no parsing, no LSP.
 * jc_repomap_build walks the workspace and renders the bounded map section.
 */

#include "jc_repomap.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#define RM_MAX_FILE_BYTES (512L * 1024L)

/* ---- small char helpers ------------------------------------------------- */

/* M523: a byte >= 0x80 counts as an identifier character. Python, Zig, JS, Java,
 * Ruby and Elixir all permit non-ASCII identifiers, and the scanner is fed
 * (unsigned char), so a UTF-8 lead or continuation byte arrives here as
 * 128..255. Measured before the change: a workspace holding 계산기.py and
 * 計算.py produced a repo map listing both FILES and not one symbol, because
 * `def ` was followed by nothing the predicate recognised as a name -- a file
 * paying map rent while contributing only its own path.
 *
 * Deliberately a byte test rather than UTF-8 decoding: the scanner is a
 * line-oriented heuristic over bytes (its own header says so), a symbol name is
 * copied out verbatim rather than interpreted, and no C89 identifier can contain
 * these bytes anyway -- so the widest correct rule is also the simplest. */
static int rm_id(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}
static int rm_id0(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c >= 0x80;
}
static int rm_sp(int c) { return c == ' ' || c == '\t'; }

static const char *id_end(const char *p)
{
    while (rm_id((unsigned char)*p)) p++;
    return p;
}

/* `s` begins with word `w` and the next char is not an identifier char. */
static int kw(const char *s, const char *w)
{
    jc_size n = (jc_size)strlen(w);
    return strncmp(s, w, n) == 0 && !rm_id((unsigned char)s[n]);
}

/* Push [s,e) as a symbol name (malloc'd), bounded by JC_REPOMAP_SCAN_CAP. */
static void add_sym(struct jc_vec *out, const char *s, const char *e,
                    int *count)
{
    char buf[128];
    jc_size n;
    char *dup;

    if (*count >= JC_REPOMAP_SCAN_CAP) return;
    n = (jc_size)(e - s);
    if (n == 0 || n > sizeof(buf) - 1) return;
    memcpy(buf, s, n);
    buf[n] = '\0';
    {
        /* Skip a name already collected (clause-based langs repeat names). */
        jc_size i;
        for (i = 0; i < out->len; i++) {
            char *ex = *(char **)jc_vec_at(out, i);
            if (ex != NULL && strcmp(ex, buf) == 0) return;
        }
    }
    dup = jc_strdup(buf);
    if (dup != NULL && jc_vec_push(out, &dup) == JC_OK) {
        (*count)++;
    } else {
        free(dup);
    }
}

/* Emit the identifier that follows word `w` at the start of `p`. */
static void emit_after(const char *p, const char *w, struct jc_vec *out,
                       int *count)
{
    p += strlen(w);
    while (rm_sp((unsigned char)*p)) p++;
    if (rm_id0((unsigned char)*p)) {
        add_sym(out, p, id_end(p), count);
    }
}

/* ---- language matchers (operate on a left-trimmed line `l`) -------------- */

enum rm_lang {
    RL_NONE, RL_C, RL_PY, RL_GO, RL_RS, RL_JS, RL_JAVA, RL_RB, RL_SH,
    RL_RKT, RL_ZIG, RL_CLJ, RL_EX, RL_ERL, RL_HS
};

static enum rm_lang lang_of(const char *ext)
{
    if (ext == NULL) return RL_NONE;
    if (!strcmp(ext, "c") || !strcmp(ext, "h") || !strcmp(ext, "cc") ||
        !strcmp(ext, "cpp") || !strcmp(ext, "cxx") || !strcmp(ext, "hpp") ||
        !strcmp(ext, "hh") || !strcmp(ext, "hxx")) {
        return RL_C;
    }
    if (!strcmp(ext, "py")) return RL_PY;
    if (!strcmp(ext, "go")) return RL_GO;
    if (!strcmp(ext, "rs")) return RL_RS;
    if (!strcmp(ext, "js") || !strcmp(ext, "jsx") || !strcmp(ext, "ts") ||
        !strcmp(ext, "tsx") || !strcmp(ext, "mjs") || !strcmp(ext, "cjs")) {
        return RL_JS;
    }
    if (!strcmp(ext, "java")) return RL_JAVA;
    if (!strcmp(ext, "rb")) return RL_RB;
    if (!strcmp(ext, "sh") || !strcmp(ext, "bash")) return RL_SH;
    if (!strcmp(ext, "rkt") || !strcmp(ext, "rktl")) return RL_RKT;
    /* Scheme + Guile share the Racket s-expression `define` scanner. */
    if (!strcmp(ext, "scm") || !strcmp(ext, "ss") || !strcmp(ext, "sld") ||
        !strcmp(ext, "sls") || !strcmp(ext, "sps")) {
        return RL_RKT;
    }
    if (!strcmp(ext, "zig")) return RL_ZIG;
    if (!strcmp(ext, "clj") || !strcmp(ext, "cljs") || !strcmp(ext, "cljc")) {
        return RL_CLJ;
    }
    if (!strcmp(ext, "ex") || !strcmp(ext, "exs")) return RL_EX;
    if (!strcmp(ext, "erl") || !strcmp(ext, "hrl")) return RL_ERL;
    if (!strcmp(ext, "hs")) return RL_HS;
    return RL_NONE;
}

static int is_c_ctrl(const char *s, const char *e)
{
    static const char *kws[] = { "if", "for", "while", "switch", "return",
                                 "sizeof", "do", "else", "case", "defined", 0 };
    jc_size n = (jc_size)(e - s);
    int i;
    for (i = 0; kws[i] != 0; i++) {
        if ((jc_size)strlen(kws[i]) == n && strncmp(s, kws[i], n) == 0) return 1;
    }
    return 0;
}

/* C/C++: the identifier defined by a function-definition line, or 0. */
static int c_func(const char *l, const char **ns, const char **ne)
{
    const char *paren = strchr(l, '(');
    const char *eq = strchr(l, '=');
    const char *p;
    const char *s;
    const char *e;

    if (paren == NULL) return 0;
    if (eq != NULL && eq < paren) return 0;        /* assignment, not a def  */
    p = paren;
    while (p > l && rm_sp((unsigned char)p[-1])) p--;
    if (p == l || !rm_id((unsigned char)p[-1])) return 0;
    e = p;
    s = p;
    while (s > l && rm_id((unsigned char)s[-1])) s--;
    if (s == l) return 0;                          /* "name(...)": call/macro */
    if (!rm_id0((unsigned char)*s)) return 0;
    if (is_c_ctrl(s, e)) return 0;                 /* while(...) etc.        */
    *ns = s;
    *ne = e;
    return 1;
}

static void scan_c(const char *l, int indent, struct jc_vec *out, int *count)
{
    jc_size len;
    if (indent != 0) return;
    if (l[0] == '#' || l[0] == '/' || l[0] == '*') return;
    if (l[0] == '}') {
        /* typedef close: "} NAME;" */
        const char *semi = strchr(l, ';');
        if (semi != NULL) {
            const char *e = semi;
            const char *s;
            while (e > l && rm_sp((unsigned char)e[-1])) e--;
            s = e;
            while (s > l && rm_id((unsigned char)s[-1])) s--;
            if (e > s && rm_id0((unsigned char)*s)) add_sym(out, s, e, count);
        }
        return;
    }
    if (kw(l, "typedef")) {
        const char *semi = strrchr(l, ';');
        if (semi != NULL) {
            const char *e = semi;
            const char *s;
            while (e > l && rm_sp((unsigned char)e[-1])) e--;
            s = e;
            while (s > l && rm_id((unsigned char)s[-1])) s--;
            if (e > s && rm_id0((unsigned char)*s)) add_sym(out, s, e, count);
        } else {
            const char *p = l + 7;
            while (rm_sp((unsigned char)*p)) p++;
            if (kw(p, "struct")) p += 6;
            else if (kw(p, "union")) p += 5;
            else if (kw(p, "enum")) p += 4;
            while (rm_sp((unsigned char)*p)) p++;
            if (rm_id0((unsigned char)*p)) add_sym(out, p, id_end(p), count);
        }
        return;
    }
    if (kw(l, "struct")) { emit_after(l, "struct", out, count); return; }
    if (kw(l, "union")) { emit_after(l, "union", out, count); return; }
    if (kw(l, "enum")) { emit_after(l, "enum", out, count); return; }

    len = (jc_size)strlen(l);
    if (len > 0 && l[len - 1] == ';') return;      /* prototype/declaration  */
    {
        const char *ns;
        const char *ne;
        if (c_func(l, &ns, &ne)) add_sym(out, ns, ne, count);
    }
}

static void scan_py(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p = l;
    if (indent != 0) return;
    if (kw(p, "async")) { p += 5; while (rm_sp((unsigned char)*p)) p++; }
    if (kw(p, "def")) emit_after(p, "def", out, count);
    else if (kw(p, "class")) emit_after(p, "class", out, count);
}

static void scan_go(const char *l, int indent, struct jc_vec *out, int *count)
{
    if (indent != 0) return;
    if (kw(l, "func")) {
        const char *p = l + 4;
        while (rm_sp((unsigned char)*p)) p++;
        if (*p == '(') {                            /* method: skip (recv)    */
            const char *q = strchr(p, ')');
            if (q != NULL) {
                q++;
                while (rm_sp((unsigned char)*q)) q++;
                if (rm_id0((unsigned char)*q)) add_sym(out, q, id_end(q), count);
            }
        } else if (rm_id0((unsigned char)*p)) {
            add_sym(out, p, id_end(p), count);
        }
    } else if (kw(l, "type")) {
        emit_after(l, "type", out, count);
    }
}

static void scan_rs(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p = l;
    if (indent != 0) return;
    if (strncmp(p, "pub", 3) == 0 && (p[3] == ' ' || p[3] == '(')) {
        p += 3;
        if (*p == '(') { const char *q = strchr(p, ')'); if (q != NULL) p = q + 1; }
        while (rm_sp((unsigned char)*p)) p++;
    }
    while (kw(p, "async") || kw(p, "unsafe") || kw(p, "extern") ||
           kw(p, "default") || kw(p, "const")) {
        while (rm_id((unsigned char)*p)) p++;
        while (rm_sp((unsigned char)*p)) p++;
        if (*p == '"') break;                       /* extern "C"             */
    }
    if (kw(p, "fn")) emit_after(p, "fn", out, count);
    else if (kw(p, "struct")) emit_after(p, "struct", out, count);
    else if (kw(p, "enum")) emit_after(p, "enum", out, count);
    else if (kw(p, "trait")) emit_after(p, "trait", out, count);
    else if (kw(p, "mod")) emit_after(p, "mod", out, count);
}

static void scan_js(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p = l;
    if (indent != 0) return;
    if (kw(p, "export")) {
        p += 6;
        while (rm_sp((unsigned char)*p)) p++;
        if (kw(p, "default")) { p += 7; while (rm_sp((unsigned char)*p)) p++; }
    }
    if (kw(p, "async")) { p += 5; while (rm_sp((unsigned char)*p)) p++; }
    if (kw(p, "function")) {
        p += 8;
        while (rm_sp((unsigned char)*p)) p++;
        if (*p == '*') { p++; while (rm_sp((unsigned char)*p)) p++; }
        if (rm_id0((unsigned char)*p)) add_sym(out, p, id_end(p), count);
    } else if (kw(p, "class")) {
        emit_after(p, "class", out, count);
    } else if (kw(p, "const")) {
        emit_after(p, "const", out, count);
    } else if (kw(p, "let")) {
        emit_after(p, "let", out, count);
    } else if (kw(p, "var")) {
        emit_after(p, "var", out, count);
    }
}

static void scan_java(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p = l;
    (void)indent;                                   /* nested types allowed   */
    while (kw(p, "public") || kw(p, "private") || kw(p, "protected") ||
           kw(p, "abstract") || kw(p, "final") || kw(p, "static") ||
           kw(p, "sealed")) {
        while (rm_id((unsigned char)*p)) p++;
        while (rm_sp((unsigned char)*p)) p++;
    }
    if (kw(p, "class")) emit_after(p, "class", out, count);
    else if (kw(p, "interface")) emit_after(p, "interface", out, count);
    else if (kw(p, "enum")) emit_after(p, "enum", out, count);
}

static void scan_rb(const char *l, int indent, struct jc_vec *out, int *count)
{
    (void)indent;
    if (kw(l, "def")) {
        const char *p = l + 3;
        while (rm_sp((unsigned char)*p)) p++;
        if (strncmp(p, "self.", 5) == 0) p += 5;
        if (rm_id0((unsigned char)*p)) add_sym(out, p, id_end(p), count);
    } else if (kw(l, "class")) {
        emit_after(l, "class", out, count);
    } else if (kw(l, "module")) {
        emit_after(l, "module", out, count);
    }
}

static void scan_sh(const char *l, int indent, struct jc_vec *out, int *count)
{
    (void)indent;
    if (kw(l, "function")) { emit_after(l, "function", out, count); return; }
    if (rm_id0((unsigned char)*l)) {
        const char *e = id_end(l);
        const char *q = e;
        while (rm_sp((unsigned char)*q)) q++;
        if (q[0] == '(' && q[1] == ')') add_sym(out, l, e, count);
    }
}

/* Racket token delimiters: whitespace, parens/brackets/braces, quote and
 * comment chars. Identifiers (stack-empty?, string->number) run between them. */
static int rkt_delim(int c)
{
    return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
           c == '}' || c == '"' || c == ';' || c == '\'' || c == '`' ||
           c == ',';
}
static const char *rkt_id_end(const char *p)
{
    while (!rkt_delim((unsigned char)*p)) p++;
    return p;
}

static void scan_rkt(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p;
    const char *hs;
    const char *he;
    jc_size hlen;

    if (indent != 0 || l[0] != '(') return;
    p = l + 1;
    while (rm_sp((unsigned char)*p)) p++;
    hs = p;
    he = rkt_id_end(p);
    hlen = (jc_size)(he - hs);
    if (hlen == 0) return;
    /* A define* form, or a struct form, names the next token. */
    if ((hlen >= 6 && strncmp(hs, "define", 6) == 0) ||
        (hlen == 6 && strncmp(hs, "struct", 6) == 0)) {
        const char *q = he;
        while (rm_sp((unsigned char)*q)) q++;
        while (*q == '(') {                 /* (define (name …)) / ((curried a) b) */
            q++;
            while (rm_sp((unsigned char)*q)) q++;
        }
        if (!rkt_delim((unsigned char)*q)) add_sym(out, q, rkt_id_end(q), count);
    }
}

static void scan_zig(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p = l;
    int moved;

    if (indent != 0) return;
    do {
        moved = 0;
        if (kw(p, "pub") || kw(p, "export") || kw(p, "extern") ||
            kw(p, "inline") || kw(p, "noinline") || kw(p, "comptime") ||
            kw(p, "threadlocal")) {
            while (rm_id((unsigned char)*p)) p++;
            while (rm_sp((unsigned char)*p)) p++;
            if (*p == '"') {                 /* extern "c" fn … */
                p++;
                while (*p != '\0' && *p != '"') p++;
                if (*p == '"') p++;
                while (rm_sp((unsigned char)*p)) p++;
            }
            moved = 1;
        }
    } while (moved);
    if (kw(p, "fn")) emit_after(p, "fn", out, count);
    else if (kw(p, "const")) emit_after(p, "const", out, count);
    else if (kw(p, "var")) emit_after(p, "var", out, count);
}

/* Clojure: top-level (def…)/(ns …) forms; reuses the s-expression reader. */
static void scan_clj(const char *l, int indent, struct jc_vec *out, int *count)
{
    const char *p;
    const char *hs;
    const char *he;
    jc_size hlen;

    if (indent != 0 || l[0] != '(') return;
    p = l + 1;
    while (rm_sp((unsigned char)*p)) p++;
    hs = p;
    he = rkt_id_end(p);
    hlen = (jc_size)(he - hs);
    if ((hlen >= 3 && strncmp(hs, "def", 3) == 0) ||
        (hlen == 2 && strncmp(hs, "ns", 2) == 0)) {
        const char *q = he;
        while (rm_sp((unsigned char)*q)) q++;
        while (*q == '^' || *q == '#') {            /* skip metadata / reader */
            while (!rkt_delim((unsigned char)*q)) q++;
            while (rm_sp((unsigned char)*q)) q++;
            if (*q == '{') {                        /* ^{...} metadata map */
                while (*q != '\0' && *q != '}') q++;
                if (*q == '}') q++;
                while (rm_sp((unsigned char)*q)) q++;
            }
        }
        if (!rkt_delim((unsigned char)*q)) add_sym(out, q, rkt_id_end(q), count);
    }
}

/* Elixir identifiers: word chars plus ? ! (predicates/bangs) and . (modules). */
static const char *ex_id_end(const char *p)
{
    while (rm_id((unsigned char)*p) || *p == '?' || *p == '!' || *p == '.') p++;
    return p;
}

static void scan_ex(const char *l, int indent, struct jc_vec *out, int *count)
{
    static const char *kws[] = {
        "defmodule", "defprotocol", "defimpl", "defmacrop", "defmacro",
        "defguardp", "defguard", "defdelegate", "defexception", "defp",
        "def", 0
    };
    int i;
    (void)indent;                                   /* defs nest inside modules */
    for (i = 0; kws[i] != 0; i++) {
        if (kw(l, kws[i])) {
            const char *p = l + strlen(kws[i]);
            while (rm_sp((unsigned char)*p)) p++;
            if (rm_id0((unsigned char)*p)) add_sym(out, p, ex_id_end(p), count);
            return;
        }
    }
}

/* Erlang: -record/-define attributes and lowercase function clauses. */
static void scan_erl(const char *l, int indent, struct jc_vec *out, int *count)
{
    if (indent != 0) return;
    if (l[0] == '-') {
        const char *p = l + 1;
        const char *ae = id_end(p);
        if ((jc_size)(ae - p) == 6 &&
            (strncmp(p, "record", 6) == 0 || strncmp(p, "define", 6) == 0)) {
            const char *q = ae;
            while (rm_sp((unsigned char)*q)) q++;
            if (*q == '(') {
                q++;
                while (rm_sp((unsigned char)*q)) q++;
                if (rm_id0((unsigned char)*q)) add_sym(out, q, id_end(q), count);
            }
        }
        return;
    }
    if (*l >= 'a' && *l <= 'z') {                   /* function clause head */
        const char *e = id_end(l);
        const char *q = e;
        while (rm_sp((unsigned char)*q)) q++;
        if (*q == '(') add_sym(out, l, e, count);   /* dup clauses deduped */
    }
}

/* Haskell identifiers may carry a trailing prime (foldl'). */
static const char *hs_id_end(const char *p)
{
    while (rm_id((unsigned char)*p) || *p == '\'') p++;
    return p;
}

static void scan_hs(const char *l, int indent, struct jc_vec *out, int *count)
{
    if (indent != 0) return;
    if (kw(l, "module")) {
        const char *p = l + 6;
        while (rm_sp((unsigned char)*p)) p++;
        if (rm_id0((unsigned char)*p)) {
            const char *e = p;
            while (rm_id((unsigned char)*e) || *e == '\'' || *e == '.') e++;
            add_sym(out, p, e, count);
        }
        return;
    }
    if (kw(l, "data") || kw(l, "newtype") || kw(l, "type") || kw(l, "class")) {
        /* The declared name is the last uppercase-initial identifier before
         * `=` or `where` (skips any `(Ctx a) =>` constraint). */
        const char *p = l;
        const char *bs = NULL;
        const char *be = NULL;
        while (rm_id((unsigned char)*p)) p++;       /* past the keyword */
        while (*p != '\0') {
            if (*p == '=') {
                if (p[1] == '>') { p += 2; continue; } /* skip a constraint => */
                break;                                 /* real body: stop */
            }
            if (rm_id0((unsigned char)*p)) {
                const char *e = hs_id_end(p);
                if ((jc_size)(e - p) == 5 && strncmp(p, "where", 5) == 0) break;
                if (*p >= 'A' && *p <= 'Z') { bs = p; be = e; }
                p = e;
            } else {
                p++;
            }
        }
        if (bs != NULL) add_sym(out, bs, be, count);
        return;
    }
    if (*l >= 'a' && *l <= 'z') {                   /* type signature: name :: */
        const char *e = hs_id_end(l);
        const char *q = e;
        while (rm_sp((unsigned char)*q)) q++;
        if (q[0] == ':' && q[1] == ':') add_sym(out, l, e, count);
    }
}

static void dispatch(enum rm_lang lg, const char *l, int indent,
                     struct jc_vec *out, int *count)
{
    switch (lg) {
    case RL_C:    scan_c(l, indent, out, count); break;
    case RL_PY:   scan_py(l, indent, out, count); break;
    case RL_GO:   scan_go(l, indent, out, count); break;
    case RL_RS:   scan_rs(l, indent, out, count); break;
    case RL_JS:   scan_js(l, indent, out, count); break;
    case RL_JAVA: scan_java(l, indent, out, count); break;
    case RL_RB:   scan_rb(l, indent, out, count); break;
    case RL_SH:   scan_sh(l, indent, out, count); break;
    case RL_RKT:  scan_rkt(l, indent, out, count); break;
    case RL_ZIG:  scan_zig(l, indent, out, count); break;
    case RL_CLJ:  scan_clj(l, indent, out, count); break;
    case RL_EX:   scan_ex(l, indent, out, count); break;
    case RL_ERL:  scan_erl(l, indent, out, count); break;
    case RL_HS:   scan_hs(l, indent, out, count); break;
    default: break;
    }
}

int jc_repomap_scan(const char *ext, const char *text, struct jc_vec *out)
{
    enum rm_lang lg = lang_of(ext);
    int count = 0;
    const char *p;

    if (lg == RL_NONE || text == NULL) return 0;
    p = text;
    while (count < JC_REPOMAP_SCAN_CAP) {
        const char *ls = p;
        const char *e;
        const char *t;
        char lbuf[1024];
        int indent = 0;
        jc_size n;

        while (*p != '\0' && *p != '\n') p++;
        e = p;
        t = ls;
        while (t < e && rm_sp((unsigned char)*t)) { indent++; t++; }
        while (e > t && rm_sp((unsigned char)e[-1])) e--;
        n = (jc_size)(e - t);
        if (n > sizeof(lbuf) - 1) n = sizeof(lbuf) - 1;
        if (n > 0) memcpy(lbuf, t, n);
        lbuf[n] = '\0';
        if (lbuf[0] != '\0') dispatch(lg, lbuf, indent, out, &count);
        if (*p == '\0') break;
        p++;
    }
    return count;
}

/* ---- builder ------------------------------------------------------------ */

/* Extension (after the last '.') of a basename, or "". */
static const char *path_ext(const char *path)
{
    const char *base = path;
    const char *dot = NULL;
    const char *p;
    for (p = path; *p != '\0'; p++) {
        if (*p == '/') base = p + 1;
        else if (*p == '.') dot = p;
    }
    if (dot == NULL || dot < base || dot[1] == '\0') return "";
    return dot + 1;
}

int jc_walk_skip_dir(const char *name, const struct jc_vec *extra)
{
    jc_size i;
    if (name == NULL || name[0] == '\0') return 1;
    if (name[0] == '.') return 1;   /* .git, .zig-cache, .venv, .jichi ...   */
    if (strcmp(name, "node_modules") == 0 || strcmp(name, "target") == 0 ||
        strcmp(name, "build") == 0 || strcmp(name, "dist") == 0 ||
        strcmp(name, "__pycache__") == 0) {
        return 1;
    }
    /* The operator's additions. Compared by NAME, at any depth, exactly like
     * the built-ins above: a venv, a reference corpus or a coverage report is
     * uninteresting wherever it sits. */
    for (i = 0; extra != NULL && i < extra->len; i++) {
        const char *e = *(char **)jc_vec_at((struct jc_vec *)extra, i);
        if (e != NULL && e[0] != '\0' && strcmp(name, e) == 0) {
            return 1;
        }
    }
    return 0;
}

static void rm_walk(const char *dir, struct jc_vec *files, struct jc_arena *a,
                    const struct jc_vec *ignore)
{
    struct jc_vec names;
    jc_size i;

    if (files->len >= JC_REPOMAP_MAX_FILES) return;
    jc_vec_init(&names, sizeof(char *));
    if (jc_list_dir(dir, &names, a) != JC_OK) {
        jc_vec_free(&names);
        return;
    }
    for (i = 0; i < names.len && files->len < JC_REPOMAP_MAX_FILES; i++) {
        const char *name = *(char **)jc_vec_at(&names, i);
        jc_size flen = (jc_size)(strlen(dir) + strlen(name) + 2);
        char *full = (char *)jc_arena_alloc(a, flen);
        if (full == NULL) break;
        jc_snprintf(full, flen, "%s/%s", dir, name);
        if (jc_is_dir(full)) {
            if (!jc_walk_skip_dir(name, ignore)) {
                rm_walk(full, files, a, ignore);
            }
        } else if (lang_of(path_ext(full)) != RL_NONE &&
                   jc_file_size(full) <= RM_MAX_FILE_BYTES) {
            jc_vec_push(files, &full);
        }
    }
    jc_vec_free(&names);
}

static int path_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Render the "<relpath>: sym, sym, ..." listing for the source tree rooted at
 * `root` (paths shown relative to `root`), bounded to `limit` bytes. Returns a
 * new arena string, or NULL when the tree has no recognised source files.
 * Shared by the whole-workspace map and the @folder reference (M34/F5). */
static char *render_listing(struct jc_app *app, const char *root, jc_size limit)
{
    struct jc_vec files;
    struct jc_sb sb;
    jc_size root_len = (jc_size)strlen(root);
    jc_size i;
    char *result;
    int any = 0;
    /* M140: the scan reads every candidate file in full. A build-local arena
     * keeps those texts (and the walk's path list) out of the session arena,
     * which used to retain ~the whole source tree for the process lifetime;
     * only the bounded rendered map survives (on app->arena, via the caller
     * or the strdup below). */
    struct jc_arena *ba = jc_arena_new(0);

    if (ba == NULL) {
        return NULL;
    }
    jc_vec_init(&files, sizeof(char *));
    rm_walk(root, &files, ba, &app->config.ignore_dirs);
    if (files.len == 0) {
        jc_vec_free(&files);
        jc_arena_free(ba);
        return NULL;
    }
    qsort(files.data, files.len, sizeof(char *), path_cmp);

    jc_sb_init(&sb);
    for (i = 0; i < files.len; i++) {
        const char *full = *(char **)jc_vec_at(&files, i);
        const char *rel = full;
        struct jc_vec syms;
        struct jc_sb line;
        char *text = NULL;
        jc_size j;

        if (strncmp(full, root, root_len) == 0 && full[root_len] == '/') {
            rel = full + root_len + 1;
        }
        jc_vec_init(&syms, sizeof(char *));
        if (jc_is_regular_file(full) && /* M198: skip FIFO/socket/device */
            jc_read_file(full, &text, NULL, ba) == JC_OK) {
            jc_repomap_scan(path_ext(full), text, &syms);
        }
        jc_sb_init(&line);
        jc_sb_append(&line, rel);
        for (j = 0; j < syms.len && (int)j < JC_REPOMAP_FILE_SYMS; j++) {
            char *s = *(char **)jc_vec_at(&syms, j);
            jc_sb_append(&line, j == 0 ? ": " : ", ");
            jc_sb_append(&line, s);
        }
        if (syms.len > JC_REPOMAP_FILE_SYMS) jc_sb_append(&line, ", ...");
        jc_sb_append(&line, "\n");
        for (j = 0; j < syms.len; j++) free(*(char **)jc_vec_at(&syms, j));
        jc_vec_free(&syms);

        if (sb.len + (line.data != NULL ? line.len : 0) > limit) {
            jc_sb_append_fmt(&sb, "... (truncated; %lu more files)\n",
                             (unsigned long)(files.len - i));
            jc_sb_free(&line);
            break;
        }
        if (line.data != NULL) jc_sb_append(&sb, line.data);
        jc_sb_free(&line);
        any = 1;
    }
    jc_vec_free(&files);
    jc_arena_free(ba); /* releases every scanned file text + the path list */

    /* Scratch, not the session arena: jc_repomap_build re-copies the body it
     * keeps, and build_dir's @folder: callers are per-turn -- so repeated
     * /map + @folder: no longer accumulate listings for the session. */
    result = any ? jc_arena_strdup(jc_app_scratch(app), sb.data) : NULL;
    jc_sb_free(&sb);
    return result;
}

char *jc_repomap_render(struct jc_app *app)
{
    struct jc_sb sb;
    jc_size limit;
    char *body;

    limit = (app->config.repo_map_limit > 0)
                ? (jc_size)app->config.repo_map_limit : JC_REPOMAP_MAX;
    body = render_listing(app, app->cwd, limit);
    if (body == NULL) {
        return NULL;
    }
    jc_sb_init(&sb);
    jc_sb_append(&sb,
        "## Repository map\n"
        "A high-level index of the project's source files and their top-level "
        "symbols, to help you navigate. It is heuristic and may be incomplete; "
        "use read_file / find_definition / search_code for detail.\n\n");
    jc_sb_append(&sb, body);
    return jc_sb_finish(&sb);
}

char *jc_repomap_build(struct jc_app *app)
{
    char *rendered = jc_repomap_render(app);
    char *result;

    if (rendered == NULL) {
        return NULL;
    }
    result = jc_arena_strdup(app->arena, rendered);
    free(rendered);
    return result;
}

char *jc_repomap_build_dir(struct jc_app *app, const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        dir = app->cwd;
    }
    return render_listing(app, dir, JC_REPOMAP_MAX);
}
