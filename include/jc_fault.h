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
 *   JICHI_FAULT_LEASE_PAUSE_MS=1500 ./jichi -p hi   # hold the lease take open (M760)
 *   JICHI_FAULT_BOARD_PAUSE_MS=1500 ./jichi board add x   # and a board edit (M761)
 *   JICHI_FAULT_NO_TLS=1 ./jichi doctor      # report the libcurl as TLS-free (C-15/M1133)
 *   JICHI_FAULT_HOME_NOPASSWD=1 ./jichi doctor  # HOME-unset /tmp fallback (C-16/M1134)
 *   JICHI_FAULT_REDACT_AFTER=0  ./jichi ...     # the 6 redact sinks fail their scrub (C-19/M1137)
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

/* M867: its own C++ linkage guard. Without it this header's functions had C
 * linkage in a unit that reached it through another header's extern "C" block
 * (jc_agent.h wraps its includes) and C++ linkage where it was included
 * directly -- the C++ build then failed to link (undefined jc_run_stop_wire). */
#ifdef __cplusplus
extern "C" {
#endif

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
    JC_FAULT_COPY,       /* the parallel/workflow merge's per-file copy
                          * (src/tools/jc_tool_parallel.c apply_change). Simulates
                          * a copy that loses the file's content, so the merge
                          * must surface a conflict and preserve the worktree
                          * rather than report a merged file that is missing
                          * (M927, R04-F1). JICHI_FAULT_COPY_AFTER=n. */
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
    JC_FAULT_RSS,        /* the topology-B leg loop's CURRENT-leg RSS read
                          * (run_headless, M953). The RSS guard (invariant 2,
                          * proposal 6.8/6.11) compares a leg's resident set to
                          * four times leg 1's -- and a healthy 6-8 MB process
                          * will never honestly exceed four times its own start,
                          * so the branch is a SAFETY guarantee that a real run
                          * cannot be made to fire. This site fakes exactly that:
                          * when armed, the current read reports the real value
                          * times four, so the guard trips on the first
                          * continue-decision. Leg 1's BASELINE read stays
                          * honest (the baseline is captured before any site
                          * call in the same leg). JICHI_FAULT_RSS_AFTER=n. */
    JC_FAULT_SPAWN_RESTORE_VANISH,  /* M1116 (B-09): the posix_spawn backend's
                          * cwd RESTORE. spawn_posix (src/util/jc_spawn.c) holds
                          * a dirfd on the original directory and fchdir()s
                          * back; this site makes the test rename the original
                          * directory's PATH out of the way at the restore point,
                          * modelling 'the old directory vanished mid-call' --
                          * the failure the path-based restore (chdir by string)
                          * cannot survive. It cannot be made to fire honestly:
                          * the directory would have to disappear in the
                          * microseconds between the child's exec and the
                          * restore. JICHI_FAULT_SPAWN_RESTORE_VANISH_AFTER=n. */
    JC_FAULT_REDACT,   /* C-19 (M1137): the jc_redact_scrub helper's malloc.
                          * Stands in for a failing allocation in the six
                          * redaction sinks (jc_toolout_preserve,
                          * jc_toolout_dump_request, jc_eventlog_add_text,
                          * jc_session_save, hl_emit, relay_write_baton),
                          * making their fail-closed OOM arms deterministically
                          * testable -- they are unreachable on a healthy
                          * machine, the same class as the C-15/C-16 probes.
                          * JICHI_FAULT_REDACT_AFTER=n. */
    JC_FAULT_LISTDIR,  /* D-10 (M1140): jc_list_dir's readdir loop. Models a
                          * MID-LISTING readdir failure -- a NULL return WITH
                          * errno set (EIO), which is how a 32-bit build without
                          * 64-bit offsets ends a listing early on a 64-bit-inode
                          * filesystem. The branch this guards (errno check on the
                          * NULL return) is otherwise unreachable on a host that
                          * never fails readdir, the same class as
                          * JC_FAULT_CHMOD/JC_FAULT_PROCFS.
                          * JICHI_FAULT_LISTDIR_AFTER=n. */
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

/* One-shot write failure (M979) -- the sibling of JC_FAULT_WRITE's monotone
 * "AFTER=n" shape, with the same reasoning as M269's mid-stream kill: a
 * RECOVERY is exactly what we need to observe, and AFTER=0 faults the
 * recovery too.
 *
 *   JICHI_FAULT_WRITE_ONCE_AT=<n>  fail the n-th jc_write_file call (0-based),
 *                                  that one only. Unset is -1 = disabled --
 *                                  never 0-armed when nobody set it (M503).
 *
 * Returns 1 when THIS call should fail; the caller still records the attempt
 * in the monotone counter as usual. Counts the call as a side effect, so call
 * it exactly once per write. */
int jc_fault_write_once_at(void);

/* Non-zero when the caller should simulate a failure at `site`. Counts the call
 * as a side effect, so each invocation advances the site's counter. */
int jc_fault_hit(enum jc_fault_site site);

/* Reset all counters (tests that want a fresh sequence in-process). */
void jc_fault_reset(void);

/* M760: a PAUSE, not a failure. JICHI_FAULT_LEASE_PAUSE_MS=<ms> sleeps that long
 * between reading a workspace's lease and taking it -- the window two runs
 * started together fall into, which is a few microseconds and therefore never
 * observed without this. */
void jc_fault_lease_pause(void);

/* M761: the same for the board. JICHI_FAULT_BOARD_PAUSE_MS=<ms> sleeps between
 * reading .jichi/board.json and writing it back, the window in which two edits
 * made at once lose one of them. */
void jc_fault_board_pause(void);

/* M1062-e (SV:1657, card 97): a GETTER for the resolver-stall hook, not a
 * pause. JICHI_FAULT_RESOLVE_STALL_MS=<ms> is read here only -- the sleep
 * happens in libcurl's resolver-start callback (jc_http.c), which re-reads the
 * value so it is always the same number the hook armed. Returns the ms value
 * when set, else -1: never 0-armed when nobody set it (M503). */
long jc_fault_resolve_stall_ms(void);

/* C-15 (M1133): the NO-TLS probe. A TLS-free libcurl reports its ssl_version
 * as "No TLS", so the doctor's https-meets-no-TLS failure arm is unreachable
 * on any host whose library HAS TLS -- and that is every build bench.
 * JICHI_FAULT_NO_TLS (non-empty) makes jc_net_curl_has_tls answer 0 for the
 * whole process, so the arm is testable deterministically, the same class as
 * JICHI_FAULT_NET_AFTER. Read, not counted: the value is what it is for the
 * lifetime of the process. Outside JC_FAULT this is a constant 0 -- the
 * accessor still exists so jc_http.c compiles in both builds, and a release
 * binary cannot be spoofed into a safety failure by an environment variable. */
int jc_fault_no_tls(void);

/* C-16 (M1134): the no-password-entry probe. Every real build bench has a
 * getpwuid entry for its own uid, so jc_home_dir's HOME-unset /tmp fallback
 * (the uid-scoped directory, the pid-scoped one, the mkdtemp leg) is
 * unreachable without dropping privileges. JICHI_FAULT_HOME_NOPASSWD
 * (non-empty) makes getpwuid(getuid()) inside jc_home_dir answer NULL, so the
 * whole fallback is testable deterministically, the same class as
 * JICHI_FAULT_NO_TLS. Read, not counted: the value is what it is for the
 * lifetime of the process. Outside JC_FAULT this is a constant 0 -- the
 * accessor still exists so jc_platform_posix.c compiles in both builds, and a
 * release binary cannot be degraded by an environment variable. */
int jc_fault_home_nopasswd(void);

/* M984: a PAUSE for the bg spool rewrite race window.
 * JICHI_FAULT_BGREWRITE_PAUSE_MS=<ms> sleeps between the size-read at
 * lseek(rfd,0,SEEK_END) and the O_TRUNC rewrite, widening the window where
 * a mid-run drain could destroy bytes the child appends. Compiled to nothing
 * without JC_FAULT. */
void jc_fault_bgrewrite_pause(void);

/* M996: a SHORT WRITE for the bg spool rewrite's temp file. The drain's
 * rewrite writes the kept tail to a temp file, then renames it over the
 * spool (the pre-M996 code O_TRUNC'd the spool and ftruncate'd it unchecked,
 * so a short write destroyed the original and padded NULs to a stale size).
 * JICHI_FAULT_BGWRITE_SHORT_AT=<n> makes the temp's write loop deliver AT MOST
 * n bytes total (0 = the loop's first write reports failure), that one drain
 * only -- a RECOVERY is what the driver observes (the original spool stands
 * untouched, no temp file left behind), so the site is one-shot on purpose,
 * the sibling of jc_fault_write_once_at. Returns -1 when disabled (the write
 * loop's own condition is the honest one) and the byte cap when armed.
 * Counts the call as a side effect, so call it exactly once per rewrite. */
long jc_fault_bgwrite_short_at(void);

/* M996: a PAUSE in the bg spool rewrite's post-write window -- after the temp
 * file is fully written and BEFORE the rename-over-the-spool (a short/failed
 * write skips it). A reap-time drain rewrites and THEN unlinks the spool, so
 * without this pause there is no window in which a test can observe the spool
 * between the rewrite and the unlink; the pause widens that window to
 * JICHI_FAULT_BGWRITE_PAUSE_MS (0 when unset, M503). Compiled to nothing
 * without JC_FAULT. */
void jc_fault_bgwrite_pause(void);

#define JC_FAULT_HIT(site) jc_fault_hit(site)

#else /* !JC_FAULT -- inert, and the branch is compiled out entirely. */

#define JC_FAULT_ALLOC 0
#define JC_FAULT_READ  0
#define JC_FAULT_WRITE 0
#define JC_FAULT_NET   0
#define JC_FAULT_PROCFS 0
#define JC_FAULT_COPY  0
#define JC_FAULT_CHMOD 0
#define JC_FAULT_RSS   0
#define JC_FAULT_SPAWN_RESTORE_VANISH 0
#define JC_FAULT_LISTDIR 0
#define JC_FAULT_HIT(site) 0
#define jc_fault_stream_kill_after() (-1L)
#define jc_fault_write_once_at() 0
#define jc_fault_lease_pause() ((void)0)
#define jc_fault_board_pause() ((void)0)
#define jc_fault_resolve_stall_ms() (-1L)
#define jc_fault_no_tls() 0
#define jc_fault_home_nopasswd() 0
#define jc_fault_bgrewrite_pause() ((void)0)
#define jc_fault_bgwrite_short_at() (-1L)
#define jc_fault_bgwrite_pause() ((void)0)

#endif /* JC_FAULT */

#ifdef __cplusplus
}
#endif

#endif /* JC_FAULT_H */
