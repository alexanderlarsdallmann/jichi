/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fault.h - deterministic fault injection for error-path testing (M198 #4).
 *
 * WHY THIS EXISTS
 *
 * Development use is memory-rich and runs on a healthy filesystem, so the error
 * paths -- and there are many, mostly of the shape `if (... != JC_OK) continue;`
 * -- are never taken. The interesting question is not "does it crash" but "what
 * does the user SEE when it fails?", and M198 found the answer was often
 * "nothing": an allocation failure inside jc_session_list silently dropped a
 * session from the listing, with a success exit code.
 *
 * WHY COMPILE-TIME, NOT LD_PRELOAD
 *
 * A malloc interposer is glibc-specific (this project supports static musl since
 * M190), cannot distinguish jichi's own allocations from libcurl's or cJSON's --
 * so a targeted test is impossible -- and needs a separate build artifact.
 * A compiled-in counter at jichi's OWN chokepoints is portable C89, targets
 * exactly the layer under test, and is auditable in one file.
 *
 * WHY COMPILE-TIME AND NOT JUST AN ENV GATE
 *
 * So a release binary contains no fault-injection surface at all. Without
 * JC_FAULT defined every macro below expands to a constant 0 and the compiler
 * removes the branch, so the default build pays nothing and offers nothing.
 *
 * USAGE
 *
 *   make clean && make FAULT=1
 *   JICHI_FAULT_ALLOC_AFTER=50 ./jichi ls        # 50 allocations succeed, rest fail
 *   JICHI_FAULT_READ_AFTER=3   ./jichi ls        # the 4th jc_read_file onward fails
 *   JICHI_FAULT_WRITE_AFTER=0  ./jichi -p hi     # every atomic write fails
 *
 * Counters are per-process and start at zero; "AFTER=n" means the first n calls
 * succeed and every call from n+1 on fails. A negative or unset value disables
 * that site. Deliberately monotone (fail-from-now-on) rather than one-shot: a
 * one-shot failure is usually recovered by a retry and tells you less than a
 * sustained one about what the user ends up seeing.
 *
 * See docs/proposals/2026-07-robustness-edge-cases.md (#4).
 */
#ifndef JC_FAULT_H
#define JC_FAULT_H

#ifdef JC_FAULT

/* Injection sites. Keep this list short and at real chokepoints. */
enum jc_fault_site {
    JC_FAULT_ALLOC = 0,  /* arena block allocation (src/util/jc_mem.c)        */
    JC_FAULT_READ,       /* jc_read_file (src/platform/jc_platform_posix.c)   */
    JC_FAULT_WRITE,      /* jc_write_file_atomic (ditto)                      */
    JC_FAULT_NET,        /* jc_http_stream (src/net/jc_http.c) -- fails as a
                          * transport error (JC_ERR_HTTP) BEFORE any bytes
                          * move, so the M219 retry ladder (backoff schedule,
                          * per-attempt request rebuild, retry-only-if-nothing
                          * -emitted) is testable deterministically, with no
                          * flaky server. JICHI_FAULT_NET_AFTER=n. */
    JC_FAULT_PROCFS,     /* jc_have_proc_rss (src/platform/jc_platform_posix.c)
                          * -- simulate a system without /proc, which is the
                          * only way to exercise the memBudgetMb warning
                          * (M326q) without a non-Linux box or privileges to
                          * unmount /proc. The branch it guards is a SAFETY
                          * key silently doing nothing, so leaving it untested
                          * was not an option. */
    JC_FAULT_CHMOD,      /* jc_make_private (src/platform/jc_platform_posix.c)
                          * -- simulate a filesystem that ACCEPTS chmod and
                          * ignores it. Measured on MSYS2's default `noacl`
                          * mount: `chmod 0600` returns success and the mode
                          * stays 0644, so the API key file, the daemon socket
                          * and the audit log are world-readable while jichi
                          * reports nothing. Same reason JC_FAULT_PROCFS
                          * exists: the branch this guards is a SAFETY
                          * guarantee silently doing nothing, and there is no
                          * `noacl` mount on the bench to test it on.
                          * JICHI_FAULT_CHMOD_AFTER=n. */
    JC_FAULT_SITE_COUNT
};

/* Mid-stream connection death (M269) -- the failure mode M227's handle reuse
 * introduced, and the one JC_FAULT_NET cannot express: the response has already
 * begun (status + headers received, connection warm) when the transfer dies.
 * The question is whether that still classifies as transient, drops the
 * poisoned handle, and retries on a FRESH connection.
 *
 *   JICHI_FAULT_NET_MID_AT=<n>     kill the n-th stream transfer (0-based),
 *                                  that one only -- deliberately NOT the
 *                                  monotone "AFTER=n" shape above, because a
 *                                  recovery is exactly what we need to observe.
 *   JICHI_FAULT_NET_MID_BYTES=<b>  deliver up to b body bytes to the callback
 *                                  first (default 0 = die on the first write).
 *
 * Keep b below the first complete SSE event: once content is emitted the ladder
 * correctly REFUSES to retry (retry-only-if-nothing-emitted), so a larger b
 * tests a different, non-retryable contract.
 *
 * Returns -1 when this transfer should stream normally, else the byte threshold.
 * Counts stream transfers as a side effect, so call it exactly once per
 * transfer. */
long jc_fault_stream_kill_after(void);

/* Non-zero when the caller should simulate a failure at `site`. Counts the call
 * as a side effect, so each invocation advances the site's counter. */
int jc_fault_hit(enum jc_fault_site site);

/* Reset all counters (tests that want a fresh sequence in-process). */
void jc_fault_reset(void);

#define JC_FAULT_HIT(site) jc_fault_hit(site)

#else /* !JC_FAULT -- inert, and the branch is compiled out entirely. */

#define JC_FAULT_ALLOC 0
#define JC_FAULT_READ  0
#define JC_FAULT_WRITE 0
#define JC_FAULT_NET   0
#define JC_FAULT_PROCFS 0
#define JC_FAULT_CHMOD 0
#define JC_FAULT_HIT(site) 0
#define jc_fault_stream_kill_after() (-1L)

#endif /* JC_FAULT */

#endif /* JC_FAULT_H */
