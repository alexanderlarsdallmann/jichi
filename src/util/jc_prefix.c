/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_prefix.c - the prompt-cache prefix sentinel (see jc_prefix.h). */

#include "jc_prefix.h"

unsigned long jc_prefix_hash(const char *s)
{
    /* FNV-1a, 32-bit constants; unsigned long is >= 32 bits in C89, and the
     * fold keeps the value stable across 32/64-bit longs. */
    unsigned long h = 2166136261UL;
    if (s == NULL) {
        s = "";
    }
    while (*s != '\0') {
        h ^= (unsigned long)(unsigned char)*s++;
        h = (h * 16777619UL) & 0xffffffffUL;
    }
    return h;
}

int jc_prefix_watch_track(struct jc_prefix_watch *w, unsigned long hash)
{
    if (w == NULL) {
        return 0;
    }
    if (!w->primed) {
        w->primed = 1;
        w->last = hash;
        w->streak = 0;
        return 0;
    }
    if (hash == w->last) {
        w->streak = 0;
        return 0;
    }
    w->last = hash;
    w->streak++;
    if (w->streak >= JC_PREFIX_CHURN_STREAK && !w->warned) {
        w->warned = 1;
        return 1;
    }
    return 0;
}
