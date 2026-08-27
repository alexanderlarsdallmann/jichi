/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_compact.c - automatic history compaction (see jc_compact.h). */

#include "jc_compact.h"
#include "jc_app.h"
#include "jc_calib.h"
#include "jc_toolout.h"
#include "jc_agent.h"
#include "jc_provider.h"
#include "jc_http.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_mem.h"
#include "jc_log.h"
#include "jc_snprintf.h"
#include "jc_utf8.h"
#include "jc_path.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Byte heuristic: roughly this many bytes per token. */
#define BYTES_PER_TOKEN 4
/* Flat per-message token overhead (role markers, framing). */
#define MSG_OVERHEAD 4
/* Flat allowance for the system prompt + tool schemas not in `hist`. Kept as the
 * FALLBACK only (see nonhist_est): measured on real workloads the true figure is
 * 7421 tokens under the `core` tool profile and 11167 under `full`, so this
 * constant understates it by 5-9k. The pure trim passes below still use it as a
 * fixed offset in their stop condition, where being off by that much on a ~100k
 * budget only shifts where they stop trimming; it is used as a SCALING basis
 * nowhere any more, which is where the error actually mattered (M286). */
#define SYS_TOOLS_OVERHEAD 2000L
/* Don't bother compacting histories shorter than this. */
#define MIN_MESSAGES 4
/* Per-message content cap when rendering the transcript (bytes). */
#define RENDER_TRUNC 4000

/* Token estimate for one message (content + tool-call payloads + overhead). */
static long msg_tokens(struct jc_message *m)
{
    long bytes = 0;
    jc_size k, n;
    if (m->content != NULL) {
        bytes += (long)strlen(m->content);
    }
    n = jc_msg_tool_call_count(m);
    for (k = 0; k < n; k++) {
        struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
        if (tc->name != NULL) {
            bytes += (long)strlen(tc->name);
        }
        if (tc->arguments_json != NULL) {
            bytes += (long)strlen(tc->arguments_json);
        }
    }
    return bytes / BYTES_PER_TOKEN + MSG_OVERHEAD;
}

long jc_compact_estimate_message(struct jc_message *m)
{
    return (m != NULL) ? msg_tokens(m) : 0;
}

long jc_compact_estimate_message_cal(struct jc_app *app, struct jc_message *m)
{
    return (long)((double)jc_compact_estimate_message(m)
                  * jc_compact_calibration(app));
}

long jc_compact_estimate_tokens(const struct jc_history *hist)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n = jc_history_len(h);
    jc_size i;
    long total = 0;
    for (i = 0; i < n; i++) {
        total += msg_tokens(jc_history_get(h, i));
    }
    return total;
}

long jc_compact_estimate_text(const char *s)
{
    return (s != NULL) ? (long)strlen(s) / BYTES_PER_TOKEN : 0;
}

jc_size jc_compact_find_cut(const struct jc_history *hist, long keep_tokens)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n = jc_history_len(h);
    long acc = 0;
    long best_cut = -1;  /* earliest user index whose tail fits keep_tokens */
    long last_user = -1; /* most recent user index                          */
    jc_size idx;

    if (n < MIN_MESSAGES) {
        return 0;
    }

    for (idx = n; idx > 0; idx--) {
        jc_size i = idx - 1;
        struct jc_message *m = jc_history_get(h, i);
        acc += msg_tokens(m);
        if (m->role == JC_ROLE_USER) {
            if (last_user < 0) {
                last_user = (long)i;
            }
            if (acc <= keep_tokens) {
                best_cut = (long)i;
            } else {
                break; /* earlier user boundaries only cost more */
            }
        }
    }

    if (best_cut < 0) {
        best_cut = last_user; /* even the last turn exceeds budget: keep it */
    }
    if (best_cut <= 0) {
        return 0; /* nothing before the cut to summarize */
    }
    return (jc_size)best_cut;
}

/* Append `s` to `sb`, truncating to RENDER_TRUNC bytes with a marker. */
static void append_truncated(struct jc_sb *sb, const char *s)
{
    jc_size len;
    if (s == NULL) {
        return;
    }
    len = (jc_size)strlen(s);
    if (len <= RENDER_TRUNC) {
        jc_sb_append(sb, s);
    } else {
        jc_sb_append_n(sb, s, jc_utf8_trunc_len(s, (jc_size)RENDER_TRUNC));
        jc_sb_append(sb, "...[truncated]");
    }
}

jc_size jc_compact_window_end(const struct jc_history *hist, jc_size start,
                              long budget_tokens)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n = jc_history_len(h);
    jc_size i;
    long acc = 0;

    if (start >= n) {
        return n;
    }
    for (i = start; i < n; i++) {
        long t = msg_tokens(jc_history_get(h, i));
        /* Always include at least the first message of the window. */
        if (i > start && acc + t > budget_tokens) {
            return i;
        }
        acc += t;
    }
    return n;
}

char *jc_compact_render_range(const struct jc_history *hist, jc_size start,
                              jc_size end, struct jc_arena *a)
{
    struct jc_history *h = (struct jc_history *)hist;
    struct jc_sb sb;
    jc_size i;
    char *out;

    jc_sb_init(&sb);
    for (i = start; i < end; i++) {
        struct jc_message *m = jc_history_get(h, i);
        switch (m->role) {
            case JC_ROLE_USER:
                jc_sb_append(&sb, "User: ");
                break;
            case JC_ROLE_ASSISTANT:
                jc_sb_append(&sb, "Assistant: ");
                break;
            case JC_ROLE_TOOL:
                jc_sb_append(&sb, m->is_error ? "Tool error: "
                                              : "Tool result: ");
                break;
            default:
                jc_sb_append(&sb, "System: ");
                break;
        }
        append_truncated(&sb, m->content);
        jc_sb_append_char(&sb, '\n');

        if (m->role == JC_ROLE_ASSISTANT) {
            jc_size k, nc = jc_msg_tool_call_count(m);
            for (k = 0; k < nc; k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                jc_sb_append(&sb, "  -> called ");
                jc_sb_append(&sb, tc->name ? tc->name : "");
                jc_sb_append_char(&sb, '(');
                append_truncated(&sb, tc->arguments_json);
                jc_sb_append(&sb, ")\n");
            }
        }
    }
    out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return out;
}

char *jc_compact_render_transcript(const struct jc_history *hist, jc_size end,
                                   struct jc_arena *a)
{
    return jc_compact_render_range(hist, 0, end, a);
}

void jc_compact_apply(struct jc_history *hist, jc_size cut, const char *summary,
                      struct jc_arena *a)
{
    struct jc_message *keep;
    char *orig;
    struct jc_sb sb;

