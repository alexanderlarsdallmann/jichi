/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_snprintf_fallback.c - the C89 jc_snprintf FALLBACK, run by the unit
 * suite on every architecture the suite runs on (M1172).
 *
 * WHY THIS FILE EXISTS. The fallback formatter in src/platform/jc_snprintf.c is
 * compiled only where libc lacks vsnprintf (the #else of JC_HAVE_VSNPRINTF),
 * which is no platform in today's matrix. So no binary carries it, and the unit
 * suite never ran it. Its one runner, tests/smoke/snprintf_fallback.sh,
 * compiles it with the host's compiler -- and the smoke tier runs on real
 * machines only, not in the emulated architecture sweep.
 *
 * That is how M1079's consume_class defect lived from 2026-10-29 to 2026-11-13:
 * the helper took the caller's va_list BY VALUE and called va_arg on it. C89
 * 4.8 leaves the caller's list indeterminate after that. On x86-64 SysV (and
 * 32-bit powerpc and s390x) va_list is an array type, so the helper advanced
 * the caller's list by luck; on aarch64, riscv64, 32-bit arm, i386,
 * powerpc64le, loongarch64 and mips the helper got a copy, and the conversion
 * AFTER an unrendered one (%n, %a, an unknown letter) read the wrong argument.
 * The smoke driver's cell 15 caught it on the first non-x86-64 board (the
 * Pi 500, 2026-11-12) and on every one since; nothing caught it in the sweep.
 *
 * HOW. This file compiles the fallback branch of jc_snprintf.c INTO ITSELF,
 * under private names, with JC_HAVE_VSNPRINTF undefined -- so the unit suite,
 * and therefore every row of scripts/tier-v-arch.sh, runs the fallback on its
 * own ABI. The real jc_snprintf.h is included first (with the real names), so
 * its include guard turns the .c file's own #include into a no-op and the
 * private prototypes below are the only declarations of the private names.
 *
 * RED, AND WHERE (measured 2026-11-13 on the consume_class code). x86-64:
 * green with and without the fix (the ABI masks the defect). qemu-aarch64:
 * 2 failures (cells 1 and 2). qemu-i386: 3 failures (cells 1, 2 and 3).
 * qemu-riscv64: the unit suite CRASHED (SIGSEGV in cell 3). With the fix:
 * 0 failures on all four.
 * docs/analysis/2026-11-13-a-va-list-passed-by-value.md records the measured
 * red and green. */

#include "jc_test.h"
#include "jc_snprintf.h"

#undef JC_HAVE_VSNPRINTF
int jc_fb_snprintf(char *buf, jc_size cap, const char *fmt, ...);
int jc_fb_vsnprintf(char *buf, jc_size cap, const char *fmt, va_list ap);
#define jc_snprintf jc_fb_snprintf
#define jc_vsnprintf jc_fb_vsnprintf
#include "../src/platform/jc_snprintf.c"
#undef jc_snprintf
#undef jc_vsnprintf

/* ends_with: 1 when s ends in tail. */
static int ends_with(const char *s, const char *tail)
{
    size_t ls = strlen(s);
    size_t lt = strlen(tail);
    return ls >= lt && strcmp(s + ls - lt, tail) == 0;
}

void test_snprintf_fallback(void);
void test_snprintf_fallback(void)
{
    char out[64];
    int cnt;
    int r;

    /* (1) %n then %s: the %n operand (an int *) is consumed in the CALLER's
     * list, never written through, and the %s reads "tail" -- not the bytes
     * of the int the pointer points at (which printed "M", 77, on aarch64). */
    cnt = 77;
    r = jc_fb_snprintf(out, sizeof out, "%n%s", &cnt, "tail");
    JC_CHECK(r >= 0 && r < (int)sizeof out);
    JC_CHECK(cnt == 77);
    JC_CHECK(ends_with(out, "tail"));

    /* (2) a rendered conversion on BOTH sides of %n stays on its own slot. */
    cnt = 77;
    r = jc_fb_snprintf(out, sizeof out, "%d|%n|%d", 1, &cnt, 2);
    JC_CHECK_STR(out, "1|%n|2");
    JC_CHECK(cnt == 77);

    /* (3) the double class: %a is not rendered, so it must consume a double
     * in the caller's list, and the %s after it reads "tail". On aarch64 this
     * cell is green even on the old code: AAPCS64 keeps separate offsets for
     * the general and the floating-point registers, so consuming a double from
     * a COPY does not disturb the caller's next pointer. It is red on i386, and
     * on riscv64 the old code CRASHED here: the %s read the double's bits as a
     * pointer (SIGSEGV, measured 2026-11-13). */
    r = jc_fb_snprintf(out, sizeof out, "%a%s", 1.5, "tail");
    JC_CHECK(r >= 0 && r < (int)sizeof out);
    JC_CHECK(ends_with(out, "tail"));
}
