/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_calib.h - persistent per-model token-estimate calibration (M77).
 *
 * jichi estimates a request's token cost with a cheap byte heuristic (~bytes/4;
 * see jc_compact.c). Real tokenizers run denser -- for the models dogfooded so
 * far the true cost is ~2x the estimate -- so every context decision keyed off
 * the raw estimate (the compaction trigger, mid-turn elision, system-prompt
 * fitting, the /context breakdown, the TUI context%) fires late or reads low.
 *
 * This module learns the real `prompt_tokens / estimate` ratio PER MODEL from
 * the usage numbers the provider already returns, persists it across sessions,
 * and hands it back so those consumers can scale the estimate to reality. The
 * ratio is a running, window-capped average (so it keeps adapting) clamped to a
 * sane band; an uncalibrated model returns 1.0 (no correction).
 *
 * The math (clamp / blend) is pure and unit-tested; load/save is JSON I/O to a
 * single file OUTSIDE any workspace (~/.jichi.d/calibration.json), per
 * the observability-blast-radius lesson. See docs/COMPACTION.md.
 */
#ifndef JC_CALIB_H
#define JC_CALIB_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_arena; /* jc_mem.h */

/* Ratio clamp band: below 0.5 or above 8.0 is almost certainly a bad sample
 * (a truncated request, a cache-only turn), so refuse to record it. */
#define JC_CALIB_MIN        0.5
#define JC_CALIB_MAX        8.0
/* Effective averaging window: after this many samples the blend stops shrinking
 * the update weight, so the ratio keeps tracking drift instead of freezing. */
#define JC_CALIB_WINDOW     20
/* Cap the table so a shared file can't grow without bound. */
#define JC_CALIB_MAX_ENTRIES 128

/* Schema version of the persisted table, written as the top-level "v" key.
 *
 * A ratio is only comparable to others measured against the SAME estimate basis,
 * so the basis is part of the file format. Version 1 (implicit: no "v" key) was
 * learned against `history + 2000`, a flat stand-in for the system prompt and
 * tool schemas that understated them by 5-9k tokens; version 2 (M286) is learned
 * against the measured system + tools + history. A v1 ratio is systematically
 * too high -- one dogfooded model persisted 2.717 where the honest basis gives
 * 1.19 -- so v1 entries are DISCARDED on load rather than blended down over
 * thousands of samples. Each model re-learns within its first turn. Bump this
 * whenever the estimate basis changes again. */
#define JC_CALIB_SCHEMA     2

struct jc_calib_entry {
    const char *model_id;   /* arena-owned key (the wire model id) */
    double      ratio;      /* real prompt_tokens / estimated tokens */
    long        samples;    /* observations folded into `ratio`     */
};

struct jc_calib {
    struct jc_vec    entries;  /* of struct jc_calib_entry */
    struct jc_arena *arena;    /* backs model_id + path strings */
    const char      *path;     /* file to persist to (arena-owned), or NULL */
    int              dirty;    /* an observation changed the table */
};

/* Clamp a ratio into [JC_CALIB_MIN, JC_CALIB_MAX]. Pure. */
double jc_calib_clamp(double r);

/* Fold `sample` into a running average given the prior `ratio` and how many
 * samples it already represents. The first sample (samples <= 0) is taken
 * verbatim (clamped); later ones move `ratio` toward `sample` by 1/(n+1),
 * with n capped at JC_CALIB_WINDOW. Result is clamped. Pure. */
double jc_calib_blend(double ratio, long samples, double sample);

/* Initialise an empty table backed by `a`. */
void jc_calib_init(struct jc_calib *c, struct jc_arena *a);
void jc_calib_free(struct jc_calib *c);

/* The learned ratio for `model_id`, or 1.0 when unknown/uncalibrated (so an
 * uncalibrated model applies no correction). */
double jc_calib_get(const struct jc_calib *c, const char *model_id);

/* Record one observation: the provider reported `real` prompt tokens for a
 * request our heuristic put at `est` tokens. No-op unless both are positive and
 * the resulting ratio is in-band. Sets `dirty`. */
void jc_calib_observe(struct jc_calib *c, const char *model_id,
                      long real, long est);

/* Load the table from `path` (stashed for jc_calib_save). A missing file is not
 * an error (the table stays empty). Clears `dirty`. */
jc_status jc_calib_load(struct jc_calib *c, const char *path);

/* Write the table back to its stashed path if `dirty`. Creates the parent
 * directory as needed. No-op (JC_OK) when clean or when no path was set. */
jc_status jc_calib_save(struct jc_calib *c);

#ifdef __cplusplus
}
#endif
#endif /* JC_CALIB_H */
