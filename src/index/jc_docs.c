/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_docs.c - external documentation retrieval (see jc_docs.h).
 *
 * A thin sibling of jc_search: it builds an embeddings index over a *named,
 * arbitrary directory* (a `docs` config source) rather than the workspace, ranks
 * the chunks against a query (cosine + optional rerank), formats the hits, and
 * frees the index before returning. The disk cache under
 * ~/.jichi.d/index/<key>/ makes the second and later calls cheap (no
 * re-embedding unless a file's mtime changed).
 */

#include "jc_docs.h"
#include "jc_rss.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_index.h"
#include "jc_retrieve.h"
#include "jc_queryrewrite.h"
#include "jc_pdf.h"
#include "jc_path.h"
#include "jc_http.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* Re-fetch a cached URL source after this many seconds (1 day). */
#define DOCS_URL_TTL 86400.0
/* Cap a fetched page (bytes) before stripping/indexing. */
#define DOCS_FETCH_MAX (2 * 1024 * 1024)

/* Case-insensitive prefix test: does `s` begin with `pfx`? */
static int ci_prefix(const char *s, const char *pfx)
{
    while (*pfx != '\0') {
        char a = *s;
        char b = *pfx;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) {
            return 0;
        }
        s++;
        pfx++;
    }
    return 1;
}

/* True if the tag starting just after '<' (optionally with a leading '/') is a
 * block-level element whose boundary should become a newline. */
static int tag_is_block(const char *p)
{
    static const char *const BLOCK[] = {
        "p", "br", "div", "li", "ul", "ol", "tr", "h1", "h2", "h3", "h4",
        "h5", "h6", "pre", "table", "section", "article", "header", "footer",
        "blockquote", "hr", NULL
    };
    int i;
    if (*p == '/') {
        p++;
    }
    for (i = 0; BLOCK[i] != NULL; i++) {
        jc_size n = strlen(BLOCK[i]);
        if (ci_prefix(p, BLOCK[i])) {
            char after = p[n];
            /* the tag name ends here (space, '>', '/', or attribute) */
            if (after == '>' || after == ' ' || after == '/' ||
                after == '\t' || after == '\n' || after == '\0') {
                return 1;
            }
        }
    }
    return 0;
}

/* M524: a NUMERIC character reference -- "&#167;" or "&#xA7;" -- encoded as
 * UTF-8. Only "&#39;" had been special-cased, so everything else survived
 * verbatim: measured while indexing the ANSI C Rationale, whose prose uses
 * &#160; and &#167; throughout, a reader got "&#167;3.6.2" and the raw
 * references went into the EMBEDDED chunks as noise, not merely into the
 * display.
 *
 * Returns the codepoint and advances *pp past the ';', or 0 if this is not a
 * well-formed numeric reference (in which case *pp is untouched and the caller
 * emits the '&' literally -- refuse rather than guess). */
