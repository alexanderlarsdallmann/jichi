#!/bin/sh
# smoke: the C89 jc_snprintf fallback must format (not just compile) --
# M1079 (A-10, the 2026-10-17 hardening review, chapter A-input-parsing).
#
# THE DEFECT THIS EXISTS FOR. src/platform/jc_snprintf.c carries a C89
# fallback for libcs without vsnprintf (the Cygwin/Debian-5 class, M476/
# def-75). A-10 measured it CRASHING: `jc_snprintf(buf,64,"%.*s|%s",3,
# "abcdef","tail")` SEGV'd under ASan (in sink_puts) because BOTH else
# branches of jc_vsnprintf (jc_snprintf.c:150-154 and :170-173) print the
# literal specifier and consume NO va_arg -- so the later %s reads the
# int 3 as a char*. The tree formats %.*s at 9+ sites, %g at 2, and
# width/precision specs at ~6. No gate ever RAN the fallback (def-75's
# "compiled, never run"); this driver is the run.
#
# WHAT IS CHECKED. The driver compiles THIS repository's
# src/platform/jc_snprintf.c WITHOUT -DJC_HAVE_VSNPRINTF (the fallback
# path, in the project's strict-C89 dialect) next to a small C99 test
# main whose libc snprintf is the reference, and compares
# output+return value for the supported set: %s/%d/%x baseline,
# %.*s|%s (the A-10 desync case), %5d, %-10s, %02x, %.78s, %.3s (a
# precision SMALLER than the string, so the truncation branch n < prec
# runs; byte-for-byte vs libc), %04d, %ld,
# a truncation return-value case (cap 8 of "%s-%d"), and -- the alignment
# core -- %g and %07.4f: their rendered TEXT may legitimately be crude in
# a diagnostics fallback (the class is the desync, not float fidelity),
# but they must CONSUME their double, stay bounded, and keep the
# following conversions on the right slot (that is what libc agrees on
# after the fix; %g's crude form is documented in jc_snprintf.h). And the
# two M1079-c %n pins (audit finding 1): "n nowrite resync" runs
# jc_snprintf "%n%s" with the int operand pre-set and asserts the pointer
# is LEFT UNMODIFIED (no write through it) and the following %s stays
# aligned (native re-sync); "n slotwidth" is the durable Leg B, a
# hand-aligned two-slot va region whose first slot is consumed at the %n
# width (a pointer, not an int) and whose second slot must read back
# clean -- the only construct red on a width regression on this SysV host,
# where the real va_arg masks it.
#
# This driver tests the repository's platform source compiled into a
# throwaway program under $tmp. It does NOT run the pre-built tier
# binary (that would be vacuous here -- the fallback is not selected on
# this host's libc, which is exactly why it stayed broken until M1079);
# so it is named in smoke_lint check 5's exempt list (same shape as
# release_mode/static_mode; SV:1201 -- the code line that used to carry the
# $BIN name made mutant-sweep select it, and it passed hollow).
. "$(dirname "$0")/_smoke.sh"

root=$(cd "$(dirname "$0")/../.." && pwd)

# Decide BEFORE the plan (smoke_lint check 20): every skip must be able to
# fire without a TAP plan line already on stdout.
_cc=""
for _c in "${CC:-}" cc gcc clang; do
    [ -n "$_c" ] || continue
    command -v "$_c" >/dev/null 2>&1 || continue
    _cc="$_c"; break
done
[ -n "$_cc" ] || t_skip "no C compiler on PATH (tried \$CC, cc, gcc, clang)"

t_plan 16
s=$(smoke_tmp)
# A real literal tab for the t_fail pretty-print (the M471 class: `\t` in a
# sed replacement is a GNU-ism, not POSIX sh). _smoke.sh's drivers printf the
# tab into a variable this same way.
tab=$(printf '\t')

