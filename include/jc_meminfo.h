/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_meminfo.h - the process's own memory footprint (M180).
 *
 * The historical 12 GB memory-growth report was unactionable because nothing
 * ever recorded memory: telemetry had tokens, latency, and cost, but not one
 * byte of RSS. This module closes that hole. The parser is pure (a
 * /proc/self/status text in, VmRSS/VmHWM out) so it is unit-testable; the
 * reader is a thin Linux /proc shell (jichi is Linux/POSIX-only; on a system
 * without /proc it degrades to "unknown" and every caller simply omits the
 * field).
 *
 * Emitted on the `turn_end` telemetry event (`rss_kb`), in the `--heartbeat`
 * jsonl event, and as a line in `/context` beside the M140 arena gauge --
 * so a long-run growth curve is one jq away from any telemetry file.
 */
#ifndef JC_MEMINFO_H
#define JC_MEMINFO_H


#ifdef __cplusplus
extern "C" {
#endif
/* Parse a /proc/<pid>/status text for `VmRSS:` and `VmHWM:` (kB). Pure.
 * Either out pointer may be NULL. Missing fields leave the out value 0.
 * Returns 1 iff VmRSS was found, else 0. */
int jc_meminfo_parse(const char *status_text, long *rss_kb, long *hwm_kb);

/* Read /proc/self/status and parse it. Returns 1 on success, 0 when the
 * file is unavailable (non-Linux, restricted /proc) -- callers should then
 * omit the metric rather than report 0. */
int jc_meminfo_self(long *rss_kb, long *hwm_kb);

#ifdef __cplusplus
}
#endif
#endif /* JC_MEMINFO_H */
