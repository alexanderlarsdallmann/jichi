/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_patch.c - pure string edit primitives (see jc_patch.h). */

#include "jc_patch.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>
#include <stdlib.h>

int jc_patch_count(const char *hay, const char *needle)
{
    int n = 0;
    jc_size nlen;
    const char *p = hay;

    if (hay == NULL || needle == NULL) {
        return 0;
    }
    nlen = (jc_size)strlen(needle);
    if (nlen == 0) {
        return 0;
    }
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += nlen;
    }
    return n;
}

/* M208: the ambiguous-match hint. See the header for the rationale. */
#define PATCH_MATCH_LINES_MAX 8

void jc_patch_matchlines_hint(const char *text, const char *old_s,
                              struct jc_sb *out)
{
    jc_size nlen, line = 1, seen = 0;
    const char *p, *scan, *cur;
    char num[32];
    int shown = 0;

    if (text == NULL || old_s == NULL || out == NULL) {
        return;
    }
    nlen = (jc_size)strlen(old_s);
    if (nlen == 0 || text[0] == '\0') {
        return;
    }

    /* One pass: `scan` trails the search cursor counting newlines, so each
     * match's 1-based line number falls out without re-scanning from the top.
     * Matches are non-overlapping, exactly as jc_patch_count counts them. */
    scan = text;
    cur = text;
    while ((p = strstr(cur, old_s)) != NULL) {
        while (scan < p) {
            if (*scan == '\n') {
                line++;
            }
            scan++;
        }
        seen++;
        if (shown < PATCH_MATCH_LINES_MAX) {
            jc_sb_append(out, shown == 0 ? "\nhint: it matches at line " : ", ");
            jc_snprintf(num, sizeof(num), "%lu", (unsigned long)line);
            jc_sb_append(out, num);
            shown++;
        }
        cur = p + nlen;
    }
    if (shown == 0) {
        return;
    }
    if (seen > (jc_size)shown) {
        jc_sb_append(out, ", ...");
    }
    jc_sb_append(out, " -- extend old_string with a nearby line that differs "
                      "between them (or set replace_all to change all of them)");
}

void jc_patch_build(const char *text, const char *old_s, const char *new_s,
                    int replace_all, struct jc_sb *out)
{
    jc_size olen;
    const char *p;

    if (text == NULL) {
        return;
    }
    if (old_s == NULL || old_s[0] == '\0') {
        jc_sb_append(out, text);
        return;
    }
    olen = (jc_size)strlen(old_s);
    p = text;
    for (;;) {
        const char *hit = strstr(p, old_s);
        if (hit == NULL) {
            jc_sb_append(out, p);
            break;
        }
        jc_sb_append_n(out, p, (jc_size)(hit - p));
        jc_sb_append(out, new_s != NULL ? new_s : "");
        p = hit + olen;
        if (!replace_all) {
            jc_sb_append(out, p);
            break;
        }
    }
}

const char *jc_patch_strategy_name(enum jc_patch_strategy s)
{
    switch (s) {
    case JC_PATCH_EXACT:  return "exact";
    case JC_PATCH_WS:     return "whitespace-insensitive";
    case JC_PATCH_ANCHOR: return "anchored";
    default:              return "none";
}
}

/* --- Fuzzy (line-oriented) fallback matching (M38) -------------------------
 *
 * When an exact match fails, real edits usually differ only in horizontal
 * whitespace (indentation / trailing space) or line endings, or a single
 * interior line was misquoted. We match at line granularity, trimming each
 * line's leading/trailing whitespace (which also absorbs '\r'), and require a
 * unique hit so we never silently edit the wrong place. The replaced byte range
 * is computed in the ORIGINAL text so the surrounding bytes are preserved
 * exactly. */

static int patch_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* Split `t` (length `tlen`) into lines. Allocates two parallel arrays via
 * malloc: off[i] = byte offset of line i's first char; end[i] = byte offset of
 * its terminating '\n' (or tlen for the final line). Line count = number of
 * '\n' + 1 (so "a\n" -> ["a",""], "" -> [""]). Returns the count, or 0 on OOM
 * (with both out-arrays left NULL). */
