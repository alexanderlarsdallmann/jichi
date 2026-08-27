/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_testparse.c - pure test-output parser. See include/jc_testparse.h.
 *
 * Pure string processing only: no I/O, no process, no network. JUnit-XML and
 * TAP are parsed structurally; everything else (and, as a backstop, the
 * messages extracted from the structured formats) goes through a generic
 * heuristic line scan that harvests "<file>:<line>" locations and failure
 * markers. Output is bounded by JC_TEST_MAX_FAILURES / JC_TEST_MAX_MSG.
 */

#include "jc_testparse.h"

#include <stdlib.h>
#include <string.h>

struct lr { const char *s; const char *e; };

static int dig(int c) { return c >= '0' && c <= '9'; }
static int spc(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* Copy [s,e) into dst, trimming surrounding whitespace, capped at cap-1. */
static void copy_range(char *dst, jc_size cap, const char *s, const char *e)
{
    jc_size n;
    while (s < e && spc((unsigned char)*s)) s++;
    while (e > s && spc((unsigned char)e[-1])) e--;
    n = (jc_size)(e - s);
    if (n > cap - 1) n = cap - 1;
    if (n > 0) memcpy(dst, s, n);
    dst[n] = '\0';
}

/* Like copy_range but decodes the five standard XML entities while copying. */
static void copy_xml(char *dst, jc_size cap, const char *s, const char *e)
{
    jc_size o = 0;
    while (s < e && spc((unsigned char)*s)) s++;
    while (e > s && spc((unsigned char)e[-1])) e--;
    while (s < e && o < cap - 1) {
        if (*s == '&') {
            if (e - s >= 4 && strncmp(s, "&lt;", 4) == 0) {
                dst[o++] = '<'; s += 4; continue;
            }
            if (e - s >= 4 && strncmp(s, "&gt;", 4) == 0) {
                dst[o++] = '>'; s += 4; continue;
            }
            if (e - s >= 5 && strncmp(s, "&amp;", 5) == 0) {
                dst[o++] = '&'; s += 5; continue;
            }
            if (e - s >= 6 && strncmp(s, "&quot;", 6) == 0) {
                dst[o++] = '"'; s += 6; continue;
            }
            if (e - s >= 6 && strncmp(s, "&apos;", 6) == 0) {
                dst[o++] = '\''; s += 6; continue;
            }
        }
        dst[o++] = *s++;
    }
    dst[o] = '\0';
}

/* Find a "<file>:<line>" location in NUL-terminated s. The file token must
 * look path-like (contain '.' or '/'). Returns 1 and fills file/line on the
 * first match, else 0. */
static int find_loc(const char *s, char *file, jc_size cap, long *line_out)
{
    const char *p = s;
    while (*p != '\0') {
        if (*p == ':' && dig((unsigned char)p[1])) {
            const char *fe = p;
            const char *fs = p;
            const char *d;
            const char *t;
            long n;
            int pathlike = 0;

            while (fs > s && !spc((unsigned char)fs[-1]) &&
                   fs[-1] != ':' && fs[-1] != '(') {
                fs--;
            }
            for (t = fs; t < fe; t++) {
                if (*t == '.' || *t == '/') { pathlike = 1; break; }
            }
            if (pathlike && fe > fs) {
                d = p + 1;
                n = 0;
                while (dig((unsigned char)*d)) { n = n * 10 + (*d - '0'); d++; }
                copy_range(file, cap, fs, fe);
                *line_out = n;
                return 1;
            }
        }
        p++;
    }
    return 0;
}

/* Append a failure to the report, duplicating its strings and capping the
 * message length. Beyond JC_TEST_MAX_FAILURES, count toward `truncated`. */
static void add_failure(struct jc_test_report *r, const char *name,
                        const char *file, long line, const char *message)
{
    struct jc_test_failure f;
    char mbuf[JC_TEST_MAX_MSG];

    if (r->failures.len >= JC_TEST_MAX_FAILURES) {
        r->truncated++;
        return;
    }
    f.name = (name != NULL && name[0] != '\0') ? jc_strdup(name) : NULL;
    f.file = (file != NULL && file[0] != '\0') ? jc_strdup(file) : NULL;
    f.line = line;
    f.message = NULL;
    if (message != NULL && message[0] != '\0') {
        copy_range(mbuf, sizeof mbuf, message, message + strlen(message));
        if (mbuf[0] != '\0') f.message = jc_strdup(mbuf);
    }
    jc_vec_push(&r->failures, &f);
}

static void split_lines(const char *s, struct jc_vec *out)
{
    const char *p = s;
    const char *start = s;
    for (;;) {
        if (*p == '\n' || *p == '\0') {
            struct lr line;
            line.s = start;
            line.e = p;
            if (line.e > line.s && line.e[-1] == '\r') line.e--;
            jc_vec_push(out, &line);
            if (*p == '\0') break;
            start = p + 1;
        }
        p++;
    }
}

/* ---- JUnit-XML ---------------------------------------------------------- */

/* Read attribute `name` from within the opening tag [ts,te) (decoding
 * entities) into dst. Returns 1 if found. */
static int xml_attr(const char *ts, const char *te, const char *name,
                    char *dst, jc_size cap)
{
    jc_size nl = (jc_size)strlen(name);
    const char *p = ts;
    while (p < te) {
        if ((p == ts || spc((unsigned char)p[-1])) &&
            (jc_size)(te - p) > nl &&
            strncmp(p, name, nl) == 0 && p[nl] == '=') {
            const char *v = p + nl + 1;
            const char *ve;
            char q = '"';
            if (*v == '"' || *v == '\'') { q = *v; v++; }
            ve = v;
            while (ve < te && *ve != q) ve++;
            copy_xml(dst, cap, v, ve);
            return 1;
        }
        p++;
    }
    dst[0] = '\0';
    return 0;
}

static void parse_junit(const char *output, struct jc_test_report *r)
{
    const char *p = output;
    const char *end = output + strlen(output);
    int total = 0;
    int failed = 0;

    while ((p = strstr(p, "<testcase")) != NULL) {
        const char *tagend = strchr(p, '>');
        const char *body;
        const char *close;
        const char *scope;
        const char *fp;
        int selfclose;

        total++;
        if (tagend == NULL) break;
        selfclose = (tagend > p && tagend[-1] == '/');
        body = tagend + 1;
        if (selfclose) { p = body; continue; }

        close = strstr(body, "</testcase>");
        scope = (close != NULL) ? close : end;

        fp = strstr(body, "<failure");
        if (fp == NULL || fp >= scope) fp = strstr(body, "<error");
        if (fp != NULL && fp < scope) {
            char name[256];
            char cls[256];
            char msg[JC_TEST_MAX_MSG];
            char inner[JC_TEST_MAX_MSG];
            char nm[512];
            char file[1024];
            const char *ftagend = strchr(fp, '>');
            long line = 0;

            xml_attr(p, tagend, "name", name, sizeof name);
            xml_attr(p, tagend, "classname", cls, sizeof cls);
            msg[0] = '\0';
            inner[0] = '\0';
            if (ftagend != NULL && ftagend < scope) {
                const char *ic = strstr(ftagend, "</failure>");
                if (ic == NULL || ic > scope) ic = strstr(ftagend, "</error>");
                if (ic != NULL && ic <= scope) {
                    copy_xml(inner, sizeof inner, ftagend + 1, ic);
                }
                xml_attr(fp, ftagend, "message", msg, sizeof msg);
            }
            if (msg[0] == '\0') {
                memcpy(msg, inner, sizeof inner);
            }
            /* Qualify the name with its class when both are present. */
            if (cls[0] != '\0' && name[0] != '\0') {
                jc_size a = (jc_size)strlen(cls);
                jc_size b = (jc_size)strlen(name);
                if (a + b + 2 < sizeof nm) {
                    memcpy(nm, cls, a);
                    nm[a] = '.';
                    memcpy(nm + a + 1, name, b + 1);
                } else {
                    copy_range(nm, sizeof nm, name, name + b);
                }
            } else {
                const char *src = name[0] != '\0' ? name : cls;
                copy_range(nm, sizeof nm, src, src + strlen(src));
            }
            file[0] = '\0';
            if (!(msg[0] != '\0' && find_loc(msg, file, sizeof file, &line)) &&
                inner[0] != '\0') {
                find_loc(inner, file, sizeof file, &line);
            }
            add_failure(r, nm, file, line, msg);
            failed++;
        }
        p = (close != NULL) ? close + 11 : scope; /* 11 = strlen("</testcase>") */
    }

    r->total = total;
    r->failed = failed;
    r->passed = total - failed;
}

/* ---- TAP ---------------------------------------------------------------- */

static int is_tap(const char *s)
{
    const char *p = s;
    int atbol = 1;
    while (*p != '\0') {
        if (atbol) {
            const char *q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (strncmp(q, "not ok", 6) == 0) return 1;
            if (strncmp(q, "ok ", 3) == 0 && dig((unsigned char)q[3])) return 1;
            if (dig((unsigned char)*q)) {
                const char *d = q;
                while (dig((unsigned char)*d)) d++;
                if (d[0] == '.' && d[1] == '.' && dig((unsigned char)d[2])) {
                    return 1;
                }
            }
        }
        atbol = (*p == '\n');
        p++;
    }
    return 0;
}

static void parse_tap(const char *output, struct jc_test_report *r)
{
    struct jc_vec lines;
    jc_size i;
    int pass = 0;
    int fail = 0;
    int total = -1;

    jc_vec_init(&lines, sizeof(struct lr));
    split_lines(output, &lines);
    for (i = 0; i < lines.len; i++) {
        struct lr *L = (struct lr *)jc_vec_at(&lines, i);
        char buf[4096];

        copy_range(buf, sizeof buf, L->s, L->e);
        if (buf[0] == '\0') continue;

        if (dig((unsigned char)buf[0])) {
            const char *d = buf;
            while (dig((unsigned char)*d)) d++;
            if (d[0] == '.' && d[1] == '.') {
                long m = 0;
                d += 2;
                while (dig((unsigned char)*d)) { m = m * 10 + (*d - '0'); d++; }
                total = (int)m;
                continue;
            }
        }
        if (strncmp(buf, "not ok", 6) == 0) {
            const char *q = buf + 6;
            const char *hash;
            const char *e;
            char nm[256];
            char file[1024];
            long ln = 0;

            while (*q == ' ') q++;
            while (dig((unsigned char)*q)) q++;
            while (*q == ' ') q++;
            if (*q == '-') { q++; while (*q == ' ') q++; }
            hash = strstr(q, " #");
            e = (hash != NULL) ? hash : (q + strlen(q));
            copy_range(nm, sizeof nm, q, e);
            file[0] = '\0';
            if (find_loc(buf, file, sizeof file, &ln)) {
                add_failure(r, nm, file, ln, buf);
            } else {
                add_failure(r, nm, NULL, 0, buf);
            }
            fail++;
        } else if (strncmp(buf, "ok", 2) == 0 &&
                   (buf[2] == ' ' || buf[2] == '\0')) {
            pass++;
        }
    }
    r->passed = pass;
    r->failed = fail;
    r->total = (total < 0) ? (pass + fail) : total;
    jc_vec_free(&lines);
}

/* ---- generic ------------------------------------------------------------ */

static int strong_marker(const char *line)
{
    return strstr(line, "FAILED") != NULL || strstr(line, "--- FAIL") != NULL ||
           strstr(line, "FAIL:") != NULL || strstr(line, "not ok") != NULL;
}

static int err_keyword(const char *line)
{
    return strstr(line, "error:") != NULL || strstr(line, "Error") != NULL ||
           strstr(line, "assert") != NULL || strstr(line, "Assert") != NULL ||
           strstr(line, "panic:") != NULL || strstr(line, "Exception") != NULL ||
           strstr(line, "fatal:") != NULL ||
           strstr(line, "undefined reference") != NULL;
}

static void extract_name(const char *line, char *dst, jc_size cap)
{
    const char *p;
    const char *e;

    dst[0] = '\0';
    if ((p = strstr(line, "--- FAIL:")) != NULL) {
        p += 9;
        while (*p == ' ') p++;
        e = p;
        while (*e != '\0' && *e != '(') e++;
        copy_range(dst, cap, p, e);
        return;
    }
    if ((p = strstr(line, "FAILED ")) != NULL) {
        p += 7;
        e = p;
        while (*e != '\0' && *e != ' ') e++;
        copy_range(dst, cap, p, e);
        return;
    }
    if ((p = strstr(line, "FAIL: ")) != NULL) {
        p += 6;
        e = p;
        while (*e != '\0' && *e != ' ' && *e != '(') e++;
        copy_range(dst, cap, p, e);
        return;
    }
}

/* Read the integer ending just before `kw` (which points at a separating
 * space), or -1. */
static int num_before(const char *base, const char *kw)
{
    const char *p = kw;
    long n = 0;
    long mul = 1;
    int got = 0;

    while (p > base && p[-1] == ' ') p--;
    while (p > base && dig((unsigned char)p[-1])) {
        n += (long)(p[-1] - '0') * mul;
        mul *= 10;
        p--;
        got = 1;
    }
    return got ? (int)n : -1;
}

static void scan_counts(const char *output, struct jc_test_report *r)
{
    const char *p;
    const char *q;

    p = output;
    while ((q = strstr(p, " failed")) != NULL) {
        int v = num_before(output, q);
        if (v >= 0) r->failed = v;
        p = q + 7;
    }
    p = output;
    while ((q = strstr(p, " passed")) != NULL) {
        int v = num_before(output, q);
        if (v >= 0) r->passed = v;
        p = q + 7;
    }
    if ((q = strstr(output, "out of ")) != NULL) {
        const char *d = q + 7;
        long n = 0;
        int got = 0;
        while (dig((unsigned char)*d)) { n = n * 10 + (*d - '0'); d++; got = 1; }
        if (got) r->total = (int)n;
    }
    /* "All N tests passed." (Zig) / "N tests" totals: read the number just
     * before a " tests" token. Only when no authoritative total was found, so
     * junit/tap/"out of" counts still win. Keeps the largest (0 is a real,
     * kept signal -- it drives the M86 no-tests warning). */
    if (r->total < 0) {
        int best = -1;
        p = output;
        while ((q = strstr(p, " tests")) != NULL) {
            int v = num_before(output, q);
            if (v > best) best = v;
            p = q + 6;
        }
        if (best >= 0) r->total = best;
    }
    if (r->total < 0 && r->failed >= 0 && r->passed >= 0) {
        r->total = r->failed + r->passed;
    }
}

static void parse_generic(const char *output, struct jc_test_report *r)
{
    struct jc_vec lines;
    jc_size i;

    jc_vec_init(&lines, sizeof(struct lr));
    split_lines(output, &lines);
    for (i = 0; i < lines.len; i++) {
        struct lr *L = (struct lr *)jc_vec_at(&lines, i);
        char line[4096];
        char name[256];
        char file[1024];
        long ln = 0;
        int has_loc;

        copy_range(line, sizeof line, L->s, L->e);
        if (line[0] == '\0') continue;

        file[0] = '\0';
        name[0] = '\0';
        has_loc = find_loc(line, file, sizeof file, &ln);
        if (!(strong_marker(line) || (has_loc && err_keyword(line)))) continue;

        extract_name(line, name, sizeof name);
        if (!has_loc && i + 1 < lines.len) {
            struct lr *N = (struct lr *)jc_vec_at(&lines, i + 1);
            char nbuf[4096];
            copy_range(nbuf, sizeof nbuf, N->s, N->e);
            if (find_loc(nbuf, file, sizeof file, &ln)) has_loc = 1;
        }
        if (!has_loc && name[0] != '\0') {
            const char *cc = strstr(name, "::");
            if (cc != NULL && cc > name) {
                copy_range(file, sizeof file, name, cc);
            }
        }
        add_failure(r, name[0] != '\0' ? name : NULL,
                    file[0] != '\0' ? file : NULL, ln, line);
    }
    jc_vec_free(&lines);
    scan_counts(output, r);
}

/* ---- public API --------------------------------------------------------- */

void jc_test_report_init(struct jc_test_report *r)
{
    jc_vec_init(&r->failures, sizeof(struct jc_test_failure));
    r->passed = -1;
    r->failed = -1;
    r->total = -1;
    r->truncated = 0;
    r->format = "generic";
}

void jc_test_report_free(struct jc_test_report *r)
{
    jc_size i;
    for (i = 0; i < r->failures.len; i++) {
        struct jc_test_failure *f =
            (struct jc_test_failure *)jc_vec_at(&r->failures, i);
        free(f->name);
        free(f->file);
        free(f->message);
    }
    jc_vec_free(&r->failures);
    r->passed = -1;
    r->failed = -1;
    r->total = -1;
    r->truncated = 0;
}

void jc_testparse(const char *output, struct jc_test_report *r)
{
    if (output == NULL || output[0] == '\0') return;

    if (strstr(output, "<testcase") != NULL ||
        strstr(output, "<testsuite") != NULL) {
        r->format = "junit";
        parse_junit(output, r);
    } else if (is_tap(output)) {
        r->format = "tap";
        parse_tap(output, r);
    } else {
        r->format = "generic";
        parse_generic(output, r);
    }
    if (r->failed < 0 && (r->failures.len > 0 || r->truncated > 0)) {
        r->failed = (int)r->failures.len + r->truncated;
    }
}

int jc_test_report_count(const struct jc_test_report *r)
{
    if (r == NULL) return -1;
    if (r->total >= 0) return r->total;
    if (r->passed >= 0 || r->failed >= 0) {
        int p = r->passed >= 0 ? r->passed : 0;
        int f = r->failed >= 0 ? r->failed : 0;
        return p + f;
    }
    return -1;
}

int jc_testparse_render(const struct jc_test_report *r, struct jc_sb *out)
{
    struct jc_vec *fv = (struct jc_vec *)&r->failures;
    jc_size i;
    int n = 0;

    if (r->failed >= 0 || r->passed >= 0 || r->total >= 0) {
        jc_sb_append(out, "Tests:");
        if (r->failed >= 0) jc_sb_append_fmt(out, " %d failed", r->failed);
        if (r->passed >= 0) {
            jc_sb_append_fmt(out, "%s%d passed",
                             r->failed >= 0 ? ", " : " ", r->passed);
        }
        if (r->total >= 0) jc_sb_append_fmt(out, " (of %d)", r->total);
        jc_sb_append(out, "\n");
    }
    if (fv->len > 0) jc_sb_append(out, "Failures:\n");
    for (i = 0; i < fv->len; i++) {
        struct jc_test_failure *f =
            (struct jc_test_failure *)jc_vec_at(fv, i);
        jc_sb_append(out, "- ");
        if (f->name != NULL) jc_sb_append(out, f->name);
        if (f->file != NULL) {
            jc_sb_append(out, f->name != NULL ? " @ " : "@ ");
            jc_sb_append(out, f->file);
            if (f->line > 0) jc_sb_append_fmt(out, ":%ld", f->line);
        }
        if (f->message != NULL) {
            if (f->name != NULL || f->file != NULL) jc_sb_append(out, ": ");
            jc_sb_append(out, f->message);
        }
        if (f->name == NULL && f->file == NULL && f->message == NULL) {
            jc_sb_append(out, "(unknown)");
        }
        jc_sb_append(out, "\n");
        n++;
    }
    if (r->truncated > 0) jc_sb_append_fmt(out, "... and %d more\n", r->truncated);
    return n;
}
