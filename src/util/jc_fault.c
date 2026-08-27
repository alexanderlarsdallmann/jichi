/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fault.c - deterministic fault injection (M198 #4). See jc_fault.h.
 *
 * The whole file compiles to nothing unless JC_FAULT is defined (make FAULT=1),
 * so a release binary carries no fault-injection surface.
 */

#include "jc_fault.h"

#ifdef JC_FAULT

#include <stdlib.h>

/* Per-site "first N calls succeed" thresholds, read once from the environment.
 * -1 means the site is disabled. */
static long thresh[JC_FAULT_SITE_COUNT];
static long calls[JC_FAULT_SITE_COUNT];
static int loaded = 0;

static long env_after(const char *name)
{
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') {
        return -1;
    }
    return atol(v);
}

/* Mid-stream kill (M269): which stream transfer dies, and after how many
 * delivered body bytes. Not part of the monotone `thresh` table -- this site is
 * one-shot on purpose, because the recovery is the thing under test. */
static long mid_at = -1;
static long mid_bytes;
static long stream_no;

static void load(void)
{
    thresh[JC_FAULT_ALLOC] = env_after("JICHI_FAULT_ALLOC_AFTER");
    thresh[JC_FAULT_READ]  = env_after("JICHI_FAULT_READ_AFTER");
    thresh[JC_FAULT_WRITE] = env_after("JICHI_FAULT_WRITE_AFTER");
    thresh[JC_FAULT_NET]   = env_after("JICHI_FAULT_NET_AFTER");
    /* M503: every site MUST be listed here. `thresh` is static, so a site
     * nobody assigns keeps the zero-initialised 0 -- which this file's own
     * convention reads as "fire after 0 calls", i.e. ARMED ON EVERY CALL in any
     * FAULT=1 build with no environment set at all.
     *
     * PROCFS had been in that state since M326q and nobody noticed, because
     * `make smoke-faults` runs three drivers and none of them asserts on
     * /proc-derived behaviour. It surfaced when CHMOD was added: the unit suite
     * built with FAULT objects failed two assertions that had guarded 0600 and
     * 0700 modes for milestones, because jc_make_private had become a no-op
     * with nothing asking it to be.
     *
     * An injector that is on when nobody armed it is the same defect class as a
     * safety check that is off when everyone thinks it is on -- it silently
     * changes what the suite is measuring. */
    thresh[JC_FAULT_PROCFS] = env_after("JICHI_FAULT_PROCFS_AFTER");
    thresh[JC_FAULT_CHMOD]  = env_after("JICHI_FAULT_CHMOD_AFTER");
    mid_at = env_after("JICHI_FAULT_NET_MID_AT");
    mid_bytes = env_after("JICHI_FAULT_NET_MID_BYTES");
    if (mid_bytes < 0) {
        mid_bytes = 0; /* default: die on the first write callback */
    }
    loaded = 1;
}

long jc_fault_stream_kill_after(void)
{
    long n;
    if (!loaded) {
        load();
    }
    if (mid_at < 0) {
        return -1; /* disabled: do not even count */
    }
    n = stream_no++;
    return (n == mid_at) ? mid_bytes : -1;
}

int jc_fault_hit(enum jc_fault_site site)
{
    if (site < 0 || site >= JC_FAULT_SITE_COUNT) {
        return 0;
    }
    if (!loaded) {
        load();
    }
    if (thresh[site] < 0) {
        return 0; /* disabled: do not even count */
    }
    /* Monotone: the first `thresh` calls pass, everything after fails. */
    if (calls[site] < thresh[site]) {
        calls[site]++;
        return 0;
    }
    calls[site]++;
    return 1;
}

void jc_fault_reset(void)
{
    int i;
    for (i = 0; i < JC_FAULT_SITE_COUNT; i++) {
        calls[i] = 0;
    }
    stream_no = 0;
    loaded = 0;
}

#else /* !JC_FAULT */

/* C89 forbids an empty translation unit (-pedantic errors on it), and without
 * JC_FAULT this file has no content whatsoever. A typedef costs nothing at
 * runtime and -- unlike a static object or function -- draws no -Wunused. */
typedef int jc_fault_translation_unit_not_empty;

#endif /* JC_FAULT */
