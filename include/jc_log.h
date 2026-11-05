/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_log.h - leveled logging.
 *
 * C89 forbids variadic *macros*, but variadic *functions* are fine, so
 * logging is a plain function taking an explicit level. Output goes to
 * stderr, gated by the level set via jc_log_set_level (default JC_LOG_WARN).
 */
#ifndef JC_LOG_H
#define JC_LOG_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

#define JC_LOG_DEBUG 0
#define JC_LOG_INFO  1
#define JC_LOG_WARN  2
#define JC_LOG_ERROR 3
#define JC_LOG_NONE  4

void jc_log_set_level(int level);
int  jc_log_get_level(void);

/* Log at `level`. A newline is appended automatically. */
void jc_logf(int level, const char *fmt, ...) JC_PRINTF(2, 3);

/* M864 (the demos drive's §11 (b)): a spawn_parallel child's id, set in the forked
 * child. While it is set, every line jc_logf writes begins with the UTC time and
 * the id -- "2026-10-02T08:12:34Z <task_run> [jichi warn] ..." -- because the lead
 * and every child share one stderr, and a line named neither its agent nor its
 * time. NULL or "" clears it (the lead's lines are unchanged). */
void jc_log_set_prefix(const char *id);

/* Secret redaction (M24, defense-in-depth). No code path logs an API key today,
 * but to keep it that way as diagnostics grow, register the resolved secrets at
 * startup and route any future content-bearing diagnostic through the redactor.
 *
 * jc_redact_apply is the pure, unit-tested core: copy `in` to `out` (capacity
 * `cap`, always NUL-terminated when cap>0), replacing every occurrence of each
 * registered secret with "***". Returns the number of substitutions. Secrets
 * shorter than JC_REDACT_MIN are ignored (too generic to safely scrub). */
#define JC_REDACT_MIN 6
int  jc_redact_apply(const char *const *secrets, int nsecrets,
                     const char *in, char *out, jc_size cap);

/* M1040 (the 2047-copy-boundary leak): on a TRUNCATED capture (cap_len ==
 * cap_total - 1), replace the captured tail of a registered secret (a strict
 * prefix of the secret) with "***". On a cut: the tail is replaced by "***";
 * *new_len_out receives the new length (position after the stars); returns 1.
 * On no cut: *new_len_out = cap_len, nothing touched; returns 0. The helper
 * hardens the redaction: it only acts when the capture is full, and the matched
 * tail must be at least JC_REDACT_MIN bytes. It ZEROes the bytes past the NUL
 * so length-based consumers like jc_http_body_ctx_overflow(bridge.raw, ...) see
 * only zeros, not the remainder of the captured secret head. */
int  jc_redact_capture_tail(char *cap, jc_size cap_len, jc_size cap_total,
                            jc_size *new_len_out);

/* Register a secret to be scrubbed by jc_redact_secrets. M926 (R05-F1): the
 * value is COPIED -- the registry keeps process-lifetime storage no matter
 * what arena or buffer the caller's pointer lives in (the @ref alias site
 * passes per-turn scratch; a stored pointer there was a UAF from the second
 * turn on). NULL / too-short / duplicate / registry-full secrets are
 * ignored (a full registry is reported by the caller's own M724 gate on the
 * env-var side). */
void jc_redact_register(const char *secret);

/* jc_redact_reset() -- test isolation only; production never calls it.
 * Frees every registered secret copy and sets g_nsecrets = 0. */
void jc_redact_reset(void);

/* Redact registered secrets from `in` into `out` (see jc_redact_apply). */
int  jc_redact_secrets(const char *in, char *out, jc_size cap);

/* M1137 (C-19): malloc a NUL-terminated redacted copy of the `len`-byte input
 * `in`, or return NULL meaning 'degrade' -- when the JC_FAULT_REDACT probe is
 * armed or the malloc fails. The caller owns the returned buffer (free it) and
 * takes its own existing degrade path on NULL. jc_redact_secrets never
 * lengthens, so len+1 always fits. In a non-FAULT build the probe compiles to
 * nothing and only the real malloc outcome decides. */
char *jc_redact_scrub(const char *in, jc_size len);

/* M1131 (C-13): redact the FULL (NUL-terminated) `in` into `out` (capacity
 * `out_cap`), always NUL-terminating `out` even on a tight cap. jc_logf's
 * secrets path uses this on a heap-formatted message that does not fit the
 * 4096-byte stack buffer: a registered secret straddling that old cut used to
 * leave its near half unmatched (redaction is an exact strncmp over the WHOLE
 * secret) and printed. Redacting the full text first means no registered
 * secret is cut before it can be matched. `in` and `out` must not be NULL
 * (jc_logf's contract). */
void jc_logf_redact_full(const char *in, char *out, jc_size out_cap);

/* Non-zero if at least one secret is registered. Lets a content sink skip the
 * redaction copy entirely when nothing needs scrubbing. */
int  jc_redact_active(void);

/* The longest registered secret's length, or 0 when none. M802b: a streaming sink
 * that redacts each chunk must hold back this many trailing bytes across a chunk
 * boundary, or a secret split across two chunks escapes per-chunk redaction. */
jc_size jc_redact_maxlen(void);

#ifdef __cplusplus
}
#endif
#endif /* JC_LOG_H */