    if (cut == 0 || cut >= jc_history_len(hist)) {
        return;
    }
    keep = jc_history_get(hist, cut);
    orig = jc_arena_strdup(a, keep->content != NULL ? keep->content : "");

    jc_sb_init(&sb);
    jc_sb_append(&sb, "[Earlier conversation summarized to save context]\n");
    jc_sb_append(&sb, summary != NULL ? summary : "");
    jc_sb_append(&sb, "\n\n[Most recent request follows]\n");
    jc_sb_append(&sb, orig);

    jc_history_drop_front(hist, cut);
    jc_msg_set_content(jc_history_get(hist, 0),
                       sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
}

/* Resolve the effective context-token budget. */
static long effective_limit(struct jc_app *app)
{
    if (app->config.context_limit > 0) {
        return app->config.context_limit;
    }
    if (app->config.model.context_limit > 0) {
        return app->config.model.context_limit;
    }
    return JC_COMPACT_DEFAULT_LIMIT;
}

long jc_compact_context_limit(struct jc_app *app)
{
    return effective_limit(app);
}

/* M358: the EXPLICITLY configured limit only -- 0 when jichi would be using
 * the built-in default. The context-gauge prompt line states this number as a
 * fact about the model, and JC_COMPACT_DEFAULT_LIMIT is a guess about it (the
 * M355 armed-only rule: a limit nobody set is not a fact about this model). */
long jc_compact_context_limit_explicit(struct jc_app *app)
{
    if (app == NULL) {
        return 0;
    }
    if (app->config.context_limit > 0) {
        return app->config.context_limit;
    }
    if (app->config.model.context_limit > 0) {
        return app->config.model.context_limit;
    }
    return 0;
}

/* M358: the [context] pressure note -- the model-facing half of the gauge.
 * The operator has warned_short and the TUI ctx%; the MODEL saw only the
 * per-message elision markers after the fact, which explain what happened to
 * one output but never say "change how you read". Rendered once per agent
 * loop, at the first pressed mid-turn pass; `reached` picks the honest
 * message (elision is coping vs elision cannot cope). The percentage uses
 * the pass's own calibrated numbers, so the note and the trigger cannot
 * disagree. Appends nothing without real numbers. */
void jc_compact_pressure_note(long before, long limit, int reached,
                              struct jc_sb *out)
{
    long pct;