static unsigned long html_numeric_ref(const char **pp)
{
    const char *p = *pp;            /* just past the '&' */
    unsigned long cp = 0;
    int digits = 0;
    int hex = 0;
    if (*p != '#') {
        return 0;
    }
    p++;
    if (*p == 'x' || *p == 'X') {
        hex = 1;
        p++;
    }
    while (*p != '\0' && *p != ';' && digits < 8) {
        int d;
        if (*p >= '0' && *p <= '9')       d = *p - '0';
        else if (hex && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (hex && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else return 0;
        cp = cp * (unsigned long)(hex ? 16 : 10) + (unsigned long)d;
        digits++;
        p++;
    }
    if (digits == 0 || *p != ';' || cp == 0 || cp > 0x10FFFFUL) {
        return 0;
    }
    /* Surrogates are not valid scalar values; treat as malformed. */
    if (cp >= 0xD800UL && cp <= 0xDFFFUL) {
        return 0;
    }
    *pp = p + 1;
    return cp;
}

/* Append one codepoint as UTF-8. Local to this module: the util layer has a
 * decoder but no encoder, and one caller does not justify a public API. */
static void sb_append_cp(struct jc_sb *out, unsigned long cp)
{
    if (cp < 0x80UL) {
        jc_sb_append_char(out, (char)cp);
    } else if (cp < 0x800UL) {
        jc_sb_append_char(out, (char)(0xC0UL | (cp >> 6)));
        jc_sb_append_char(out, (char)(0x80UL | (cp & 0x3FUL)));
    } else if (cp < 0x10000UL) {
        jc_sb_append_char(out, (char)(0xE0UL | (cp >> 12)));
        jc_sb_append_char(out, (char)(0x80UL | ((cp >> 6) & 0x3FUL)));
        jc_sb_append_char(out, (char)(0x80UL | (cp & 0x3FUL)));
    } else {
        jc_sb_append_char(out, (char)(0xF0UL | (cp >> 18)));
        jc_sb_append_char(out, (char)(0x80UL | ((cp >> 12) & 0x3FUL)));
        jc_sb_append_char(out, (char)(0x80UL | ((cp >> 6) & 0x3FUL)));
        jc_sb_append_char(out, (char)(0x80UL | (cp & 0x3FUL)));
    }
}

void jc_docs_html_to_text(const char *html, struct jc_sb *out)
{
    const char *p;
    int pending_nl = 0; /* a block boundary is queued */
    int pending_sp = 0; /* an inline boundary is queued */
    int wrote = 0;      /* any non-space char emitted yet */

    if (html == NULL || out == NULL) {
        return;
    }
    for (p = html; *p != '\0';) {
        if (*p == '<') {
            const char *t = p + 1;
            if (ci_prefix(t, "script") || ci_prefix(t, "style") ||
                ci_prefix(t, "!--")) {
                /* Skip the whole element body. */
                const char *close = ci_prefix(t, "script") ? "</script"
                                  : ci_prefix(t, "style") ? "</style" : "-->";
                const char *q = p + 1;
                while (*q != '\0' && !ci_prefix(q, close)) {
                    q++;
                }
                if (*q != '\0') {
                    q += strlen(close);
                    while (*q != '\0' && *q != '>') {
                        q++;
                    }
                    if (*q == '>') {
                        q++;
                    }
                }
                p = q;
                pending_nl = 1;
                continue;
            }
            /* Block tags become a newline; inline tags (b/i/a/span/...) add no
             * boundary -- the text nodes carry their own spaces, so injecting
             * one here would split "<b>word</b>." into "word ." (M51). */
            if (tag_is_block(t)) {
                pending_nl = 1;
            }
            while (*p != '\0' && *p != '>') {
                p++;
            }
            if (*p == '>') {
                p++;
            }
            continue;
        }
        if (*p == '&') {
            const char *e = p + 1;
            const char *rep = NULL;
            if (ci_prefix(e, "amp;")) { rep = "&"; e += 4; }
            else if (ci_prefix(e, "lt;")) { rep = "<"; e += 3; }
            else if (ci_prefix(e, "gt;")) { rep = ">"; e += 3; }
            else if (ci_prefix(e, "quot;")) { rep = "\""; e += 5; }
            else if (ci_prefix(e, "#39;")) { rep = "'"; e += 4; }
            else if (ci_prefix(e, "apos;")) { rep = "'"; e += 5; }
            else if (ci_prefix(e, "nbsp;")) { rep = " "; e += 5; }
            if (rep == NULL && *e == '#') {
                const char *q = e;
                unsigned long cp = html_numeric_ref(&q);
                if (cp != 0) {
                    /* U+00A0 and friends are spaces: route them through the
                     * pending-space path so they collapse like any other run of
                     * whitespace, rather than becoming a hard character. */
                    if (cp == 0xA0UL || cp == 0x2007UL || cp == 0x202FUL) {
                        pending_sp = 1;
                    } else {
                        if (pending_nl && wrote) jc_sb_append_char(out, '\n');
                        else if (pending_sp && wrote) jc_sb_append_char(out, ' ');
                        pending_nl = pending_sp = 0;
                        sb_append_cp(out, cp);
                        wrote = 1;
                    }
                    p = q;
                    continue;
                }
            }
            if (rep != NULL) {
                if (rep[0] != ' ') {
                    if (pending_nl && wrote) jc_sb_append_char(out, '\n');
                    else if (pending_sp && wrote) jc_sb_append_char(out, ' ');
                    pending_nl = pending_sp = 0;
                    jc_sb_append(out, rep);
                    wrote = 1;
                } else {
                    pending_sp = 1;
                }
                p = e;
                continue;
            }
        }
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ||
            *p == '\f' || *p == '\v') {
            if (*p == '\n') {
                pending_nl = pending_nl || 0; /* keep block intent */
            }
            pending_sp = 1;
            p++;
            continue;
        }
        /* a real glyph: flush the strongest pending boundary first */
        if (pending_nl && wrote) {
            jc_sb_append_char(out, '\n');
        } else if (pending_sp && wrote) {
            jc_sb_append_char(out, ' ');
        }
        pending_nl = pending_sp = 0;
        jc_sb_append_char(out, *p);
        wrote = 1;
        p++;
    }
}

/* Compute the local cache directory for a URL doc source and ensure it exists.
 * Keyed by name so a stable dir survives across runs. */
static int url_cache_dir(const struct jc_docs_cfg *src, char *buf, jc_size cap)
{
    char safe[128];
    jc_size i;
    const char *nm = (src->name != NULL) ? src->name : "docs";
    for (i = 0; nm[i] != '\0' && i < sizeof(safe) - 1; i++) {
        char ch = nm[i];
        int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                 (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        safe[i] = ok ? ch : '_';
    }
    safe[i] = '\0';
    jc_snprintf(buf, cap, "%s/.jichi.d/docs/%s", jc_home_dir(), safe);
    return jc_mkdir_p(buf) == JC_OK;
}

/* Materialize a URL doc source into a local cache dir holding one text file,
 * (re)fetching when the cache is missing or older than the TTL. On success
 * writes the cache dir into `root` (which jc_index_build then indexes like any
 * local directory). Returns JC_OK or an error with *err set to a heap message. */
static jc_status materialize_url(struct jc_app *app,
                                 const struct jc_docs_cfg *src,
                                 char *root, jc_size root_cap, char **err)
{
    char file[4200];
    double mtime;
    struct jc_http_request req;
    long status = 0;
    char *body = NULL;
    jc_size len = 0;
    jc_status st;
    struct jc_sb text;

    *err = NULL;
    if (!url_cache_dir(src, root, root_cap)) {
        *err = jc_strdup("error: could not create the docs cache directory");
        return JC_ERR_IO;
    }
    jc_snprintf(file, sizeof(file), "%s/page.txt", root);

    /* Fresh cache? Reuse it (no network). */
    mtime = jc_file_mtime(file);
    if (mtime >= 0.0 && (jc_now_seconds() - mtime) < DOCS_URL_TTL) {
        return JC_OK;
    }

    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = src->url;
    req.timeout_secs = 30;
    req.abort_flag = &app->abort_flag;
    /* A documentation URL is a CONTENT fetch with no credential attached, and
     * doc sites redirect constantly (http->https, /latest->/v1.2, trailing
     * slash). So this is one of the three callers that should follow (M472).
     * Note it must be set explicitly now: the check below is `status >= 400`,
     * so an unfollowed 302 would have stored an empty page as the cached doc. */
    req.follow_redirects = 1;
    st = jc_http_perform(&req, &status, &body, &len);
    if (st != JC_OK || status >= 400) {
        free(body);
        /* Stale cache is better than nothing: keep using it if present. */
        if (mtime >= 0.0) {
            return JC_OK;
        }
        {
            struct jc_sb sb;
            jc_sb_init(&sb);
            if (st != JC_OK) {
                jc_sb_append_fmt(&sb, "error: fetching %s failed (%s)",
                                 src->url, jc_status_str(st));
            } else {
                jc_sb_append_fmt(&sb, "error: HTTP %ld fetching %s", status,
                                 src->url);
            }
            *err = jc_sb_finish(&sb);
            jc_sb_free(&sb);
        }
        return (st != JC_OK) ? st : JC_ERR_HTTP;
    }
    if (len > DOCS_FETCH_MAX) {
        len = DOCS_FETCH_MAX;
    }
    if (body != NULL) {
        body[len] = '\0';
    }

    jc_sb_init(&text);
    /* A feed source (config type "rss"/"atom") reduces via jc_rss; otherwise the
     * page is treated as HTML. Auto-detect a feed too, so a `url` that turns out
     * to serve XML is still read cleanly. */
    if (src->feed || jc_rss_looks_like_feed(body != NULL ? body : "")) {
        jc_rss_to_text(body != NULL ? body : "", &text);
    } else {
        jc_docs_html_to_text(body != NULL ? body : "", &text);
    }
    free(body);
    st = jc_write_file(file, text.data != NULL ? text.data : "", text.len);
    jc_sb_free(&text);
    if (st != JC_OK) {
        *err = jc_strdup("error: could not write the fetched docs to cache");
        return st;
    }
    return JC_OK;
}

jc_status jc_docs_source_root(struct jc_app *app, const struct jc_docs_cfg *src,
                              char *root, jc_size cap, char **err)
{
    if (err != NULL) {
        *err = NULL;
    }
    if (src == NULL || (src->path == NULL && src->url == NULL)) {
        return JC_ERR_INVALID;
    }
    if (src->url != NULL) {
        char *e = NULL;
        jc_status st = materialize_url(app, src, root, cap, &e);
        if (err != NULL) {
            *err = e;
        } else {
            free(e);
        }
        return st;
    }
    if (jc_path_resolve(src->path, root, cap) != JC_OK || !jc_is_dir(root)) {
        if (err != NULL) {
            struct jc_sb sb;
            jc_sb_init(&sb);
            jc_sb_append_fmt(&sb,
                "error: docs source path is not a directory: %s", src->path);
            *err = jc_sb_finish(&sb);
            jc_sb_free(&sb);
        }
        return JC_ERR_NOTFOUND;
    }
    return JC_OK;
}

const struct jc_docs_cfg *jc_docs_find(struct jc_app *app, const char *name)
{
    jc_size i;

    if (app == NULL) {
        return NULL;
    }
    if (name == NULL || name[0] == '\0') {
        /* Sole-source shortcut: an unnamed query is unambiguous iff there is
         * exactly one configured source. */
        return (app->config.docs.len == 1)
                   ? (const struct jc_docs_cfg *)jc_vec_at(&app->config.docs, 0)
                   : NULL;
    }
    for (i = 0; i < app->config.docs.len; i++) {
        const struct jc_docs_cfg *d =
            (const struct jc_docs_cfg *)jc_vec_at(&app->config.docs, i);
        if (d->name != NULL && strcmp(d->name, name) == 0) {
            return d;
        }
    }
    return NULL;
}

static char *format_results(struct jc_index *idx, const int *sel, int n)
{
    struct jc_sb sb;
    int i;

    jc_sb_init(&sb);
    if (n == 0) {
        jc_sb_append(&sb, "No matching documentation found.");
        return jc_sb_finish(&sb);
    }
    for (i = 0; i < n; i++) {
        const char *path = jc_index_chunk_path(idx, sel[i]);
        const char *text = jc_index_chunk_text(idx, sel[i]);
        int s = jc_index_chunk_start(idx, sel[i]);
        int e = jc_index_chunk_end(idx, sel[i]);
        jc_sb_append_fmt(&sb, "%s:%d-%d\n", path != NULL ? path : "?", s, e);
        jc_sb_append(&sb, text != NULL ? text : "");
        jc_sb_append(&sb, "\n---\n");
    }
    return jc_sb_finish(&sb);
}

jc_status jc_docs_run(struct jc_app *app, const struct jc_docs_cfg *src,
                      const char *query, int top_k, char **out_text)
{
    struct jc_model_cfg *embed_model;
    struct jc_model_cfg *rerank_model;
    struct jc_index *index = NULL;
    struct jc_retrieve_opts opts;
    char root[4096];
    int *idx = NULL;
    double *score = NULL;
    int n = 0;
    jc_status st;

    *out_text = NULL;
    if (src == NULL || (src->path == NULL && src->url == NULL)) {
        return JC_ERR_INVALID;
    }
    if (top_k <= 0) {
        top_k = 5;
    }

    embed_model = jc_app_model_for_role(app, JC_ROLE_EMBED);
    if (embed_model == NULL) {
        *out_text = jc_strdup("error: no embedding model configured "
                              "(add a model with role \"embed\")");
        return JC_ERR_NOTFOUND;
    }

    {
        char *err = NULL;
        st = jc_docs_source_root(app, src, root, sizeof(root), &err);
        if (st != JC_OK) {
            *out_text = (err != NULL) ? err
                : jc_strdup("error: could not resolve the docs source");
            return st;
        }
    }

    /* Build (or incrementally reload) the index over the docs directory. PDF
     * sources are extracted to text (M42/M44); the codebase index does not. */
    st = jc_index_build(root, embed_model, 0,
                        jc_pdf_command(app->config.pdf_command), &index, NULL,
                        &app->abort_flag, &app->config.ignore_dirs);
    if (st != JC_OK) {
        *out_text = jc_strdup("error: failed to index the documentation source");
        return st;
    }

    if (jc_index_count(index) == 0) {
        jc_index_free(index);
        *out_text = jc_strdup("The documentation index is empty.");
        return JC_OK;
    }

    rerank_model = jc_app_model_for_role(app, JC_ROLE_RERANK);
    jc_retrieve_opts_from_config(&app->config, top_k, &opts);

    idx = (int *)malloc((jc_size)top_k * sizeof(int));
    score = (double *)malloc((jc_size)top_k * sizeof(double));
    if (idx == NULL || score == NULL) {
        free(idx);
        free(score);
        jc_index_free(index);
        return JC_ERR_OOM;
    }
    {
        /* Optional query rewrite/HyDE (opt-in): embed an expanded query. */
        char *eff = NULL;
        const char *q = query;
        if (app->config.retrieval.query_rewrite != JC_QR_OFF) {
            eff = jc_queryrewrite_run(app, query,
                                      app->config.retrieval.query_rewrite);
            if (eff != NULL) {
                q = eff;
            }
        }
        st = jc_retrieve_from_index(index, embed_model, rerank_model, q,
                                    &opts, idx, score, &n, &app->abort_flag);
        free(eff);
    }
    if (st != JC_OK) {
        free(idx);
        free(score);
        jc_index_free(index);
        if (st == JC_ERR_PROVIDER) {
            *out_text = jc_strdup("error: query/index embedding dimension "
                                  "mismatch (the docs were indexed with a "
                                  "different model)");
        } else {
            *out_text = jc_strdup("error: failed to embed the query");
        }
        return st;
    }

    *out_text = format_results(index, idx, n);
    free(idx);
    free(score);
    jc_index_free(index);
    return (*out_text != NULL) ? JC_OK : JC_ERR_OOM;
}
