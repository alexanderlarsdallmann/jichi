/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_snprintf.h - bounded formatting for C89.
 *
 * Strict C89 has no snprintf/vsnprintf (those are C99). All formatted output
 * in jichi goes through these wrappers; we never call sprintf.
 *
 * When the build probe defines JC_HAVE_VSNPRINTF (true on glibc and any
 * modern libc), these forward to the C99 functions. Otherwise the C89
 * fallback formatter (M1079, A-10) handles the specifier subset the
 * codebase actually uses: %s %c %d %u %o %x %X and the l-forms %ld %lu
 * %lx %lX, plus %f %g %e %p %%. Flags and width are parsed and honoured
 * for that set: '-' left-align, '+'/' ' sign, '0' zero-pad; width is
 * digits or '*' (an int arg). Precision is honoured for %s (truncation)
 * and %f (default 6; digits or '*'); it is parsed-but-ignored for the
 * integer forms (%d %i %u %o %x %X and their l-forms), and a present
 * precision suppresses the 0-flag only for %f -- not for the integers
 * (whose %.*d precision is read and dropped). %g and %e render in the
 * SAME fixed-precision form (no exponent, no shortest representation) --
 * the fallback is a diagnostics path, so the float fidelity is crude by
 * design. A conversion NOT in the set still CONSUMES the argument its
 * class implies and prints the specifier literally, so the va_arg list
 * can never desync (the A-10 defect: %.*s printed without consuming its
 * int, and the later %s read the 3 as a char*). %n is such a not-in-the-
 * set conversion: it consumes its int * (the pointer, 8 bytes) and
 * ignores the write. Both halves of that behaviour are now pinned by
 * tests/smoke/snprintf_fallback.sh (M1079-c): a "%n%s" case that asserts
 * the int * is left unmodified and the following conversion stays
 * aligned, and a byte-offset va-region case that asserts the %n consume
 * is pointer-width (a 4-byte int read desyncs the next slot).
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

/* JC_PRINTF on jc_snprintf (the variadic entry): format is arg 3, the first
 * variadic arg is 4. jc_vsnprintf below is intentionally NOT annotated -- it
 * takes a va_list, and format(printf,...) only checks a true `...` function;
 * the checker for the vsnprintf call is jc_snprintf. */
int jc_snprintf(char *buf, jc_size cap, const char *fmt, ...) JC_PRINTF(3, 4);
int jc_vsnprintf(char *buf, jc_size cap, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif
#endif /* JC_SNPRINTF_H */