    if (out == NULL || before <= 0 || limit <= 0) {
        return;
    }
    pct = (before * 100L) / limit;
    jc_sb_append_fmt(out,
                     "[context] this turn has used ~%ld%% of the ~%ld-token "
                     "context budget. ",
                     pct, limit);
    if (reached) {
        jc_sb_append(out,
                     "Older tool output is being elided to make room. Prefer "
                     "read_file with offset/limit and search_code over "
                     "whole-file reads, and do not re-read files that have "
                     "not changed.");
    } else {
        jc_sb_append(out,
                     "Eliding old tool output could NOT bring it back under "
                     "the target, so upcoming requests may exceed the "
                     "window. Finish the current step and write durable "
                     "results now; use offset/limit reads and search_code "
                     "from here on.");
    }
}

long jc_compact_context_limit_at(struct jc_app *app, int idx)
{
    const struct jc_model_cfg *m;

    if (app == NULL || idx < 0 ||
        (jc_size)idx >= app->config.models.len) {
        return 0;
    }
    /* A global budget is deliberately authoritative over every model's declared
     * window: the user pinned one number for the whole run. */
    if (app->config.context_limit > 0) {
        return app->config.context_limit;
    }
    m = (const struct jc_model_cfg *)jc_vec_at(&app->config.models,
                                               (jc_size)idx);
    if (m != NULL && m->context_limit > 0) {
        return m->context_limit;
    }
    return JC_COMPACT_DEFAULT_LIMIT;
}

/* The non-history part of a request (system prompt + tool schemas) in byte-estimate
 * tokens: the value measured on the most recent model call (M286), or the flat
 * SYS_TOOLS_OVERHEAD before any call has been made. The triggers below add this to
 * the history estimate BEFORE scaling by the calibration ratio, so the sum and the
 * ratio's own basis are the same quantity -- which is the whole point: a ratio
 * learned against `history + 2000` and applied to `history + 2000` hid a 5-9k
 * additive error inside a multiplicative correction, making the "constant" vary
 * 2.9x with history size. */
long jc_compact_nonhist_est(struct jc_app *app)
{
    if (app != NULL && app->last_nonhist_est > 0) {
        return app->last_nonhist_est;
    }
    return SYS_TOOLS_OVERHEAD;
}

double jc_compact_calibration(struct jc_app *app)
{
    const char *id;
    double r;
    if (app == NULL) {
        return 1.0;
    }
    id = (app->config.model.model != NULL) ? app->config.model.model
                                           : app->config.model.name;
    r = jc_calib_get(&app->calib, id);
    return (r > 0.0) ? r : 1.0;
}

long jc_compact_estimate_tokens_cal(struct jc_app *app,
                                    const struct jc_history *hist)
{
    return (long)((double)jc_compact_estimate_tokens(hist)
                  * jc_compact_calibration(app));
}

/* M536: the ONE quantity every context threshold must evaluate -- calibrated
 * history PLUS the measured non-history part of the request.
 *
 * Four call sites already computed this expression by hand, and their comments
 * state the reason out loud: "the same quantity the compaction trigger
 * evaluates (M286), so the two thresholds are comparable and 75% really does
 * come before 80%". The invariant was therefore written down three times and
 * enforced nowhere -- and the ONE reader that got it wrong was the TUI's ctx%
 * badge, the only one of the six a human ever looks at. It called
 * jc_compact_estimate_tokens_cal, i.e. history alone, so it under-read by
 * exactly the system prompt plus the tool schemas: on this repo's own tool set
 * that is a five-figure token count, and on a 32k context the badge sat in its
 * grey band while the 80% trigger was firing. An instrument that reads
 * comfortable while the machine acts is worse than no instrument, because the
 * operator stops looking.
 *
 * Percentages taken against jc_compact_context_limit(app) are now comparable
 * across the badge, the routing escalation (75%), the de-escalation (55%) and
 * the compaction trigger (80%) BY CONSTRUCTION rather than by four authors
 * remembering. tests/smoke/ctx_estimate_lint.sh keeps it that way. */
long jc_compact_effective_est(struct jc_app *app, const struct jc_history *hist)
{
    return (long)((double)(jc_compact_estimate_tokens(hist)
                           + jc_compact_nonhist_est(app))
                  * jc_compact_calibration(app));
}

long jc_compact_estimate_text_cal(struct jc_app *app, const char *s)
{
    return (long)((double)jc_compact_estimate_text(s)
                  * jc_compact_calibration(app));
}

long jc_compact_estimate_bytes(jc_size nbytes)
{
    return (long)nbytes / BYTES_PER_TOKEN;
}

long jc_compact_estimate_bytes_cal(struct jc_app *app, jc_size nbytes)
{
    return (long)((double)jc_compact_estimate_bytes(nbytes)
                  * jc_compact_calibration(app));
}

static const char *SUMMARIZE_SYS =
    "You are summarizing an AI coding agent's conversation so it can "
    "continue with less context. Write a concise but complete summary that "
    "preserves: the user's goals and explicit requests, key decisions, "
    "important facts (file paths, identifiers, commands, values), what has "
    "already been done, and what remains. Prefer compact prose or bullets. "
    "Do not omit details the agent would need to continue correctly.";

/* Summarizer context (tokens): the model's declared context, else the default. */
static long summarizer_ctx(const struct jc_model_cfg *sm)
{
    return (sm->context_limit > 0) ? sm->context_limit
                                   : JC_COMPACT_SUMMARIZER_DEFAULT;
}

/* Token budget for one summarizer *input* transcript (leaves room for the
 * system prompt + the generated summary + margin). */
static long summarizer_input_tokens(const struct jc_model_cfg *sm)
{
    long b = summarizer_ctx(sm) * 3 / 5;
    return (b < 256) ? 256 : b;
}

/* Token cap for the summarizer's *output*. */
static long summarizer_output_tokens(const struct jc_model_cfg *sm)
{
    long o = summarizer_ctx(sm) / 4;
    if (o > 2048) o = 2048;
    if (o < 256) o = 256;
    return o;
}

/* One non-streaming summarizer call over `transcript` against the already-built
 * `prov`. Returns JC_OK with *out (arena-owned) on success, JC_ERR_TOOBIG on an
 * HTTP 400 (context overflow -- the caller can shrink and retry), or another
 * JC_ERR_* on failure. */
static jc_status summarize_call(struct jc_app *app, struct jc_provider *prov,
                                const char *transcript, char **out)
{
    struct jc_history mini;
    struct jc_http_headers headers;
    struct jc_http_request req;
    struct jc_sb sb;
    char *body = NULL;
    char *resp = NULL;
    long http_status = 0;
    jc_status st;

    *out = NULL;
    jc_history_init(&mini);
    jc_sb_init(&sb);
    jc_sb_append(&sb, "Summarize this conversation transcript:\n\n");
    jc_sb_append(&sb, transcript != NULL ? transcript : "");
    jc_history_add(&mini, JC_ROLE_USER, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);

    st = prov->vt->build_request(prov, &mini, SUMMARIZE_SYS, NULL, 0, &body);
    if (st != JC_OK) {
        jc_history_free(&mini);
        return st;
    }
    jc_http_headers_init(&headers);
    prov->vt->add_headers(prov, &headers);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = prov->vt->endpoint(prov);
    req.headers = &headers;
    req.body = body;
    req.body_len = strlen(body);
    req.timeout_secs = 60;
    req.abort_flag = &app->abort_flag;

    st = jc_http_perform(&req, &http_status, &resp, NULL);
    jc_http_headers_free(&headers);
    free(body);

    if (st == JC_OK && http_status < 400 && resp != NULL) {
        struct jc_message *reply = jc_history_add(&mini, JC_ROLE_ASSISTANT,
                                                  NULL);
        st = prov->vt->parse_full(prov, resp, reply);
        if (st == JC_OK && reply->content != NULL && reply->content[0] != '\0') {
            /* M140: scratch, not the session arena -- the summary is folded
             * into the history by jc_compact_apply (which mallocs its own
             * copy), so it only needs to live to the end of this turn.
             * Session-arena copies accumulated once per compaction before. */
            *out = jc_arena_strdup(jc_app_scratch(app), reply->content);
        }
    } else if (http_status >= 400) {
        /* DEBUG, not WARN: chunking expects to hit this and recover. A
         * context-overflow 400 becomes JC_ERR_TOOBIG so the caller can shrink. */
        jc_logf(JC_LOG_DEBUG, "auto-compaction: summarizer HTTP %ld",
                http_status);
        st = (http_status == 400) ? JC_ERR_TOOBIG : JC_ERR_PROVIDER;
    } else if (st == JC_OK) {
        st = JC_ERR_PROVIDER;
    }

    free(resp);
    jc_history_free(&mini);
    if (*out != NULL) {
        return JC_OK;
    }
    return (st == JC_OK) ? JC_ERR_PROVIDER : st;
}

/* summarize_call, but on a context-overflow 400 halve the transcript (keeping
 * the oldest half) and retry, so an over-estimate of how much fits still
 * produces a summary instead of failing. */
static jc_status summarize_fit(struct jc_app *app, struct jc_provider *prov,
                               const char *transcript, char **out)
{
    const char *t = transcript;
    int tries;

    for (tries = 0; tries < 3; tries++) {
        jc_status st = summarize_call(app, prov, t, out);
        if (st != JC_ERR_TOOBIG) {
            return st;
        }
        {
            jc_size len = (jc_size)strlen(t);
            jc_size half = len / 2;
            struct jc_sb sb;
            if (half == 0) {
                return st;
            }
            jc_sb_init(&sb);
            jc_sb_append_n(&sb, t, jc_utf8_trunc_len(t, half));
            jc_sb_append(&sb, "\n...[earlier portion omitted]");
            t = jc_arena_strdup(jc_app_scratch(app),
                                sb.data != NULL ? sb.data : "");
            jc_sb_free(&sb);
        }
    }
    return JC_ERR_TOOBIG;
}

/* Summarize the prefix [0, cut) so the result fits the summarize model's
 * context: window the prefix into summarizer-sized chunks, summarize each, and
 * fold the partials into one summary. *out is arena-owned on success. */
static jc_status summarize_chunked(struct jc_app *app, struct jc_history *hist,
                                   jc_size cut, char **out)
{
    const struct jc_model_cfg *sm;
    struct jc_model_cfg smc;
    struct jc_provider *prov;
    long in_tokens;
    jc_size in_bytes;
    jc_size start;
    struct jc_sb combined;
    struct jc_arena *win;
    int nwin = 0;
    char *text;
    int pass;
    jc_status st;

    *out = NULL;
    sm = jc_app_model_for_role(app, JC_ROLE_SUMMARIZE);
    if (sm == NULL) {
        sm = &app->config.model;
    }
    in_tokens = summarizer_input_tokens(sm);
    in_bytes = (jc_size)(in_tokens * BYTES_PER_TOKEN);

    /* Inject a max_tokens for the summary output (the configured summarize model
     * may not set one), without mutating the shared config. */
    smc = *sm;
    if (smc.max_tokens <= 0) {
        smc.max_tokens = summarizer_output_tokens(sm);
    }
    prov = jc_provider_create(&smc);
    if (prov == NULL) {
        return JC_ERR_INVALID;
    }

    /* M218: the big intermediates (a rendered window is up to `in_bytes`,
     * once per window; a fold piece likewise, per pass) go to a build-local
     * arena RESET PER ITERATION (the M197 pattern), and the fold accumulator
     * is malloc-owned -- they used to pile up on the per-turn scratch, so a
     * huge compaction cost O(prefix x folds) until the NEXT turn began. Only
     * the final *out stays on scratch (the documented contract). */
    win = jc_arena_new(0);
    if (win == NULL) {
        prov->vt->free(prov);
        return JC_ERR_OOM;
    }

    /* Pass 1: summarize each message-window of the prefix. */
    jc_sb_init(&combined);
    start = 0;
    while (start < cut) {
        jc_size end = jc_compact_window_end(hist, start, in_tokens);
        char *tr;
        char *part = NULL;
        if (end > cut) {
            end = cut;
        }
        jc_arena_reset(win);
        tr = jc_compact_render_range(hist, start, end, win);
        if (tr != NULL && summarize_fit(app, prov, tr, &part) == JC_OK &&
            part != NULL) {
            if (combined.len > 0) {
                jc_sb_append(&combined, "\n\n");
            }
            jc_sb_append(&combined, part);
            nwin++;
        }
        if (end <= start) {
            break; /* safety: window_end always advances, but guard anyway */
        }
        start = end;
    }

    if (nwin == 0) {
        jc_sb_free(&combined);
        jc_arena_free(win);
        prov->vt->free(prov);
        return JC_ERR_PROVIDER;
    }
    if (nwin == 1) {
        *out = jc_arena_strdup(jc_app_scratch(app),
                               combined.data ? combined.data : "");
        jc_sb_free(&combined);
        jc_arena_free(win);
        prov->vt->free(prov);
        return (*out != NULL && (*out)[0] != '\0') ? JC_OK : JC_ERR_PROVIDER;
    }

    /* Fold: reduce the joined partials until they fit, then a unifying pass. */
    text = jc_sb_finish(&combined);
    for (pass = 0; pass < 3 && text != NULL &&
                   (long)strlen(text) / BYTES_PER_TOKEN > in_tokens; pass++) {
        struct jc_sb nx;
        jc_size off = 0;
        jc_size tlen = (jc_size)strlen(text);
        jc_sb_init(&nx);
        while (off < tlen) {
            jc_size w = (tlen - off < in_bytes) ? (tlen - off) : in_bytes;
            char *piece;
            char *p2 = NULL;
            jc_arena_reset(win);
            piece = jc_arena_strndup(win, text + off, w);
            if (piece != NULL && summarize_fit(app, prov, piece, &p2) == JC_OK &&
                p2 != NULL) {
                if (nx.len > 0) {
                    jc_sb_append(&nx, "\n\n");
                }
                jc_sb_append(&nx, p2);
            }
            off += w;
        }
        free(text);
        text = jc_sb_finish(&nx);
    }

    st = summarize_fit(app, prov, text != NULL ? text : "", out);
    if (st != JC_OK || *out == NULL) {
        /* Could not unify; fall back to the (bounded) joined partials. */
        *out = jc_arena_strdup(jc_app_scratch(app), text != NULL ? text : "");
    }
    free(text);
    jc_arena_free(win);
    prov->vt->free(prov);
    return (*out != NULL && (*out)[0] != '\0') ? JC_OK : JC_ERR_PROVIDER;
}

/* Shared core: decide a cut, summarize the prefix, and rewrite `hist`. Assumes
 * the caller has already decided that compaction is wanted. Returns JC_OK with
 * *did_compact set to 1 on a successful fold, 0 otherwise (never fails). */
static jc_status do_compact(struct jc_app *app, struct jc_history *hist,
                            int *did_compact)
{
    long keep_tokens;
    jc_size cut;
    char *summary = NULL;
    jc_status st;

    *did_compact = 0;

    keep_tokens = effective_limit(app) * 7 / 20; /* keep ~0.35 as recent tail */
    cut = jc_compact_find_cut(hist, keep_tokens);
    if (cut == 0) {
        return JC_OK; /* nothing old enough to fold away */
    }

    /* Summarize the prefix in summarizer-context-sized windows (M30), so a
     * small summarize model can't be handed more than it can hold. */
    st = summarize_chunked(app, hist, cut, &summary);
    if (st != JC_OK || summary == NULL || summary[0] == '\0') {
        jc_logf(JC_LOG_WARN,
                "auto-compaction: summarization failed; keeping full history");
        return JC_OK; /* never break the turn */
    }

    /* Prepend the summary to the first kept (user) message and drop the
     * summarized prefix. */
    /* M140: the `orig` intermediate inside apply is per-call scratch too. */
    jc_compact_apply(hist, cut, summary, jc_app_scratch(app));

    *did_compact = 1;
    if (!app->quiet) {
        fprintf(stderr, "[compacted %lu earlier messages into a summary]\n",
                (unsigned long)cut);
    }
    jc_logf(JC_LOG_DEBUG, "auto-compaction: folded %lu messages",
            (unsigned long)cut);
    return JC_OK;
}

jc_status jc_compact_run(struct jc_app *app, struct jc_history *hist,
                         const struct jc_agent_callbacks *cb, int *did_compact)
{
    long limit, est;
    int local = 0;

    (void)cb;
    if (did_compact != NULL) {
        *did_compact = 0;
    }
    if (!app->config.auto_compact) {
        return JC_OK;
    }

    limit = effective_limit(app);
    /* Scale the byte estimate to real tokens (M77) so the trigger fires against
     * the model's actual context usage, not a ~2x-optimistic guess. */
    est = jc_compact_effective_est(app, hist);              /* M536 */
    if (est <= limit * 4 / 5) {
        return JC_OK; /* below the trigger */
    }

    return do_compact(app, hist, did_compact != NULL ? did_compact : &local);
}

jc_status jc_compact_force(struct jc_app *app, struct jc_history *hist,
                           int *did_compact)
{
    int local = 0;
    return do_compact(app, hist, did_compact != NULL ? did_compact : &local);
}

/* Mid-turn compaction tuning (M76). */
#define MIDTURN_HIGH_PCT    80  /* act above this % of the effective limit  */
#define MIDTURN_TARGET_PCT  60  /* elide down to this %                     */
#define MIDTURN_KEEP_RECENT 6   /* never elide the last N messages          */
#define ELIDE_MIN_BYTES     800 /* only elide tool results larger than this */
#define ELIDE_HEAD          400 /* bytes kept from the start                */
#define ELIDE_TAIL          200 /* bytes kept from the end                  */

/* Elide one tool-result message's content to head+tail+marker. Returns 1 on a
 * real shrink, 0 otherwise (NULL/short content, or OOM). Skips content already
 * <= ELIDE_MIN_BYTES, which doubles as the idempotence guard: an already-elided
 * message (head+marker+tail ~660 B) is under the threshold and never re-elided.
 * The new text is built in a separate buffer first, so reading m->content stays
 * valid until jc_msg_set_content dups the new content and frees the old. */
static int elide_tool_msg(struct jc_message *m, jc_compact_spill_fn spill,
                          void *spill_ctx, int *preserved, int superseded)
{
    jc_size len;
    jc_size head;
    jc_size tail_off;
    struct jc_sb sb;
    char ticket[700];
    int ok;

    if (preserved != NULL) {
        *preserved = 0;
    }
    if (m == NULL || m->content == NULL) {
        return 0;
    }
    len = (jc_size)strlen(m->content);
    if (len <= (jc_size)ELIDE_MIN_BYTES) {
        return 0;
    }
    /* M348: explicit idempotence. The size floor used to double as the guard
     * (600 kept bytes + a ~70-byte marker < 800), but a claim-ticket marker
     * carries a path, and a deep enough $HOME could push an elided message
     * back over the floor -- and a second pass would then elide the marker
     * itself and preserve a ticket pointing at a ticket. */
    /* M354: the guard matches the SHARED prefix of every marker flavour --
     * "...to fit the context window" (pressure), ": superseded" (dedup) --
     * so no flavour can be re-elided by another pass. */
    if (strstr(m->content, "bytes of tool output elided") != NULL) {
        return 0;
    }
    /* Cut on character boundaries, not byte offsets (M191). A head ending in the
     * first byte of an em-dash -- or a tail starting on its last -- makes the
     * whole request body ill-formed UTF-8, and a strict server then rejects every
     * turn for the rest of the run, since the split byte stays in the history.
     * That is exactly how a zigodot run wedged: HTTP 500 in 40ms, forever.
     * See docs/ANECDOTES.md #22. */
    head = jc_utf8_trunc_len(m->content, (jc_size)ELIDE_HEAD);
    tail_off = jc_utf8_resync(m->content, len, len - (jc_size)ELIDE_TAIL);
    /* M348: the claim ticket. Offer the FULL original to the preservation
     * store; on success the marker names where the bytes went, so the model
     * can retrieve exactly what was taken instead of re-running the original
     * call at full price (the measured re-read loop). On failure -- or with
     * no spiller -- the marker is the old ticketless text, byte for byte. */
    ticket[0] = '\0';
    if (spill != NULL) {
        if (!spill(spill_ctx, m->content, len, ticket, sizeof(ticket))) {
            ticket[0] = '\0';
        }
    }
    jc_sb_init(&sb);
    jc_sb_append_n(&sb, m->content, head);
    if (superseded) {
        /* M354: the old marker said "to fit the context window" here, which
         * was false twice over -- the eager dedup runs at budget 0, under no
         * window pressure at all, and the marker withheld the one fact that
         * stops the re-read loop: the newer copy is still IN this
         * conversation. Say the true reason, and point below. */
        jc_sb_append_fmt(&sb,
            "\n... [%lu bytes of tool output elided: superseded -- a newer "
            "read of this same file appears LATER in this conversation; use "
            "that instead of re-reading the file] ...\n",
            (unsigned long)(tail_off - head));
    } else if (ticket[0] != '\0') {
        jc_sb_append_fmt(&sb,
            "\n... [%lu bytes of tool output elided to fit the context "
            "window; the complete result is preserved at %s -- read or "
            "search THAT path instead of re-running the call] ...\n",
            (unsigned long)(tail_off - head), ticket);
    } else {
        jc_sb_append_fmt(&sb,
            "\n... [%lu bytes of tool output elided to fit the context "
            "window] ...\n",
            (unsigned long)(tail_off - head));
    }
    jc_sb_append(&sb, m->content + tail_off);
    /* Only count a real shrink: on OOM jc_msg_set_content leaves the old
     * content in place, so the elision didn't happen. */
    ok = (jc_msg_set_content(m, sb.data != NULL ? sb.data : "") == JC_OK);
    if (ok && ticket[0] != '\0' && preserved != NULL) {
        *preserved = 1;
    }
    jc_sb_free(&sb);
    return ok;
}

/* If message `i` is a read_file tool RESULT, return a malloc'd copy of its
 * `path` argument (caller frees) and, via `off_out`/`lim_out`, the RANGE that
 * call requested; else NULL. The result carries a tool_call_id; the originating
 * call (matched across the assistant messages) supplies the tool name +
 * arguments.
 *
 * M287: the range is part of a read's identity. Without it, supersession matched
 * on path alone -- so a model paging a large file (lines 1-100, then 100-250)
 * had its FIRST page elided when the second landed, on the grounds that "the
 * later read carries the current content". For a paged read that is false: the
 * later read carries DIFFERENT lines, and the pass whose entire claim is zero
 * information loss was silently discarding content the model still needed. One
 * project's log shows 909 paged reads, so this fired routinely -- and plausibly
 * caused re-reads, since 82 of 142 advisory-firing re-reads immediately followed
 * another read_file. Defaults match read_file's own (offset 1, limit 0 = to the
 * end) and use the shared lenient numeric parse, so `{"limit": "100.0"}` keys
 * the same as `{"limit": 100}`. */
static char *read_path_dup(struct jc_history *hist, jc_size i,
                           long *off_out, long *lim_out)
{
    struct jc_message *m = jc_history_get(hist, i);
    const char *id;
    jc_size j;

    if (m == NULL || m->role != JC_ROLE_TOOL || m->tool_call_id == NULL) {
        return NULL;
    }
    id = m->tool_call_id;
    /* M218: scan BACKWARD from the result. Tool results directly follow
     * their assistant message in this history model (the Anthropic
     * serializer's tool_result grouping depends on exactly that adjacency),
     * so the originating call is almost always the nearest preceding
     * assistant message -- the old forward-from-0 scan made the caller's
     * per-message loop O(n^2) over a marathon history. Kept as "scan until
     * found", not "first assistant only", so an injected message (operator
     * steering lands as a user message at a round boundary) can never break
     * the match -- worst case is the old cost, never a wrong answer. */
    for (j = i; j > 0; j--) {
        struct jc_message *a = jc_history_get(hist, j - 1);
        jc_size k, nc;
        if (a == NULL || a->role != JC_ROLE_ASSISTANT) {
            continue;
        }
        nc = jc_msg_tool_call_count(a);
        for (k = 0; k < nc; k++) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(a, k);
            if (tc == NULL || tc->id == NULL || strcmp(tc->id, id) != 0) {
                continue;
            }
            /* Found the originating call. */
            if (tc->name == NULL || strcmp(tc->name, "read_file") != 0) {
                return NULL;
            }
            {
                cJSON *o = jc_json_parse(tc->arguments_json != NULL
                                         ? tc->arguments_json : "");
                const char *p = jc_json_get_str(o, "path", NULL);
                char *out = (p != NULL) ? jc_strdup(p) : NULL;
                if (out != NULL) {
                    *off_out = (long)jc_json_get_num_lenient(o, "offset", 1.0);
                    *lim_out = (long)jc_json_get_num_lenient(o, "limit", 0.0);
                }
                if (o != NULL) {
                    cJSON_Delete(o);
                }
                return out;
            }
        }
    }
    return NULL;
}