static jc_size patch_lines(const char *t, jc_size tlen, jc_size **off,
                           jc_size **end)
{
    jc_size n = 1, i, li = 0, start = 0;
    jc_size *o, *e;

    *off = NULL;
    *end = NULL;
    for (i = 0; i < tlen; i++) {
        if (t[i] == '\n') {
            n++;
        }
    }
    o = (jc_size *)malloc(n * sizeof(jc_size));
    e = (jc_size *)malloc(n * sizeof(jc_size));
    if (o == NULL || e == NULL) {
        free(o);
        free(e);
        return 0;
    }
    for (i = 0; i < tlen; i++) {
        if (t[i] == '\n') {
            o[li] = start;
            e[li] = i;
            li++;
            start = i + 1;
        }
    }
    o[li] = start;
    e[li] = tlen;
    *off = o;
    *end = e;
    return n;
}

/* Compare two line content ranges for equality after trimming leading/trailing
 * whitespace from each. */
static int patch_line_eq(const char *a, jc_size as, jc_size ae,
                         const char *b, jc_size bs, jc_size be)
{
    while (as < ae && patch_is_ws(a[as])) as++;
    while (ae > as && patch_is_ws(a[ae - 1])) ae--;
    while (bs < be && patch_is_ws(b[bs])) bs++;
    while (be > bs && patch_is_ws(b[be - 1])) be--;
    if (ae - as != be - bs) {
        return 0;
    }
    return ae == as || memcmp(a + as, b + bs, ae - as) == 0;
}

/* True when line content range [s,e) is whitespace-only. */
static int patch_line_blank(const char *t, jc_size s, jc_size e)
{
    while (s < e && patch_is_ws(t[s])) s++;
    return s == e;
}

/* Resolve a unique whitespace-insensitive / anchored line match of `needle` in
 * `hay`. On a unique hit writes the original-text byte range [*ms, *ms+*ml) and
 * returns JC_PATCH_WS or JC_PATCH_ANCHOR; returns JC_PATCH_AMBIGUOUS (with
 * *nmatch set) when a tier matched more than once, or JC_PATCH_NONE. */
static enum jc_patch_strategy patch_locate_fuzzy(const char *hay, jc_size hlen,
                                                 const char *needle,
                                                 jc_size nlen, jc_size *ms,
                                                 jc_size *ml, int *nmatch)
{
    jc_size *hoff = NULL, *hend = NULL, *noff = NULL, *nend = NULL;
    jc_size hn, nn, nN, i, j, last = 0;
    int had_nl, found, fa, la;
    enum jc_patch_strategy result = JC_PATCH_NONE;

    *nmatch = 0;
    hn = patch_lines(hay, hlen, &hoff, &hend);
    nn = patch_lines(needle, nlen, &noff, &nend);
    if (hn == 0 || nn == 0) {
        goto done;
    }
    /* Drop a single trailing empty line so "x\n" matches as one line "x" but we
     * remember the newline was intended (so the replacement consumes it too). */
    nN = nn;
    had_nl = 0;
    if (nN > 1 && noff[nN - 1] == nend[nN - 1]) {
        nN--;
        had_nl = 1;
    }
    if (nN == 0 || nN > hn) {
        goto done;
    }

    /* Tier 1: whitespace-insensitive, every line. */
    found = 0;
    for (i = 0; i + nN <= hn; i++) {
        int ok = 1;
        for (j = 0; j < nN; j++) {
            if (!patch_line_eq(hay, hoff[i + j], hend[i + j],
                               needle, noff[j], nend[j])) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            found++;
            *ms = hoff[i];
            last = i + nN - 1;
        }
    }
    if (found >= 1) {
        result = (found == 1) ? JC_PATCH_WS : JC_PATCH_AMBIGUOUS;
        *nmatch = found;
        goto finish;
    }

    /* Tier 2: anchor on the first + last non-blank needle lines. */
    fa = -1;
    la = -1;
    for (j = 0; j < (jc_size)nN; j++) {
        if (!patch_line_blank(needle, noff[j], nend[j])) {
            if (fa < 0) {
                fa = (int)j;
            }
            la = (int)j;
        }
    }
    if (fa < 0) {
        goto done; /* all-blank needle: nothing to anchor on */
    }
    found = 0;
    for (i = 0; i + nN <= hn; i++) {
        if (patch_line_eq(hay, hoff[i + (jc_size)fa], hend[i + (jc_size)fa],
                          needle, noff[fa], nend[fa]) &&
            patch_line_eq(hay, hoff[i + (jc_size)la], hend[i + (jc_size)la],
                          needle, noff[la], nend[la])) {
            found++;
            *ms = hoff[i];
            last = i + nN - 1;
        }
    }
    if (found >= 1) {
        result = (found == 1) ? JC_PATCH_ANCHOR : JC_PATCH_AMBIGUOUS;
        *nmatch = found;
    }

finish:
    if (result == JC_PATCH_WS || result == JC_PATCH_ANCHOR) {
        jc_size end_byte;
        if (had_nl) {
            end_byte = (hend[last] < hlen) ? hend[last] + 1 : hlen;
        } else {
            end_byte = hend[last];
        }
        *ml = end_byte - *ms;
    }
done:
    free(hoff);
    free(hend);
    free(noff);
    free(nend);
    return result;
}