# --- the throwaway C program -------------------------------------------------
# Written into $s (the driver's own temp dir), never into the repo tree.
# The here-document is UNQUOTED on purpose: only $root is meant to
# expand; the C code below carries no other $, backtick, or backslash.
cat > "$s/fb.c" <<EOF
/* A-10: link the repository's C89 fallback (compiled WITHOUT
 * JC_HAVE_VSNPRINTF) and compare it against this C99 main's libc
 * snprintf for the supported set plus the desync case. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "jc_snprintf.h"

#define CASE(label, cap, fmt, ...) \
    do { \
        char b1[256], b2[256]; \
        int r1, r2; \
        r1 = jc_snprintf(b1, (cap), fmt, __VA_ARGS__); \
        r2 = snprintf(b2, (cap), fmt, __VA_ARGS__); \
        if (r1 == r2 && (jc_size)(r1 < 0 ? 0 : r1) == strlen(b1) \
            && strcmp(b1, b2) == 0) \
            printf("case: %s ... ok\n", label); \
        else \
            printf("case: %s ... MISMATCH (jc r=%d out=[%s]; lib r=%d out=[%s])\n", \
                   label, r1, b1, r2, b2); \
    } while (0)

/* M1079-c: the durable Leg B from 2026-10-29-M1079-b.md, ported into the
 * driver. It emulates a BYTE-OFFSET va_list (the i386 / strict-ABI class,
 * where va_arg advances by sizeof(type)) in a hand-aligned buffer:
 *   slot0 (offset 0) = an int* value  (the %n operand)
 *   slot1 (offset 8) = a char* value  (the %s that follows)
 * The helper mirrors consume_class's %n branch: it CONSUMES slot0 at a width
 * (the fixed branch consumes a pointer, sizeof(int *); a width regression
 * consumes an int, sizeof(int)) and then reads the following %s as a char*.
 * If the consume is the RIGHT width, the following read lands on the clean,
 * aligned slot1. If it is the WRONG (narrow) width, the following read lands
 * MID-SLOT (the high word of slot0 welded to the low word of slot1) -- the
 * exact desync A-10 exists to eliminate.
 *
 * WHY EMULATION (not a real jc_snprintf call): this host is x86-64 SysV,
 * where glibc's va_arg advances a FULL 8-byte register slot for int AND int*
 * alike (the M1079-b Leg A mask), so no real %n call here can go red on a
 * width regression. Emulating the byte-offset advance is the only construct
 * that exercises the width logic in isolation. width is a compile-time
 * constant here so the compiler can fold it; the RED demonstration flips
 * N_WIDTH from sizeof(int *) to sizeof(int) (the buggy read) and shows the
 * case go red, then restores it.
 *
 * Returns the following %s as read (void *), so the caller can compare it
 * against the clean slot1 pointer. */
#define N_WIDTH (sizeof(int *))  /* the FIXED consume_class %n width: int * */
static void *legb_next_after_n(const void *region, unsigned width)
{
    const unsigned char *ap = (const unsigned char *)region;
    void *next;
    ap += width;                    /* consume slot0 at 'width' (the %n operand) */
    memcpy(&next, ap, sizeof next); /* the following %s reads a char* here */
    return next;
}