jc_size jc_compact_trim_tool_output(struct jc_history *hist, long budget_tokens,
                                    jc_size keep_recent)
{
    return jc_compact_trim_tool_output_ex(hist, budget_tokens, keep_recent,
                                          NULL, NULL, NULL);
}

jc_size jc_compact_trim_tool_output_ex(struct jc_history *hist,
                                       long budget_tokens,
                                       jc_size keep_recent,
                                       jc_compact_spill_fn spill,
                                       void *spill_ctx,
                                       jc_size *preserved_out)
{
    jc_size n;
    jc_size i;
    jc_size elided = 0;
    jc_size preserved = 0;

    if (preserved_out != NULL) {
        *preserved_out = 0;
    }
    if (hist == NULL) {
        return 0;
    }
    n = jc_history_len(hist);
    if (n <= keep_recent) {
        return 0;
    }
    for (i = 0; i + keep_recent < n; i++) {
        struct jc_message *m = jc_history_get(hist, i);
        int got_ticket = 0;
        if (m == NULL || m->role != JC_ROLE_TOOL) {
            continue;
        }
        if (elide_tool_msg(m, spill, spill_ctx, &got_ticket, 0)) {
            elided++;
            if (got_ticket) {
                preserved++;
            }
        }
        if (jc_compact_estimate_tokens(hist) + SYS_TOOLS_OVERHEAD
                <= budget_tokens) {
            break;
        }
    }
    if (elided > 0) {
        hist->gen++; /* M218: content changed; the next session save must write */
    }
    if (preserved_out != NULL) {
        *preserved_out = preserved;
    }
    return elided;
}

