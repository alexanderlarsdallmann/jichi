/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_rss.c - RSS 2.0 / Atom feed -> plain text (see jc_rss.h). */

#include "jc_rss.h"
#include "jc_docs.h" /* jc_docs_html_to_text (entity/HTML decode) */
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

#define RSS_MAX_ITEMS   40
#define RSS_DESC_MAX  1200 /* per-item description snippet cap (bytes) */

/* Case-insensitive prefix test. */
static int ci_prefix(const char *s, const char *pfx)
{
    while (*pfx != '\0') {
        char a = *s;
        char b = *pfx;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        s++;
        pfx++;
    }
    return 1;
}

/* Case-insensitive search for `needle` in [start, end); NULL if absent. */
static const char *ci_find(const char *start, const char *end,
                           const char *needle)
{
    const char *p;
    jc_size nlen = (jc_size)strlen(needle);
    if (start == NULL || end == NULL || nlen == 0) return NULL;
    for (p = start; p + nlen <= end; p++) {
        if (ci_prefix(p, needle)) return p;
    }
    return NULL;
}

/* Trim leading/trailing ASCII whitespace of [*s, *e). */
static void trim(const char **s, const char **e)
{
    while (*s < *e && (**s == ' ' || **s == '\t' || **s == '\r' ||
                       **s == '\n')) (*s)++;
    while (*e > *s) {
        char c = (*e)[-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') (*e)--;
        else break;
    }
}

/* Extract the first <tag ...>inner</tag> within [start, end): the decoded inner
 * text is appended to `dst`. CDATA is unwrapped; entities + any nested HTML are
 * decoded via jc_docs_html_to_text. When the inner text is empty and the open
 * tag carries an href="..." (Atom <link>), that href is used instead. Returns 1
 * if the element was found. */
static int field(const char *start, const char *end, const char *tag,
                 struct jc_sb *dst)
{
    char open[24];
    const char *o;
    const char *gt;
    const char *inner;
    const char *inner_end;
    const char *close;
    char closebuf[24];
    struct jc_sb tmp;
    const char *is;
    const char *ie;

    /* "<tag" then a delimiter (space, >, /) so <link> != <linkfoo>. */
    if (strlen(tag) + 2 >= sizeof(open)) return 0;
    open[0] = '<';
    strcpy(open + 1, tag);
    o = ci_find(start, end, open);
    while (o != NULL) {
        char after = o[1 + (int)strlen(tag)];
        if (after == ' ' || after == '>' || after == '\t' || after == '/' ||
            after == '\r' || after == '\n') {
            break;
        }
        o = ci_find(o + 1, end, open);
    }
    if (o == NULL) return 0;
    gt = o;
    while (gt < end && *gt != '>') gt++;
    if (gt >= end) return 0;
    inner = gt + 1;
    /* A self-closing element (<link .../>) has no inner text; a non-self-closing
     * one without a matching close within the block also yields no usable inner
     * (don't swallow the rest of the item). Both fall through to the href probe. */
    if (gt > o && gt[-1] == '/') {
        close = NULL;
        inner_end = inner;
    } else {
        closebuf[0] = '<';
        closebuf[1] = '/';
        strcpy(closebuf + 2, tag);
        close = ci_find(inner, end, closebuf);
        inner_end = (close != NULL) ? close : inner;
    }

    is = inner;
    ie = inner_end;
    trim(&is, &ie);
    /* Unwrap CDATA. */
    if (ie - is >= 12 && ci_prefix(is, "<![CDATA[")) {
        is += 9;
        if (ie - is >= 3 && strncmp(ie - 3, "]]>", 3) == 0) ie -= 3;
        trim(&is, &ie);
    }

    if (ie > is) {
        jc_sb_init(&tmp);
        jc_sb_append_n(&tmp, is, (jc_size)(ie - is));
        jc_docs_html_to_text(tmp.data != NULL ? tmp.data : "", dst);
        jc_sb_free(&tmp);
        return 1;
    }
    /* Empty inner (Atom self-closing <link href="..."/>): pull href from the
     * open tag [o, gt]. */
    {
        const char *h = ci_find(o, gt, "href=");
        if (h != NULL) {
            const char *v = h + 5;
            char q;
            if (v < gt && (*v == '"' || *v == '\'')) {
                const char *ve;
                q = *v++;
                ve = v;
                while (ve < gt && *ve != q) ve++;
                if (ve > v) {
                    jc_sb_append_n(dst, v, (jc_size)(ve - v));
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Append `field` if present, prefixed by `prefix`, else nothing. */
static int emit_field(const char *start, const char *end, const char *tag,
                      const char *prefix, struct jc_sb *out)
{
    struct jc_sb v;
    int got;
    jc_sb_init(&v);
    got = field(start, end, tag, &v);
    if (got && v.len > 0) {
        jc_sb_append(out, prefix);
        if (v.len > RSS_DESC_MAX) {
            jc_sb_append_n(out, v.data, RSS_DESC_MAX);
            jc_sb_append(out, " ...");
        } else {
            jc_sb_append_n(out, v.data, v.len);
        }
    }
    jc_sb_free(&v);
    return got;
}

int jc_rss_looks_like_feed(const char *xml)
{
    const char *p;
    const char *end;
    jc_size scan;
    if (xml == NULL) return 0;
    scan = (jc_size)strlen(xml);
    if (scan > 4096) scan = 4096; /* only look near the top */
    end = xml + scan;
    for (p = xml; p < end; p++) {
        if (*p == '<') {
            if (ci_prefix(p, "<rss") || ci_prefix(p, "<feed") ||
                ci_prefix(p, "<rdf") || ci_prefix(p, "<item") ||
                ci_prefix(p, "<entry")) {
                return 1;
            }
        }
    }
    return 0;
}

void jc_rss_to_text(const char *xml, struct jc_sb *out)
{
    const char *end;
    const char *p;
    int n = 0;

    if (xml == NULL || out == NULL) return;
    end = xml + strlen(xml);

    /* Optional feed title: the first <title> that precedes any item. */
    {
        const char *first_item = ci_find(xml, end, "<item");
        const char *first_entry = ci_find(xml, end, "<entry");
        const char *body_start = first_item;
        if (first_entry != NULL &&
            (body_start == NULL || first_entry < body_start)) {
            body_start = first_entry;
        }
        if (body_start != NULL && body_start > xml) {
            struct jc_sb ft;
            jc_sb_init(&ft);
            if (field(xml, body_start, "title", &ft) && ft.len > 0) {
                jc_sb_append(out, "# Feed: ");
                jc_sb_append_n(out, ft.data, ft.len);
                jc_sb_append(out, "\n\n");
            }
            jc_sb_free(&ft);
        }
    }

    p = xml;
    while (n < RSS_MAX_ITEMS) {
        const char *it = ci_find(p, end, "<item");
        const char *en = ci_find(p, end, "<entry");
        const char *open;
        const char *close;
        const char *item_end;
        const char *closetag;
        int has_title;

        if (it != NULL && (en == NULL || it < en)) {
            open = it; closetag = "</item";
        } else if (en != NULL) {
            open = en; closetag = "</entry";
        } else {
            break;
        }
        /* Guard against <item> vs <itemfoo>. */
        {
            char after = open[(open == it) ? 5 : 6];
            if (after != ' ' && after != '>' && after != '\t' &&
                after != '/' && after != '\r' && after != '\n') {
                p = open + 1;
                continue;
            }
        }
        close = ci_find(open, end, closetag);
        item_end = (close != NULL) ? close : end;

        jc_sb_append(out, "- ");
        has_title = emit_field(open, item_end, "title", "", out);
        if (!has_title) {
            jc_sb_append(out, "(untitled)");
        }
        jc_sb_append(out, "\n");
        /* date: RSS pubDate, else Atom updated/published. */
        if (!emit_field(open, item_end, "pubDate", "  date: ", out)) {
            if (!emit_field(open, item_end, "updated", "  date: ", out)) {
                emit_field(open, item_end, "published", "  date: ", out);
            }
        }
        jc_sb_append(out, "");
        if (emit_field(open, item_end, "link", "\n  link: ", out)) {
            /* linked */
        }
        jc_sb_append(out, "\n");
        /* body: RSS description, else Atom content/summary. */
        if (!emit_field(open, item_end, "description", "  ", out)) {
            if (!emit_field(open, item_end, "content", "  ", out)) {
                emit_field(open, item_end, "summary", "  ", out);
            }
        }
        jc_sb_append(out, "\n\n");

        p = item_end + 1;
        n++;
    }
}
