/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_workerpool.h - shared fork-child supervision primitives.
 *
 * Small helpers lifted from the spawn_parallel fork pool so the warm daemon can
 * reuse the same hardening (graceful SIGTERM->SIGKILL reap + a per-child
 * watchdog) rather than re-inventing it. The parallel tool and the daemon both
 * fork short-lived children that write to a pipe/socket; these primitives keep a
 * wedged or SIGTERM-trapping child from hanging the parent.
 *
 * jc_worker_over_deadline is pure and unit-tested; jc_worker_reap_grace waits.
 */
#ifndef JC_WORKERPOOL_H
#define JC_WORKERPOOL_H


#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h> /* pid_t */

/* SIGTERM->SIGKILL grace window (ms) before a child is force-killed. */
#define JC_WORKER_TERM_GRACE_MS 300

/* A child that has ALREADY been sent SIGTERM: poll waitpid(WNOHANG) for up to
 * grace_ms (in 10ms steps); if it hasn't exited, SIGKILL it and block-reap. This
 * guarantees the parent never blocks forever in waitpid on a child that ignores
 * or traps SIGTERM. No-op when pid <= 0. */
void jc_worker_reap_grace(pid_t pid, int grace_ms);

/* Pure: has a worker that started at monotonic `start_ms` exceeded a `timeout_ms`
 * budget as of `now_ms`? A timeout_ms <= 0 disables the watchdog (returns 0).
 * Unit-tested; the single deadline predicate the daemon + parallel pools share. */
int jc_worker_over_deadline(double now_ms, double start_ms, long timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* JC_WORKERPOOL_H */