/* Build the M218 elision marker for one tool call: a compact, VALID JSON
 * object (the Anthropic serializer re-parses arguments_json; anything invalid
 * degrades to _unparsed_arguments) noting the elision and preserving the
 * call's path so the model still knows what it wrote. Returns a malloc'd
 * string (caller frees) or NULL on OOM. Built via cJSON rather than
 * hand-quoting: the path came out of a JSON document, so re-emitting it
 * through cJSON is the only escaping guaranteed to round-trip. No UTF-8
 * boundary risk here (M191 doesn't apply): the arguments are replaced
 * wholesale, never split. */
static char *elide_args_marker(const char *old_args, jc_size old_len)
{
    cJSON *o;
    cJSON *marker;
    const char *path;
    char note[256];             /* M289: the directive wording needs the room */
    char *out;

    marker = cJSON_CreateObject();
    if (marker == NULL) {
        return NULL;
    }
    /* M289: the note is DIRECTIVE, not merely descriptive. The old wording
     * ("arguments (N bytes) elided mid-turn to fit the context window") read as
     * a field value, and the model copied the whole object back as arguments.
     * Say plainly that these are not arguments and what to do instead -- the
     * ~60 extra bytes are nothing beside the kilobytes elided, and beside the
     * round-trip a copied marker wastes. */
    jc_snprintf(note, sizeof(note),
                "PLACEHOLDER, not arguments: %lu bytes of arguments were "
                "dropped from your context to save space and cannot be "
                "recovered. To repeat this call, send its real arguments.",
                (unsigned long)old_len);
    cJSON_AddStringToObject(marker, JC_COMPACT_ELIDED_KEY, note);
    /* Keep the target path: top-level `path` (write_file/edit_file), else the
     * first edit's path (apply_patch). Absent/foreign shapes just omit it. */
    o = jc_json_parse(old_args);
    path = jc_json_get_str(o, "path", NULL);
    if (path == NULL && o != NULL) {
        cJSON *edits = cJSON_GetObjectItem(o, "edits");
        cJSON *first = (edits != NULL) ? cJSON_GetArrayItem(edits, 0) : NULL;
        path = jc_json_get_str(first, "path", NULL);
    }
    if (path != NULL) {
        cJSON_AddStringToObject(marker, "path", path);
    }
    out = cJSON_PrintUnformatted(marker);
    cJSON_Delete(marker);
    if (o != NULL) {
        cJSON_Delete(o);
    }
    return out;
}

