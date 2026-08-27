/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_autocontext.c - automatic RAG context injection (see jc_autocontext.h). */

#include "jc_autocontext.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_search.h"
#include "jc_docs.h"
#include "jc_refs.h"
#include "jc_compact.h"
#include "jc_eventlog.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_TOKENS 3000   /* default injection cap when unset             */
#define BYTES_PER_TOKEN 4     /* matches jc_compact's estimate heuristic       */

long jc_autocontext_budget(long limit, long cap)
{
    long share;
    if (cap <= 0) {
        cap = DEFAULT_TOKENS;
    }
    if (limit <= 0) {
        return cap;
    }
    share = limit / 3;
    return (cap < share) ? cap : share;
}

/* A retrieval result is "useful" when it is neither an error nor an empty/no-
 * match notice from jc_search_run / jc_docs_run. */
static int useful(const char *t)
{
    if (t == NULL || t[0] == '\0') {
        return 0;
    }
    if (strncmp(t, "error", 5) == 0) {
        return 0;
    }
    if (strncmp(t, "No matching", 11) == 0) {
        return 0;
    }
    if (strncmp(t, "The codebase index is empty", 27) == 0) {
        return 0;
    }
    if (strncmp(t, "The documentation index is empty", 32) == 0) {
        return 0;
    }
    return 1;
}

/* Append a labeled section, truncating `text` to fit `remaining` tokens.
 * Returns the tokens consumed. Caller ensures remaining > 0. */
static long add_section(struct jc_sb *sb, const char *label, const char *text,
                        long remaining)
{
    long ltok = jc_compact_estimate_text(label);
    long ttok = jc_compact_estimate_text(text);

    jc_sb_append(sb, label);
    if (ltok + ttok <= remaining) {
        jc_sb_append(sb, text);
        return ltok + ttok;
    } else {
        long avail = remaining - ltok;
        jc_size bytes;
        if (avail < 0) {
            avail = 0;
        }
        bytes = (jc_size)(avail * BYTES_PER_TOKEN);
        if (bytes > (jc_size)strlen(text)) {
            bytes = (jc_size)strlen(text);
        }
        jc_sb_append_n(sb, text, bytes);
        jc_sb_append(sb, "\n... [truncated]\n");
        return remaining;
    }
}

jc_status jc_autocontext_expand(struct jc_app *app, const char *raw,
                                struct jc_arena *a, char **out)
{
    struct jc_config *cfg;
    struct jc_sb block;
    struct jc_vec refs;
    long budget;
    long used = 0;
    int top_k;
    int nblocks = 0;

    *out = NULL;
    cfg = (app != NULL) ? &app->config : NULL;

    /* Gating: feature off / subagent / empty / slash command / no embed model. */
    if (app == NULL || cfg == NULL || !cfg->auto_context ||
        app->agent_depth != 0 || raw == NULL || raw[0] == '\0' ||
        raw[0] == '/' ||
        jc_app_model_for_role(app, JC_ROLE_EMBED) == NULL) {
        *out = jc_arena_strdup(a, raw != NULL ? raw : "");
        return JC_OK;
    }

    /* Skip when the user already supplied explicit @-references. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    if (jc_refs_scan(raw, &refs) > 0) {
        jc_refs_free(&refs);
        *out = jc_arena_strdup(a, raw);
        return JC_OK;
    }
    jc_refs_free(&refs);

    top_k = (cfg->auto_context_top_k > 0) ? cfg->auto_context_top_k : 5;
    budget = jc_autocontext_budget(jc_compact_context_limit(app),
                                   cfg->auto_context_max_tokens);
    /* The retrieved text is measured with the raw byte estimate, which runs
     * optimistic; deflate the budget by the model's calibration ratio (M77) so
     * the REAL injected size stays within the intended fraction of the window. */
    {
        double cal = jc_compact_calibration(app);
        if (cal > 1.0) {
            budget = (long)((double)budget / cal);
        }
    }

    jc_sb_init(&block);
    jc_sb_append(&block, "\n\n--- automatically retrieved context ---\n");

    /* Codebase index. */
    if (cfg->auto_context_sources != JC_ACTX_DOCS && used < budget) {
        char *text = NULL;
        if (jc_search_run(app, raw, top_k, &text) == JC_OK && useful(text)) {
            used += add_section(&block, "[codebase]\n", text, budget - used);
            nblocks++;
        }
        free(text);
    }

    /* External docs sources. */
    if (cfg->auto_context_sources != JC_ACTX_CODEBASE) {
        jc_size i;
        for (i = 0; i < cfg->docs.len && used < budget; i++) {
            const struct jc_docs_cfg *d =
                (const struct jc_docs_cfg *)jc_vec_at(&cfg->docs, i);
            char *text = NULL;
            char label[256];
            if (jc_docs_run(app, d, raw, top_k, &text) == JC_OK &&
                useful(text)) {
                jc_snprintf(label, sizeof(label), "[docs:%s]\n",
                            d->name != NULL ? d->name : "");
                used += add_section(&block, label, text, budget - used);
                nblocks++;
            }
            free(text);
        }
    }

    /* Telemetry (M21-style metrics event). */
    {
        cJSON *o = jc_app_telem_begin(app, "retrieve");
        if (o != NULL) {
            cJSON_AddNumberToObject(o, "blocks", (double)nblocks);
            cJSON_AddNumberToObject(o, "tokens", (double)used);
            jc_app_telem_end(app, o);
        }
    }

    if (nblocks == 0) {
        jc_sb_free(&block);
        *out = jc_arena_strdup(a, raw);
        return JC_OK;
    }
    {
        struct jc_sb full;
        jc_sb_init(&full);
        jc_sb_append(&full, raw);
        jc_sb_append(&full, block.data != NULL ? block.data : "");
        *out = jc_arena_strdup(a, full.data != NULL ? full.data : raw);
        jc_sb_free(&full);
    }
    jc_sb_free(&block);
    return JC_OK;
}
