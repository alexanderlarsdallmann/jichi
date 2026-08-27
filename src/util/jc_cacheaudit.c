/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_cacheaudit.c - prompt-cache audit over telemetry (see jc_cacheaudit.h). */

#include "jc_cacheaudit.h"
#include "jc_telemetry.h"
#include "jc_str.h"
#include "jc_vec.h"

/* Below this many input tokens there isn't enough signal to judge caching. */
#define JC_CACHEAUDIT_MIN_VOLUME 2000.0

int jc_cacheaudit_hitrate(double cache_read, double uncached_in)
{
    double total = cache_read + uncached_in;
    if (total < 1.0) {
        return -1;
    }
    return (int)((cache_read * 100.0) / total);
}

int jc_cacheaudit_verdict(int hitrate, double volume)
{
    if (hitrate < 0 || volume < JC_CACHEAUDIT_MIN_VOLUME) {
        return JC_CACHE_NODATA;
    }
    if (hitrate < 5) {
        return JC_CACHE_NONE;
    }
    if (hitrate < 60) {
        return JC_CACHE_PARTIAL;
    }
    return JC_CACHE_GOOD;
}

const char *jc_cacheaudit_verdict_name(int verdict)
{
    switch (verdict) {
    case JC_CACHE_NONE:    return "no cache reported";
    case JC_CACHE_PARTIAL: return "partial";
    case JC_CACHE_GOOD:    return "good";
    default:               return "insufficient data";
    }
}

void jc_cacheaudit_totals(const struct jc_telemetry_summary *s,
                          double *in_tok, double *cache_read,
                          double *cache_write, long *calls)
{
    double t_in = 0.0;
    double t_read = 0.0;
    double t_write = 0.0;
    long t_calls = 0;
    jc_size i;

    for (i = 0; i < s->models.len; i++) {
        struct jc_telem_model *m =
            (struct jc_telem_model *)jc_vec_at((struct jc_vec *)&s->models, i);
        t_in += m->in_tok;
        t_read += m->cache_read;
        t_write += m->cache_write;
        t_calls += m->calls;
    }
    if (in_tok != NULL)     *in_tok = t_in;
    if (cache_read != NULL) *cache_read = t_read;
    if (cache_write != NULL) *cache_write = t_write;
    if (calls != NULL)      *calls = t_calls;
}

double jc_cacheaudit_prefix(const struct jc_telemetry_summary *s)
{
    double sys_sum = 0.0;
    double tools_sum = 0.0;
    long n = 0;
    jc_size i;

    for (i = 0; i < s->models.len; i++) {
        struct jc_telem_model *m =
            (struct jc_telem_model *)jc_vec_at((struct jc_vec *)&s->models, i);
        sys_sum += m->sys_tok;
        tools_sum += m->tools_tok;
        n += m->attr_n;
    }
    if (n <= 0) {
        return 0.0;   /* a pre-M192 log carries no attribution */
    }
    return (sys_sum + tools_sum) / (double)n;
}

void jc_cacheaudit_render(const struct jc_telemetry_summary *s,
                          struct jc_sb *out)
{
    double t_in = 0.0;
    double t_read = 0.0;
    double t_write = 0.0;
    long t_calls = 0;
    int overall_hr;
    int overall_v;
    jc_size i;

    jc_cacheaudit_totals(s, &t_in, &t_read, &t_write, &t_calls);
    overall_hr = jc_cacheaudit_hitrate(t_read, t_in);
    overall_v = jc_cacheaudit_verdict(overall_hr, t_read + t_in);

    jc_sb_append(out, "== Prompt-cache audit ==\n\n");
    if (t_calls == 0) {
        jc_sb_append(out, "No model_call events in this log (run with "
                          "--log-level metrics).\n");
        return;
    }
    jc_sb_append_fmt(out, "overall: %d%% cache hit-rate over %.0f input tokens "
                     "across %ld calls -- %s\n", overall_hr < 0 ? 0 : overall_hr,
                     t_read + t_in, t_calls,
                     jc_cacheaudit_verdict_name(overall_v));
    jc_sb_append_fmt(out, "  cached read: %.0f   cache write: %.0f   uncached "
                     "(billed): %.0f\n\n", t_read, t_write, t_in);

    jc_sb_append(out, "Per model:\n");
    for (i = 0; i < s->models.len; i++) {
        struct jc_telem_model *m =
            (struct jc_telem_model *)jc_vec_at((struct jc_vec *)&s->models, i);
        int hr = jc_cacheaudit_hitrate(m->cache_read, m->in_tok);
        int v = jc_cacheaudit_verdict(hr, m->cache_read + m->in_tok);
        jc_sb_append_fmt(out, "  %-28s %3d%% (read %.0f / uncached %.0f)  %s\n",
                         m->name, hr < 0 ? 0 : hr, m->cache_read, m->in_tok,
                         jc_cacheaudit_verdict_name(v));
    }

    if (s->sessions.len > 0) {
        jc_sb_append(out, "\nPer-session input ramp (the cost the missing "
                          "cache makes dominant):\n");
        for (i = 0; i < s->sessions.len && i < 8; i++) {
            struct jc_telem_session *ss =
                (struct jc_telem_session *)jc_vec_at(
                    (struct jc_vec *)&s->sessions, i);
            char sid8[9];
            int k;
            for (k = 0; k < 8 && ss->sid[k] != '\0'; k++) {
                sid8[k] = ss->sid[k];
            }
            sid8[k] = '\0';
            jc_sb_append_fmt(out, "  %-8s %3ld calls, peak input %.0f, "
                             "cost $%.4f\n", sid8, ss->calls, ss->peak_in,
                             ss->cost);
        }
    }

    jc_sb_append(out, "\nRecommendation: ");
    if (overall_v == JC_CACHE_NONE) {
        jc_sb_append_fmt(out, "this backend REPORTED no prefix reuse "
            "(~%d%% hit-rate over %.0f uncached tokens). A server can cache "
            "without reporting it (measured: 18.6x repeat-prefill latency "
            "with nothing on the wire), so confirm with the latency probe "
            "(tests/bench/cache_probe.py) before acting. ",
            overall_hr < 0 ? 0 : overall_hr, t_in);
        jc_sb_append(out, "If it is truly uncached, every turn re-bills the "
            "whole prefix. Options: use a prompt-cache-capable model/backend; "
            "keep sessions short (/clear, shorter --auto runs); shrink the "
            "always-sent prefix (smaller repoMap / instruction files, or "
            "toolProfile=core).\n");
    } else if (overall_v == JC_CACHE_PARTIAL) {
        jc_sb_append(out, "caching is partial. If a model's per-model rate above "
            "is unexpectedly low, check its promptCache setting and whether its "
            "server honors cache_control; ensure the prefix (system+tools) is "
            "stable across turns.\n");
    } else if (overall_v == JC_CACHE_GOOD) {
        jc_sb_append(out, "caching is working well; the prefix is being reused. "
            "No action needed.\n");
    } else {
        jc_sb_append(out, "not enough input volume yet to judge caching; re-run "
            "the audit after a longer session.\n");
    }
}