jc_size jc_compact_trim_tool_args(struct jc_history *hist, long budget_tokens,
                                  jc_size keep_recent)
{
    jc_size n;
    jc_size i;
    jc_size elided = 0;

    if (hist == NULL) {
        return 0;
    }
    n = jc_history_len(hist);
    if (n <= keep_recent) {
        return 0;
    }
    for (i = 0; i + keep_recent < n; i++) {
        struct jc_message *m = jc_history_get(hist, i);
        jc_size k, nc;
        if (m == NULL || m->role != JC_ROLE_ASSISTANT) {
            continue;
        }
        nc = jc_msg_tool_call_count(m);
        for (k = 0; k < nc; k++) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
            jc_size len;
            char *marker;
            if (tc == NULL || tc->arguments_json == NULL) {
                continue;
            }
            len = (jc_size)strlen(tc->arguments_json);
            /* <= ELIDE_MIN_BYTES is skipped: the idempotence guard (a marker
             * is ~100 B), and it keeps read_file/search_code arguments intact
             * for the M94 dedup's path lookup (read_path_dup parses them). */
            if (len <= (jc_size)ELIDE_MIN_BYTES) {
                continue;
            }
            marker = elide_args_marker(tc->arguments_json, len);
            if (marker == NULL) {
                continue;
            }
            if (jc_msg_tool_call_set_args(tc, marker) == JC_OK) {
                elided++;
            }
            free(marker);
        }
        if (elided > 0 &&
            jc_compact_estimate_tokens(hist) + SYS_TOOLS_OVERHEAD
                <= budget_tokens) {
            break;
        }
    }
    if (elided > 0) {
        hist->gen++; /* M218: content changed; the next session save must write */
    }
    return elided;
}

