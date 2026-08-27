/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_snprintf.h - bounded formatting for C89.
 *
 * Strict C89 has no snprintf/vsnprintf (those are C99). All formatted output
 * in jichi goes through these wrappers; we never call sprintf.
 *
 * When the build probe defines JC_HAVE_VSNPRINTF (true on glibc and any
 * modern libc), these forward to the C99 functions. Otherwise a minimal
 * fallback formatter handles the specifier subset the codebase actually
 * uses: %s %c %d %ld %u %lu %x %lx %f %% and width/precision are limited.
 *
 * Return value matches C99 snprintf: the number of characters that WOULD
 * have been written had the buffer been large enough (excluding the NUL).
 * The buffer is always NUL-terminated when cap > 0.
 *
 * Note: use %lu with (unsigned long) casts for size_t; never %zu.
 */
#ifndef JC_SNPRINTF_H
#define JC_SNPRINTF_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include <stdarg.h>

int jc_snprintf(char *buf, jc_size cap, const char *fmt, ...);
int jc_vsnprintf(char *buf, jc_size cap, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif
#endif /* JC_SNPRINTF_H */
