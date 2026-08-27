/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_promptcache.h - pure placement of Anthropic prompt-cache breakpoints (M31b).
 *
 * Prompt caching on the Anthropic Messages API is explicit: a
 * `cache_control: {"type":"ephemeral"}` marker on a content block caches the
 * request prefix up to (and including) that block. Render order is
 * tools -> system -> messages, so a breakpoint on the system block caches the
 * (stable) tools + system prefix, and a breakpoint on the tail of the
 * conversation caches the growing history so each subsequent turn reads the
 * prior prefix from cache.
 *
 * This module is the pure, unit-tested decision of *where* the breakpoints go;
 * the Anthropic provider does the JSON wiring. The total breakpoint count never
 * exceeds JC_PROMPTCACHE_MAX (the API's per-request limit).
 */
#ifndef JC_PROMPTCACHE_H
#define JC_PROMPTCACHE_H


#ifdef __cplusplus
extern "C" {
#endif
/* The Anthropic API allows at most this many cache_control breakpoints. */
#define JC_PROMPTCACHE_MAX 4

struct jc_promptcache_plan {
    int cache_system;  /* 1 => put cache_control on the system block (also
                        * caches the tools array, which renders first) */
    int msg_index;     /* messages[] index whose last content block gets a
                        * breakpoint (the conversation tail), or -1 for none */
};

/* Decide breakpoint placement. When `enabled` is 0 the plan is empty. Otherwise
 * the system block is cached and, when there is at least one message, the last
 * message (the most recent turn) carries a breakpoint so the whole prior prefix
 * caches incrementally. cache_system + (msg_index >= 0) is at most 2, well
 * within JC_PROMPTCACHE_MAX. Pure. */
void jc_promptcache_plan(int enabled, int n_messages,
                         struct jc_promptcache_plan *out);

/* M340: the smallest block Anthropic will cache, in tokens, for `model_id`.
 * Returns 0 when there is no published minimum jichi can claim to know.
 *
 * This exists because a prefix below the minimum caches NOTHING AND SAYS NOTHING:
 * a measurement round was spent on a config with promptCache on, a
 * caching-capable model, and a 397-token prefix, which reported a flat zero with
 * no diagnostic anywhere (docs/analysis/2026-08-09-hrz-prompt-caching.md).
 *
 * The numbers are Anthropic's published minimums -- 2048 for Haiku, 1024 for the
 * Sonnet and Opus lines -- matched on the model id, because that is the only
 * thing jichi has. An id it does not recognise returns 0 and warns about
 * nothing: a false "this will not cache" would have an operator delete a prefix
 * that was working, which is worse than the silence this replaces. */
long jc_promptcache_min_tokens(const char *model_id);

#ifdef __cplusplus
}
#endif
#endif /* JC_PROMPTCACHE_H */