jc_size jc_compact_trim_superseded_reads(struct jc_history *hist,
                                         long budget_tokens,
                                         jc_size keep_recent,
                                         const char *cwd)
{
    jc_size n, i;
    jc_size elided = 0;
    char **paths;

    if (hist == NULL) {
        return 0;
    }
    n = jc_history_len(hist);
    if (n <= keep_recent) {
        return 0;
    }
    /* Resolve each message's read_file path once (NULL when not a read result). */
    paths = (char **)calloc((size_t)n, sizeof(char *));
    if (paths == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        long off = 1, lim = 0;
        paths[i] = read_path_dup(hist, i, &off, &lim);
        /* Compare canonical spellings so `src/vm.zig` and `/abs/src/vm.zig`
         * are one file (M192). A path jc_path_normalize refuses keeps its raw
         * spelling: the worst case is the pre-M192 missed dedup, never a wrong
         * match (which would elide a read that was not superseded). */
        if (cwd != NULL && paths[i] != NULL) {
            char norm[JC_PATH_MAX];
            if (jc_path_normalize(cwd, paths[i], norm, sizeof(norm)) == JC_OK) {
                char *dup = jc_strdup(norm);
                if (dup != NULL) {
                    free(paths[i]);
                    paths[i] = dup;
                }
            }
        }
        /* M287: fold the requested RANGE into the identity, so only a read of
         * the same slice can supersede this one. The '\n' separator cannot occur
         * in the normalized path, so distinct (path, range) pairs cannot collide
         * into one key. An allocation failure here leaves the path-only key --
         * the pre-M287 behaviour for that one message, i.e. degraded, not wrong
         * in a new way. */
        if (paths[i] != NULL) {
            char key[JC_PATH_MAX + 64];
            char *dup;
            jc_snprintf(key, sizeof(key), "%s\n%ld:%ld", paths[i], off, lim);
            dup = jc_strdup(key);
            if (dup != NULL) {
                free(paths[i]);
                paths[i] = dup;
            }
        }
    }
    for (i = 0; i + keep_recent < n; i++) {
        jc_size j;
        int superseded = 0;
        if (paths[i] == NULL) {
            continue;
        }
        /* Superseded iff the same path AND RANGE is read again later -- only then
         * does the later read carry this copy's content, making it a pure
         * duplicate (M287; a different range is paging, not duplication). */
        for (j = i + 1; j < n; j++) {
            if (paths[j] != NULL && strcmp(paths[j], paths[i]) == 0) {
                superseded = 1;
                break;
            }
        }
        if (!superseded) {
            continue;
        }
        /* M348: deliberately ticketless -- a superseded read is zero-loss by
         * construction (the newest read of the same path/range is still in
         * context), so preserving it would duplicate the store. */
        if (elide_tool_msg(jc_history_get(hist, i), NULL, NULL, NULL,
                           1)) {
            elided++;
        }
        if (jc_compact_estimate_tokens(hist) + SYS_TOOLS_OVERHEAD
                <= budget_tokens) {
            break;
        }
    }
    for (i = 0; i < n; i++) {
        free(paths[i]);
    }
    free(paths);
    if (elided > 0) {
        hist->gen++; /* M218: content changed; the next session save must write */
    }
    return elided;
}

/* M348: the claim-ticket writer the lossy trim pass calls (jc_compact_spill_fn
 * shape). Thin: the store, its 0600/atomic discipline and the D3 rule all live
 * in jc_toolout_preserve, shared with the M339 over-cap spill. */
static int midturn_spill(void *vctx, const char *text, jc_size len,
                         char *path_out, jc_size path_cap)
{
    return jc_toolout_preserve((struct jc_app *)vctx, "elided", text, len,
                               path_out, path_cap);
}

/* M361: oldest protected candidate -> the exact length that releases it.
 * A message at index i is protected while len < i + keep + 1, so the scan
 * covers [len-keep, len). A candidate is a tool result, or an assistant
 * message with a tool call whose arguments are, larger than min_bytes --
 * conservative on purpose: a wrong "candidate" only costs one early scan at
 * its release length, and the no-candidate horizon (len + keep + 1) bounds
 * every latch to at most keep+1 appends. */
jc_size jc_compact_rearm_len(const struct jc_history *hist,
                             jc_size keep_recent, jc_size min_bytes)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n = jc_history_len(h);
    jc_size start = (n > keep_recent) ? n - keep_recent : 0;
    jc_size i;

    for (i = start; i < n; i++) {
        struct jc_message *m = jc_history_get(h, i);
        if (m == NULL) {
            continue;
        }
        if (m->role == JC_ROLE_TOOL && m->content != NULL &&
            (jc_size)strlen(m->content) > min_bytes) {
            return i + keep_recent + 1;
        }
        if (m->role == JC_ROLE_ASSISTANT) {
            jc_size k, nc = jc_msg_tool_call_count(m);
            for (k = 0; k < nc; k++) {
                struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
                if (tc != NULL && tc->arguments_json != NULL &&
                    (jc_size)strlen(tc->arguments_json) > min_bytes) {
                    return i + keep_recent + 1;
                }
            }
        }
    }
    return n + keep_recent + 1;
}

jc_size jc_compact_midturn(struct jc_app *app, struct jc_history *hist,
                           const struct jc_agent_callbacks *cb,
                           struct jc_midturn_report *rep,
                           int dedup_hint,
                           struct jc_midturn_latch *latch)
{
    long limit;
    long effective;
    long target_est;
    double ratio = 1.0;
    jc_size elided;
    jc_size dup;
    struct jc_midturn_report local;

