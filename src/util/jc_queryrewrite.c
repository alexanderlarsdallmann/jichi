/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_queryrewrite.c - optional query transformation (see jc_queryrewrite.h). */

#include "jc_queryrewrite.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_provider.h"
#include "jc_message.h"
#include "jc_http.h"
#include "jc_oneshot.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

static const char *REWRITE_SYS =
    "You expand a search query to improve retrieval. Follow the user's "
    "instruction exactly and output only the requested text, with no preamble, "
    "explanation, or markdown fences.";

void jc_queryrewrite_prompt(const char *query, int mode, struct jc_sb *out)
{
    if (out == NULL) {
        return;
    }
    if (query == NULL) {
        query = "";
    }
    if (mode == JC_QR_MULTIQUERY) {
        jc_sb_append(out,
            "List three alternative phrasings of the search query below, using "
            "synonyms, related identifiers, and keywords. One phrasing per line, "
            "no numbering or bullets.\n\nQuery: ");
    } else {
        /* HyDE (default for any non-off, non-multiquery mode). */
        jc_sb_append(out,
            "Write a short hypothetical passage (code or documentation, two or "
            "three sentences) that would directly answer or match the search "
            "query below. Output only the passage.\n\nQuery: ");
    }
    jc_sb_append(out, query);
}

void jc_queryrewrite_clean(const char *resp, struct jc_sb *out)
{
    const char *p;
    int first = 1;

    if (out == NULL || resp == NULL) {
        return;
    }
    p = resp;
    while (*p != '\0') {
        const char *eol = p;
        const char *s;
        const char *e;
        while (*eol != '\0' && *eol != '\n') {
            eol++;
        }
        /* Trim leading whitespace and common list markers. */
        s = p;
        while (s < eol && (*s == ' ' || *s == '\t')) {
            s++;
        }
        if (s + 2 < eol && (*s == '-' || *s == '*' || *s == '+') &&
            s[1] == ' ') {
            s += 2;
        } else {
            const char *d = s;
            while (d < eol && *d >= '0' && *d <= '9') {
                d++;
            }
            if (d > s && d < eol && (*d == '.' || *d == ')') &&
                d + 1 < eol && d[1] == ' ') {
                s = d + 2;
            }
        }
        /* Trim trailing whitespace. */
        e = eol;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) {
            e--;
        }
        /* Skip blank lines and fence lines (``` ...). */
        if (e > s && !(e - s >= 3 && s[0] == '`' && s[1] == '`' && s[2] == '`')) {
            if (!first) {
                jc_sb_append_char(out, ' ');
            }
            jc_sb_append_n(out, s, (jc_size)(e - s));
            first = 0;
        }
        if (*eol == '\0') {
            break;
        }
        p = eol + 1;
    }
}

/* One non-streaming rewrite call. Returns the cleaned expansion (malloc'd) or
 * NULL on failure. */
static char *rewrite_call(struct jc_app *app, struct jc_provider *prov,
                          const char *query, int mode)
{
    struct jc_sb prompt;
    char *raw;
    char *result = NULL;

    jc_sb_init(&prompt);
    jc_queryrewrite_prompt(query, mode, &prompt);
    raw = jc_oneshot(prov, REWRITE_SYS,
                     prompt.data != NULL ? prompt.data : "", 30,
                     &app->abort_flag);
    jc_sb_free(&prompt);

    if (raw != NULL) {
        struct jc_sb clean;
        jc_sb_init(&clean);
        jc_queryrewrite_clean(raw, &clean);
        result = jc_sb_finish(&clean);
        free(raw);
    }
    return result;
}

char *jc_queryrewrite_run(struct jc_app *app, const char *query, int mode)
{
    struct jc_model_cfg *m;
    struct jc_provider *prov;
    char *expansion;
    char *effective = NULL;

    if (app == NULL || query == NULL || mode == JC_QR_OFF) {
        return NULL;
    }
    m = jc_app_model_for_role(app, JC_ROLE_SUMMARIZE);
    if (m == NULL) {
        m = &app->config.model;
    }
    if (m == NULL) {
        return NULL;
    }
    prov = jc_provider_create(m);
    if (prov == NULL) {
        return NULL;
    }
    expansion = rewrite_call(app, prov, query, mode);
    prov->vt->free(prov);
    if (expansion == NULL || expansion[0] == '\0') {
        free(expansion);
        return NULL;
    }
    /* Effective query = raw query + " " + expansion, so the dense embedding and
     * the lexical pass both see the original terms plus the expansion. */
    {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_sb_append(&sb, query);
        jc_sb_append_char(&sb, ' ');
        jc_sb_append(&sb, expansion);
        effective = jc_sb_finish(&sb);
    }
    free(expansion);
    return effective;
}