int main(void)
{
    char b1[64], b2[64];
    int r1, r2;
    long lv = 5000000000L;

    CASE("baseline s d x", 64, "%s %d %x", "hello", 42, 255);
    CASE("desync .*s|%s", 64, "%.*s|%s", 3, "abcdef", "tail");
    CASE("width 5d", 64, "%5d", 42);
    CASE("left10 s", 64, "%-10s", "hello");
    CASE("02x", 64, "%02x", 255);
    CASE("prec78 s", 64, "%.78s",
         "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ");
    /* M1079-b / N2: a precision SMALLER than the string, so the truncation
     * branch (n < prec) in render_str runs. The old precision case used 78
     * (larger than 38 chars), never exercising truncation. Byte-for-byte vs
     * libc: "%.3s" of "abcdef" -> "abc". */
    CASE("prec3 s", 64, "%.3s", "abcdef");
    CASE("04d", 64, "%04d", 12);
    CASE("ld", 64, "%ld", lv);

    /* %g: the alignment core. The rendered form may be crude (documented);
     * the double must be consumed and the following %s kept aligned. */
    r1 = jc_snprintf(b1, 64, "%g|%s", 3000000000.0, "tail");
    r2 = snprintf(b2, 64, "%g|%s", 3000000000.0, "tail");
    if (r1 >= 0 && (jc_size)r1 < 64 && strcmp(b1 + strlen(b1) - 4, "tail") == 0)
        printf("case: g align ... ok (jc r=%d out=[%s]; lib r=%d out=[%s])\n",
               r1, b1, r2, b2);
    else
        printf("case: g align ... MISMATCH (jc r=%d out=[%s]; lib r=%d out=[%s])\n",
               r1, b1, r2, b2);

    /* %f with width+precision (0-flag): must consume, stay bounded, and
     * keep the following %s aligned. */
    r1 = jc_snprintf(b1, 64, "%07.4f|%s", 1.5, "tail");
    r2 = snprintf(b2, 64, "%07.4f|%s", 1.5, "tail");
    if (r1 >= 0 && (jc_size)r1 < 64 && strcmp(b1 + strlen(b1) - 4, "tail") == 0)
        printf("case: f width ... ok (jc r=%d out=[%s]; lib r=%d out=[%s])\n",
               r1, b1, r2, b2);
    else
        printf("case: f width ... MISMATCH (jc r=%d out=[%s]; lib r=%d out=[%s])\n",
               r1, b1, r2, b2);

    /* Truncation: the return value is the WOULD-be length (A-10 measured
     * the fallback already agreed here); the buffer holds cap-1 + NUL. */
    r1 = jc_snprintf(b1, 8, "%s-%d", "abcdefghij", 1234);
    r2 = snprintf(b2, 8, "%s-%d", "abcdefghij", 1234);
    if (r1 == r2 && r1 == 15 && strcmp(b1, "abcdefg") == 0)
        printf("case: trunc ... ok\n");
    else
        printf("case: trunc ... MISMATCH (jc r=%d out=[%s]; lib r=%d out=[%s])\n",
               r1, b1, r2, b2);

    /* M1079-c / audit finding 1: %n is not in the rendered set, so it cannot be
     * byte-for-byte vs libc (libc PRINTS nothing for %n but WRITES through the
     * pointer; the fallback prints the literal "%n" and must NOT write). It is
     * therefore a STANDALONE check, not a CASE. Two assertions:
     *  - the write through the int * is INTENTIONALLY ignored: cnt stays 77
     *    (pins 1a -- "no write"; the driver's %n operand is &cnt, so a stray
     *    *p = write desyncs it). This half is green on the unfixed width too
     *    (the SysV mask); its job is to catch a newly-added write.
     *  - the conversion AFTER %n re-syncs on the NATIVE ABI: b1 ends in "tail"
     *    (pins 1b, the native half). */
    {
        char nb[64];
        int cnt = 77;
        int nr = jc_snprintf(nb, 64, "%n%s", &cnt, "tail");
        if (nr >= 0 && (jc_size)nr < 64 && cnt == 77
            && strcmp(nb + strlen(nb) - 4, "tail") == 0)
            printf("case: n nowrite resync ... ok (jc r=%d out=[%s] cnt=%d)\n",
                   nr, nb, cnt);
        else
            printf("case: n nowrite resync ... MISMATCH (jc r=%d out=[%s] cnt=%d; want r in [0,63], cnt==77, ends in tail)\n",
                   nr, nb, cnt);
    }

    /* M1079-c / audit finding 1c: the slot-WIDTH pin -- the durable Leg B from
     * 2026-10-29-M1079-b.md, ported into the driver (see legb_next_after_n).
     * This host (x86-64 SysV) MASKS the width bug -- glibc's va_arg advances a
     * full 8-byte register slot for int AND int* alike -- so a REAL %n call
     * here could never go red on a width regression. The case therefore
     * EXERCISES the width logic in isolation: two 8-byte aligned slots (slot0 =
     * an int* value, slot1 = a char* value), slot0 consumed at N_WIDTH
     * (= the fixed branch's pointer read), then slot1 read back as the
     * following %s. RIGHT width -> clean; a WRONG (narrow) width -> slot1 read
     * MID-SLOT (the high word of slot0 welded to the low word of slot1). The
     * RED demonstration flips N_WIDTH to sizeof(int) (the buggy read). */
    {
        /* a hand-aligned 16-byte va region: slot0 (offset 0) is the int* value,
         * slot1 (offset 8) is the char* value. unsigned long long guarantees the
         * 8-byte alignment the report's mechanism relied on. */
        union slot { unsigned char b[8]; unsigned long long v; } reg[2];
        const char *tailp = "tail";
        void *clean, *got;
        reg[0].v = (unsigned long long)(uintptr_t)&lv;  /* slot0: an int* value */
        reg[1].v = (unsigned long long)(uintptr_t)tailp; /* slot1: a char* value */
        clean = legb_next_after_n(reg, N_WIDTH); /* consume slot0, read slot1 */
        got = clean;
        if (got == tailp)
            printf("case: n slotwidth ... ok (slot1 reads back clean: %p)\n",
                   (void *)(uintptr_t)got);
        else
                printf("case: n slotwidth ... MISMATCH (slot1 reads %p, want %p; the %%n consume advanced N_WIDTH=%zu bytes -- a width regression advances sizeof(int)=%zu and desyncs this read)\n",
                   (void *)(uintptr_t)got, (void *)(uintptr_t)tailp,
                   (size_t)N_WIDTH, (size_t)sizeof(int));
    }
    return 0;
}
EOF
[ -s "$s/fb.c" ] || t_fail "could not write the throwaway C program into $s"