    /* M323: report into a local unconditionally, then publish -- so every early
     * return below carries whatever was true at that point without each one
     * having to remember to fill the caller's struct. */
    memset(&local, 0, sizeof(local));
    if (rep != NULL) {
        *rep = local;
    }
    if (app == NULL || hist == NULL) {
        return 0;
    }
    /* M94: eagerly drop superseded reads, independent of budget pressure.
     * They are pure duplicates (the latest read of each file is kept), so
     * eliding them is zero-loss; on a cacheless backend the context ramp
     * happens well below the high-water, so waiting for the 80% trigger would
     * re-bill every duplicate for many turns first. Quiet (no on_status);
     * budget 0 => never early-stops => elides all superseded.
     *
     * M218: gated on `dedup_hint` -- a read becomes superseded only when a
     * LATER read of the same path lands, so a round that appended no
     * read_file result cannot have created a new pair, and the pass (a
     * per-message args parse + path normalize) ran ~165k cJSON parses across
     * one marathon turn for nothing. The caller passes 1 on the first round
     * of a turn (a resumed history may carry pre-existing duplicates) and on
     * any round that appended a read result; elision timing is bit-identical
     * to the unconditional pass. */
    dup = dedup_hint
        ? jc_compact_trim_superseded_reads(hist, 0,
                                           (jc_size)MIDTURN_KEEP_RECENT,
                                           app->cwd)
        : 0;
    local.dup = dup;
    local.elided = dup;
    if (rep != NULL) {
        *rep = local;
    }
    if (dup > 0) {
        jc_logf(JC_LOG_INFO,
                "[compact] mid-turn: dropped %lu superseded read(s)",
                (unsigned long)dup);
    }
    limit = jc_compact_context_limit(app);
    local.limit = limit;
    /* M326x: the target is a property of the LIMIT, not of this pass, so set it
     * here rather than only on the pressured path. It used to be left at 0 by
     * the memset whenever the early return below was taken, and a reader --
     * including jichi's own summarizer -- then saw `target: 0` and a pass that
     * had "failed to reach" it. */
    local.target = (limit > 0)
        ? (long)((double)(limit * MIDTURN_TARGET_PCT / 100)) : 0;
    if (limit <= 0) {
        if (rep != NULL) {
            *rep = local;
        }
        return dup;                 /* unknown budget: only the eager dedup ran */
    }
    /* The byte/4 estimate runs optimistic vs a real tokenizer, so calibrate it
     * to the model's learned prompt_tokens/estimate ratio (M77, persisted and
     * updated from every model call's usage) -- otherwise the trigger keys off
     * a number ~2x too small and never fires before a real overflow (the
     * dogfood found the in-turn context hit ~145k while the estimate was ~72k).
     * ratio scales the estimate UP toward reality; both the trigger and the
     * trim target are evaluated in those calibrated terms. jc_compact_effective_est
     * IS that calibrated sum (M536); ratio stays for the trim target below. */
    ratio = jc_compact_calibration(app);
    effective = jc_compact_effective_est(app, hist);
    local.before = effective;
    local.after = effective;
    if (effective <= limit * MIDTURN_HIGH_PCT / 100) {
        if (rep != NULL) {
            *rep = local;
        }
        return dup;                 /* under pressure: eager dedup was enough */
    }
    /* Past here the high-water trigger HAS fired, which is the fact a reader
     * most needs: it means the request was over the comfortable mark and this
     * pass was the only thing standing between it and the limit. */
    local.pressed = 1;
    /* M361: the exhaustion latch. A previous pressed pass proved the eligible
     * range dry; until the keep-recent window releases a candidate (an exact
     * length, computed then), the lossy scans below would reclaim nothing --
     * the measured shape is 174 of 593 pressured passes running 26th-or-later
     * in their turn for zero. A history that SHRANK re-arms immediately: the
     * latch's index math is stale after a truncation. Skipping is CPU-only:
     * the estimate, `pressed`, and the telemetry event above/below are
     * unchanged, so observability keeps telling the truth about the pressure.
     */
    if (latch != NULL && latch->rearm_len > 0) {
        jc_size hlen = jc_history_len(hist);
        if (hlen >= latch->rearm_len || hlen < latch->latch_len) {
            latch->rearm_len = 0;   /* released, or shape changed: re-arm */
            latch->latch_len = 0;
        } else {
            local.latched = 1;
            local.reached = local.after <= local.target;
            local.unrelieved = local.after > limit * MIDTURN_HIGH_PCT / 100;
            if (rep != NULL) {
                *rep = local;
            }
            return dup;
        }
    }
    /* Convert the real-token target back into the byte-estimate units the
     * trimmer compares against. Superseded reads are already gone (eager pass
     * above), so this is the age-based fallback for the remaining bulk. */
    target_est = (long)((double)(limit * MIDTURN_TARGET_PCT / 100) / ratio);
    /* M348: the LOSSY pass writes claim tickets -- each elision's full
     * content goes to the preservation store and the marker names the path,
     * so the model retrieves exactly what was taken instead of re-running
     * the original call (the measured re-read loop: 72% of reads were
     * re-reads). The superseded-read dedup above stays ticketless on
     * purpose: it is zero-loss by construction (the newest read of the same
     * range is still in context), so a ticket would duplicate the store. */
    elided = dup + jc_compact_trim_tool_output_ex(hist, target_est,
                                                  (jc_size)MIDTURN_KEEP_RECENT,
                                                  midturn_spill, app,
                                                  &local.preserved);
    /* M218: if the result-side trims were not enough, the bulk lives on the
     * ARGUMENTS side (write_file/apply_patch bodies in the assistant
     * messages) -- the vector the result trims cannot reach. Same target,
     * same keep-recent window; reported separately (`args_out`) because it is
     * lossy in a different way than result elision. */
    if (jc_compact_estimate_tokens(hist) + SYS_TOOLS_OVERHEAD > target_est) {
        jc_size nargs = jc_compact_trim_tool_args(hist, target_est,
                                                  (jc_size)MIDTURN_KEEP_RECENT);
        local.args = nargs;
        elided += nargs;
    }
    local.elided = elided;
    local.after = jc_compact_effective_est(app, hist);      /* M536 */
    local.reached = local.after <= local.target;
    /* Did this pass actually RELIEVE the pressure? Not "did it reach target"
     * (that is `reached`, the 60% mark) -- a pass can miss target and still get
     * back under the 80% high-water, which buys several quiet rounds. A pass
     * that ends still above the high-water re-triggers on the very next round:
     * that is the thrash, stated exactly.
     *
     * Defining this by elision COUNT was the first attempt and it was wrong:
     * the repeating passes do elide something, typically one small item, and
     * reclaim ~0 tokens. Counting what was taken cannot express that; measuring
     * what it achieved can. */
    local.unrelieved = local.after > limit * MIDTURN_HIGH_PCT / 100;
    /* M361: this pressed pass ran the lossy trims and they found NOTHING
     * (content and args both zero -- `dup` is the eager dedup, not lossy).
     * The eligible range is dry; latch until the window releases something. */
    if (latch != NULL && elided - dup == 0 && local.args == 0) {
        latch->rearm_len = jc_compact_rearm_len(hist,
                                                (jc_size)MIDTURN_KEEP_RECENT,
                                                (jc_size)ELIDE_MIN_BYTES);
        latch->latch_len = jc_history_len(hist);
    }
    if (rep != NULL) {
        *rep = local;
    }
    if (elided > 0) {
        jc_logf(JC_LOG_INFO,
                "[compact] mid-turn: elided %lu old tool output(s) "
                "(~%ld -> ~%ld real tok of ~%ld)",
                (unsigned long)elided, effective, local.after, limit);
        if (cb != NULL && cb->on_status != NULL) {
            cb->on_status(cb->user,
                "compacting (mid-turn): elided old tool output to fit context");
        }
    }
    /* M323: a short-fall is REPORTED, not announced, from here. This runs after
     * every round of tool results -- 1,038 times in the workload that motivated
     * it -- so warning from inside would be 1,038 warnings for one condition.
     * The caller owns the throttling because only it knows where a turn begins;
     * `rep->pressed && !rep->reached` is the whole signal. */
    return elided;
}
