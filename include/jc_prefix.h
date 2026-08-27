/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_prefix.h - the prompt-cache prefix sentinel (M365).
 *
 * The M31 contract: within a session, the request prefix (system prompt +
 * tools) must stay byte-stable between calls, or the provider-side cache
 * silently stops matching and every request re-bills the whole prefix -- no
 * error, no wrong behavior, only cost (a working prefix measured 84% cheaper
 * on the HRZ gateway). The contract is upheld by every system-prompt section
 * author by hand; this is the runtime checker that did not exist.
 *
 * The watch is deliberately slow to accuse: ONE change is normal life (a
 * memory write, a mode switch, midnight moving the date line, /compact) --
 * only a hash that changes on N consecutive turns is churn, because no
 * legitimate cause fires every turn. The first proven churn source was M77
 * calibration jitter moving the M73 fit-cap truncation byte each turn (fixed
 * in the same milestone by quantizing the fit budget); this sentinel exists
 * for the next source, which will not announce itself either.
 */
#ifndef JC_PREFIX_H
#define JC_PREFIX_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FNV-1a over a NUL-terminated string (32-bit folded into unsigned long).
 * NULL hashes like "" -- the watch cares about change, not identity. */
unsigned long jc_prefix_hash(const char *s);

struct jc_prefix_watch {
    unsigned long last;    /* previous hash                               */
    int           primed;  /* a first hash has been recorded              */
    int           streak;  /* consecutive tracks whose hash CHANGED       */
    int           warned;  /* the one-per-session warning has fired       */
};

/* Number of consecutive changed builds that count as churn. */
#define JC_PREFIX_CHURN_STREAK 3

/* Record one build's hash. Returns 1 exactly once per watch lifetime: on the
 * JC_PREFIX_CHURN_STREAK-th consecutive build whose hash differs from the
 * one before it. A stable build resets the streak. */
int jc_prefix_watch_track(struct jc_prefix_watch *w, unsigned long hash);

#ifdef __cplusplus
}
#endif

#endif /* JC_PREFIX_H */
