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
void jc_logf(int level, const char *fmt, ...);

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

/* Register a secret to be scrubbed by jc_redact_secrets. The pointer is stored,
 * not copied, so it must outlive the process (API keys live on the session
 * arena). NULL / too-short secrets are ignored. */
void jc_redact_register(const char *secret);

/* Redact registered secrets from `in` into `out` (see jc_redact_apply). */
int  jc_redact_secrets(const char *in, char *out, jc_size cap);

/* Non-zero if at least one secret is registered. Lets a content sink skip the
 * redaction copy entirely when nothing needs scrubbing. */
int  jc_redact_active(void);

#ifdef __cplusplus
}
#endif
#endif /* JC_LOG_H */