enum jc_patch_strategy jc_patch_apply(const char *text, const char *old_s,
                                      const char *new_s, int replace_all,
                                      int fuzzy, struct jc_sb *out,
                                      int *nmatches)
{
    int count;

    if (nmatches != NULL) {
        *nmatches = 0;
    }
    if (text == NULL || old_s == NULL || old_s[0] == '\0') {
        return JC_PATCH_NONE;
    }
    count = jc_patch_count(text, old_s);
    if (count >= 1) {
        if (replace_all) {
            jc_patch_build(text, old_s, new_s, 1, out);
            if (nmatches != NULL) *nmatches = count;
            return JC_PATCH_EXACT;
        }
        if (count == 1) {
            jc_patch_build(text, old_s, new_s, 0, out);
            if (nmatches != NULL) *nmatches = 1;
            return JC_PATCH_EXACT;
        }
        if (nmatches != NULL) *nmatches = count; /* not unique */
        return JC_PATCH_AMBIGUOUS;
    }
    /* Exact not found. Fuzzy is single-replacement only. */
    if (!fuzzy || replace_all) {
        return JC_PATCH_NONE;
    }
    {
        jc_size ms = 0, ml = 0;
        int fm = 0;
        enum jc_patch_strategy s =
            patch_locate_fuzzy(text, (jc_size)strlen(text), old_s,
                               (jc_size)strlen(old_s), &ms, &ml, &fm);
        if (s == JC_PATCH_WS || s == JC_PATCH_ANCHOR) {
            jc_sb_append_n(out, text, ms);
            jc_sb_append(out, new_s != NULL ? new_s : "");
            jc_sb_append(out, text + ms + ml);
            if (nmatches != NULL) *nmatches = 1;
            return s;
        }
        if (nmatches != NULL) *nmatches = fm;
        return s; /* AMBIGUOUS or NONE */
    }
}

/* --- Near-match hint: help a not-found old_string self-correct (harden) ----- */

/* Token character: alphanumeric or underscore. */
static int patch_is_tok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* Does byte range [ls,le) of `t` contain the `tl`-byte token `tok` as a substring? */
static int patch_range_has(const char *t, jc_size ls, jc_size le,
                           const char *tok, jc_size tl)
{
    jc_size i;
    if (tl == 0 || le < ls || le - ls < tl) {
        return 0;
    }
    for (i = ls; i + tl <= le; i++) {
        if (memcmp(t + i, tok, tl) == 0) {
            return 1;
        }
    }
    return 0;
}

