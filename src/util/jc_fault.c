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
#include "jc_platform.h"   /* jc_sleep_ms, for the lease pause (M760) */

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

/* One-shot write failure (M979): which jc_write_file call dies (0-based). Same
 * reasoning as mid_at -- a recovery is the thing under test, and the monotone
 * AFTER=0 faults the recovery too. -1 = disabled, and it STAYS disabled when
 * the env is unset (M503: an injector armed on every call when nobody set it
 * is a suite-measuring bug). */
static long write_once_at = -1;
static long write_once_no;

/* M996: short-write cap for the bg spool rewrite's temp file. One-shot on
 * purpose (the sibling of write_once_at, M979): the driver observes a
 * RECOVERY -- the original spool standing untouched after a rewrite that
 * could not deliver all its bytes -- so a monotone AFTER=n (which would fault
 * every rewrite) is the wrong shape. JICHI_FAULT_BGWRITE_SHORT_AT=<cap> makes
 * the FIRST rewrite's temp write loop deliver at most `cap` bytes (0 = its
 * first write reports failure), and clears on consumption. -1 = disabled,
 * which STAYS disabled when the env is unset (M503: an injector armed on every
 * call when nobody set it is a suite-measuring bug). */
static long bgwrite_short_cap = -1;

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
    thresh[JC_FAULT_COPY]   = env_after("JICHI_FAULT_COPY_AFTER");   /* M927 */
    thresh[JC_FAULT_CHMOD]  = env_after("JICHI_FAULT_CHMOD_AFTER");
    thresh[JC_FAULT_RSS]    = env_after("JICHI_FAULT_RSS_AFTER");
    thresh[JC_FAULT_SPAWN_RESTORE_VANISH] =
        env_after("JICHI_FAULT_SPAWN_RESTORE_VANISH_AFTER");  /* M1116 (B-09) */
    thresh[JC_FAULT_REDACT] = env_after("JICHI_FAULT_REDACT_AFTER");  /* M1137 (C-19) */
    thresh[JC_FAULT_LISTDIR] = env_after("JICHI_FAULT_LISTDIR_AFTER"); /* M1140 (D-10) */
    mid_at = env_after("JICHI_FAULT_NET_MID_AT");
    mid_bytes = env_after("JICHI_FAULT_NET_MID_BYTES");
    if (mid_bytes < 0) {
        mid_bytes = 0; /* default: die on the first write callback */
    }
    write_once_at = env_after("JICHI_FAULT_WRITE_ONCE_AT");
    bgwrite_short_cap = env_after("JICHI_FAULT_BGWRITE_SHORT_AT"); /* M996 */
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

int jc_fault_write_once_at(void)
{
    long n;
    if (!loaded) {
        load();
    }
    if (write_once_at < 0) {
        return 0; /* disabled: do not even count */
    }
    n = write_once_no++;
    return (n == write_once_at) ? 1 : 0;
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

/* Sleep for the milliseconds a variable names; nothing when it is unset. */
static void pause_from(const char *var)
{
    const char *v = getenv(var);
    long ms = (v != NULL) ? atol(v) : 0;
    if (ms > 0) {
        jc_sleep_ms(ms, NULL);
    }
}

void jc_fault_lease_pause(void)
{
    pause_from("JICHI_FAULT_LEASE_PAUSE_MS");
}

void jc_fault_board_pause(void)
{
    pause_from("JICHI_FAULT_BOARD_PAUSE_MS");
}

/* M1062-e (SV:1657, card 97): the GETTER for the resolver-stall hook. Unlike
 * pause_from above it only READS the value and never sleeps -- the sleep is the
 * caller's (jc_http.c's resolver-start callback), which re-reads the same number
 * so what it sleeps is exactly what armed the hook. Returns the ms value, or -1
 * when unset: never 0-armed when nobody set it (M503, the pause_from idiom the
 * -1 means "off" convention already uses). Read once per call, not cached -- the
 * faults tier's drivers run one value. */
long jc_fault_resolve_stall_ms(void)
{
    const char *v = getenv("JICHI_FAULT_RESOLVE_STALL_MS");
    if (v == NULL || v[0] == '\0') {
        return -1;
    }
    return atol(v);
}

/* C-15 (M1133): the no-TLS probe. Unlike the counters above it only READS the
 * flag and never counts -- the whole process answers "no TLS" for as long as
 * the env is non-empty, which is the deterministic class the driver needs
 * (arm the probe, run doctor with an https config, expect the failure; unarm
 * it, expect the same config to pass). Read once per call, not cached. */
int jc_fault_no_tls(void)
{
    const char *v = getenv("JICHI_FAULT_NO_TLS");
    return (v != NULL && v[0] != '\0') ? 1 : 0;
}

/* C-16 (M1134): the no-password-entry probe. Same read-only shape as
 * jc_fault_no_tls: the whole process answers "no passwd entry" for as long as
 * the env is non-empty, which drives jc_home_dir's HOME-unset /tmp fallback
 * deterministically on a bench whose uid HAS a passwd entry. Read once per
 * call, not cached. */
int jc_fault_home_nopasswd(void)
{
    const char *v = getenv("JICHI_FAULT_HOME_NOPASSWD");
    return (v != NULL && v[0] != '\0') ? 1 : 0;
}

void jc_fault_bgrewrite_pause(void)
{
    pause_from("JICHI_FAULT_BGREWRITE_PAUSE_MS");
}

long jc_fault_bgwrite_short_at(void)
{
    long cap;
    if (!loaded) {
        load();
    }
    if (bgwrite_short_cap < 0) {
        return -1; /* disabled */
    }
    cap = bgwrite_short_cap;
    bgwrite_short_cap = -1; /* one-shot: consumed */
    return cap;
}

void jc_fault_bgwrite_pause(void)
{
    pause_from("JICHI_FAULT_BGWRITE_PAUSE_MS");
}

void jc_fault_reset(void)
{
    int i;
    for (i = 0; i < JC_FAULT_SITE_COUNT; i++) {
        calls[i] = 0;
    }
    stream_no = 0;
    write_once_no = 0;
    bgwrite_short_cap = -1;
    loaded = 0;
}

#else /* !JC_FAULT */

/* C89 forbids an empty translation unit (-pedantic errors on it), and without
 * JC_FAULT this file has no content whatsoever. A typedef costs nothing at
 * runtime and -- unlike a static object or function -- draws no -Wunused. */
typedef int jc_fault_translation_unit_not_empty;

#endif /* JC_FAULT */
