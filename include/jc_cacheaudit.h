/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_cacheaudit.h - prompt-cache audit over a telemetry summary (M104).
 *
 * On a cacheless (or misconfigured) backend the dominant cost is *structural
 * re-billing*: every turn re-sends and re-bills the whole request prefix
 * (system + tools + growing history) because the server never serves it as a
 * cache read. jichi's own prefix is byte-stable (guarded by the M31 test), so the
 * question at runtime is "is the backend actually caching it, and if not, how
 * much is that costing?". This module answers that from the existing
 * `model_call` telemetry (uncached input vs cache_read/cache_write) — no new
 * instrumentation. Pure; the I/O + flag live in main.c's `telemetry`.
 */
#ifndef JC_CACHEAUDIT_H
#define JC_CACHEAUDIT_H


#ifdef __cplusplus
extern "C" {
#endif
struct jc_telemetry_summary; /* jc_telemetry.h */
struct jc_sb;                /* jc_str.h        */

enum jc_cache_verdict {
    JC_CACHE_NODATA,   /* negligible input volume to judge      */
    JC_CACHE_NONE,     /* ~0 cache reads over real volume       */
    JC_CACHE_PARTIAL,  /* some reuse                             */
    JC_CACHE_GOOD      /* most of the prefix served from cache   */
};

/* Cache hit-rate percentage from cached vs uncached input tokens, or -1 when
 * there is no meaningful input volume. Pure. */
int jc_cacheaudit_hitrate(double cache_read, double uncached_in);

/* Classify a hit-rate (-1 == no data) against total input `volume` (tokens).
 * Returns an enum jc_cache_verdict. Pure. */
int jc_cacheaudit_verdict(int hitrate, double volume);

/* Human name for a verdict (never NULL). */
const char *jc_cacheaudit_verdict_name(int verdict);

/* Sum the per-model rows into the overall totals. Any output pointer may be
 * NULL. Pure.
 *
 * Factored out of jc_cacheaudit_render at M326w so `doctor` reaches the same
 * verdict from the same arithmetic: two reports on one thing showing two
 * totals is the drift M310-M313 each had to fix, and a second summing loop
 * in main.c is exactly how it starts. */
void jc_cacheaudit_totals(const struct jc_telemetry_summary *s,
                          double *in_tok, double *cache_read,
                          double *cache_write, long *calls);

/* Mean FIXED prefix (system prompt + tool definitions) per call, in tokens, or
 * 0 when the log carries no M192 attribution. Pure.
 *
 * This is the number that makes a cacheless backend concrete: on a caching one
 * the prefix is paid once and read back thereafter; without a cache it is
 * re-sent, and re-billed, on every call -- and a turn makes tens of them. */
double jc_cacheaudit_prefix(const struct jc_telemetry_summary *s);

/* Render the full cache-audit report (overall + per-model + per-session ramp +
 * an actionable recommendation) from `s` into `out`. Pure. */
void jc_cacheaudit_render(const struct jc_telemetry_summary *s,
                          struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_CACHEAUDIT_H */