#define PATCH_HINT_MAX_TOKENS 24
#define PATCH_HINT_CTX        2   /* lines of context each side of the best line */
#define PATCH_HINT_MAX_COL    160 /* truncate each shown line to this many bytes */

void jc_patch_nearmatch_hint(const char *text, const char *old_s,
                             struct jc_sb *out)
{
    jc_size tlen, olen, i, as = 0, ae = 0;
    jc_size *off = NULL, *end = NULL, nlines;
    jc_size tok_s[PATCH_HINT_MAX_TOKENS];
    jc_size tok_l[PATCH_HINT_MAX_TOKENS];
    int ntok = 0, best_score = 0;
    jc_size best_i = 0, lo, hi, k;

    if (text == NULL || old_s == NULL || out == NULL) {
        return;
    }
    tlen = (jc_size)strlen(text);
    olen = (jc_size)strlen(old_s);
    if (tlen == 0 || olen == 0) {
        return;
    }

    /* Anchor = the first non-blank line of old_s (trimmed). */
    i = 0;
    while (i < olen) {
        jc_size ls = i, le, a, b;
        while (i < olen && old_s[i] != '\n') {
            i++;
        }
        le = i;
        if (i < olen) {
            i++;
        }
        a = ls;
        b = le;
        while (a < b && (old_s[a] == ' ' || old_s[a] == '\t' ||
                         old_s[a] == '\r')) {
            a++;
        }
        while (b > a && (old_s[b - 1] == ' ' || old_s[b - 1] == '\t' ||
                         old_s[b - 1] == '\r')) {
            b--;
        }
        if (b > a) {
            as = a;
            ae = b;
            break;
        }
    }
    if (ae <= as) {
        return;
    }

    /* Significant tokens (>= 3 chars) within the anchor line. */
    i = as;
    while (i < ae && ntok < PATCH_HINT_MAX_TOKENS) {
        jc_size s;
        while (i < ae && !patch_is_tok(old_s[i])) {
            i++;
        }
        s = i;
        while (i < ae && patch_is_tok(old_s[i])) {
            i++;
        }
        if (i - s >= 3) {
            tok_s[ntok] = s;
            tok_l[ntok] = i - s;
            ntok++;
        }
    }
    if (ntok == 0) {
        return;
    }

    /* Score every line by shared-token count; keep the best. */
    nlines = patch_lines(text, tlen, &off, &end);
    if (nlines == 0) {
        return;
    }
    for (i = 0; i < nlines; i++) {
        int score = 0, j;
        for (j = 0; j < ntok; j++) {
            if (patch_range_has(text, off[i], end[i],
                                old_s + tok_s[j], tok_l[j])) {
                score++;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_i = i;
        }
    }
    if (best_score == 0) {
        free(off);
        free(end);
        return;
    }

    /* Emit a numbered excerpt around the best-matching line. */
    lo = (best_i > (jc_size)PATCH_HINT_CTX) ? best_i - PATCH_HINT_CTX : 0;
    hi = best_i + PATCH_HINT_CTX;
    if (hi >= nlines) {
        hi = nlines - 1;
    }
    jc_sb_append(out, "\nhint: the most similar text in the file is here -- "
                      "check your old_string against these exact bytes "
                      "(indentation included):\n");
    for (k = lo; k <= hi; k++) {
        char num[32];
        jc_size ll = end[k] - off[k];
        int over = (ll > (jc_size)PATCH_HINT_MAX_COL);
        if (over) {
            ll = (jc_size)PATCH_HINT_MAX_COL;
        }
        jc_snprintf(num, sizeof(num), "  %lu| ", (unsigned long)(k + 1));
        jc_sb_append(out, num);
        jc_sb_append_n(out, text + off[k], ll);
        if (over) {
            jc_sb_append(out, " ...");
        }
        jc_sb_append_char(out, '\n');
    }
    free(off);
    free(end);
}
