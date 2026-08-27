/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_promptcache.c - pure prompt-cache breakpoint placement (see header). */

#include "jc_promptcache.h"
#include <string.h>
#include <stdlib.h>

void jc_promptcache_plan(int enabled, int n_messages,
                         struct jc_promptcache_plan *out)
{
    out->cache_system = 0;
    out->msg_index = -1;
    if (!enabled) {
        return;
    }
    out->cache_system = 1;
    if (n_messages > 0) {
        out->msg_index = n_messages - 1;
    }
}

long jc_promptcache_min_tokens(const char *model_id)
{
    const char *p;

    if (model_id == NULL || model_id[0] == '\0') {
        return 0;
    }
    /* MEASURED, not read off a documentation page (M341). The published figures
     * -- 1024 for Opus/Sonnet, 2048 for Haiku -- describe an older generation and
     * are WRONG for the 4.5 models, which is what made M340 emit a false OK on a
     * 3897-token prefix that cached nothing. Bracketed against the wire:
     *
     *   claude-haiku-4-5 : 3900 no cache, 4101 cached  -> 4096
     *   claude-opus-4-5  : 3160 no cache, 6310 cached  -> 4096
     *   claude-sonnet-4-5: 1060 cached                 -> <= 1024
     *
     * Substring match: a proxy prepends its namespace, which is the shape this
     * was found on. Sonnet is tested BEFORE the 4096 pair so an id naming both
     * does not get the stricter bound by accident -- the specific match wins.
     *
     * An older model (haiku-3-5 at 2048, opus-3 at 1024) gets the 4096 figure
     * here, so the warning is CONSERVATIVE: it may say "too small" where caching
     * would in fact have worked. That is the deliberate direction now. M340
     * reasoned the opposite way and shipped a check that stayed silent through
     * exactly the failure it was built for; a warning you can dismiss costs less
     * than a reassurance that is wrong. */
    for (p = model_id; *p != '\0'; p++) {
        if (*p == 's' && strncmp(p, "sonnet", 6) == 0) {
            return 1024;
        }
    }
    for (p = model_id; *p != '\0'; p++) {
        if ((*p == 'h' && strncmp(p, "haiku", 5) == 0) ||
            (*p == 'o' && strncmp(p, "opus", 4) == 0)) {
            return 4096;
        }
    }
    return 0;   /* unrecognised: say nothing rather than guess */
}