# --- 1: the fallback TRANSLATION UNIT compiles, strict C89, no define -------
# -U first: a caller-provided -DJC_HAVE_VSNPRINTF must not silently select
# the C99 path and make every case below a libc-vs-libc comparison.
if "$_cc" -std=c89 -pedantic -D_POSIX_C_SOURCE=200112L -UJC_HAVE_VSNPRINTF \
       -I"$root/include" -c "$root/src/platform/jc_snprintf.c" \
       -o "$s/fb.o" > "$s/compile.log" 2>&1; then
    t_ok "the C89 fallback translation unit compiles without JC_HAVE_VSNPRINTF"
else
    t_fail "the C89 fallback translation unit does not compile:
$(cat "$s/compile.log")"
fi

# --- 2: the program links (test main C99: its snprintf is the reference) and
# RUNS without crashing (A-10: today this SEGVs; exit 139/134).
if "$_cc" -std=c99 -D_POSIX_C_SOURCE=200112L -I"$root/include" \
    "$s/fb.c" "$s/fb.o" -o "$s/fb" > "$s/link.log" 2>&1 \
    && with_deadline 30 "$s/fb" > "$s/run.out" 2>&1; then
    t_ok "the fallback program builds and runs without crashing (A-10: today it SEGVs)"
else
    t_fail "the fallback program does not build or does not run (exit $?):
$(cat "$s/link.log" 2>/dev/null; cat "$s/compile.log" 2>/dev/null; cat "$s/run.out" 2>/dev/null)"
fi

# --- one check per CASE, in program order (10 CASEs) + g align + f width -----
for _case in "baseline s d x" "desync .*s|%s" "width 5d" "left10 s" "02x" "prec78 s" "prec3 s" "04d" "ld" "trunc"; do
    if grep -q "case: $_case .* ok" "$s/run.out" 2>/dev/null; then
        t_ok "case $_case: the fallback agrees with C99 snprintf (output+return)"
    else
        t_fail "case $_case: no '... ok' line in the program output (crash, mismatch, or absent):
$(sed -n "s/^case: $_case /${tab}/p" "$s/run.out" 2>/dev/null; head -n 6 "$s/run.out" 2>/dev/null)"
    fi
done
if grep -q "case: g align .* ok" "$s/run.out" 2>/dev/null; then
    t_ok "case g align: %g consumes its double and keeps the later %s aligned"
else
    t_fail "case g align: no '... ok' line (crash, unbounded, or desynced %s):
$(sed -n "s/^case: g align /${tab}/p" "$s/run.out" 2>/dev/null; head -n 6 "$s/run.out" 2>/dev/null)"
fi
if grep -q "case: f width .* ok" "$s/run.out" 2>/dev/null; then
    t_ok "case f width: %07.4f consumes its double and keeps the later %s aligned"
else
    t_fail "case f width: no '... ok' line (crash, unbounded, or desynced %s):
$(sed -n "s/^case: f width /${tab}/p" "$s/run.out" 2>/dev/null; head -n 6 "$s/run.out" 2>/dev/null)"
fi
# M1079-c: the %n pin (1a no-write + 1b native re-sync). This is NOT a libc
# parity case (libc's %n writes and prints nothing; the fallback prints the
# literal and must not write), so it has its own ok/MISMATCH line.
if grep -q "case: n nowrite resync .* ok" "$s/run.out" 2>/dev/null; then
    t_ok "case n nowrite resync: %n does NOT write through its int* and the later %s stays aligned"
else
    t_fail "case n nowrite resync: no '... ok' line (the %n wrote through the pointer, or the later %s desynced):
$(sed -n "s/^case: n nowrite resync /${tab}/p" "$s/run.out" 2>/dev/null; head -n 6 "$s/run.out" 2>/dev/null)"
fi
# M1079-c: the %n pin (1c slot width, the durable Leg B). Emulates the
# byte-offset va region in isolation, so it goes red on a width regression
# even on this SysV host where the real va_arg masks it.
if grep -q "case: n slotwidth .* ok" "$s/run.out" 2>/dev/null; then
    t_ok "case n slotwidth: the %n consume is POINTER-width (a 4-byte int read desyncs the following slot)"
else
    t_fail "case n slotwidth: no '... ok' line (the %n consumed int-width, desyncing the following conversion):
$(sed -n "s/^case: n slotwidth /${tab}/p" "$s/run.out" 2>/dev/null; head -n 6 "$s/run.out" 2>/dev/null)"
fi

t_done
