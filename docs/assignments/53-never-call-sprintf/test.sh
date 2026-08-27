#!/bin/sh
# Passes iff BOTH: the suite is green under AddressSanitizer, AND the unbounded
# write is gone. The refactor must change HOW, not WHAT: same greeting, no
# overflow. The instrument is two-fold -- ASan proves the small buffer is not
# overrun, and a grep proves the unbounded functions are actually gone (making
# the buffer bigger while keeping sprintf is not a fix).
cd "$(dirname "$0")" || exit 1
trap 'rm -f fmttest' EXIT
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }
CC=${CC:-cc}
# snprintf is C99/POSIX (jichi probes for it, JC_HAVE_VSNPRINTF); expose it under
# strict C89 the way jichi's own Makefile does, with _POSIX_C_SOURCE.
FLAGS="-std=c89 -pedantic -D_POSIX_C_SOURCE=200112L -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

$CC $FLAGS -o fmttest test_fmt.c fmt.c 2>/dev/null || {
    echo "FAIL: does not compile (need gcc/clang with AddressSanitizer)"; exit 1; }
out=$(./fmttest 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: AddressSanitizer trapped, or an assertion failed -- still overflowing:"
    echo "$out" | head -3
    exit 1
fi
# The smell check: any unbounded string write fails the grade, whatever the
# output -- so growing the buffer while keeping sprintf is not a fix. The banned
# names are matched only where they are CALLED (a '(' follows), so a comment that
# merely mentions sprintf is not a false hit; and '\bsprintf' does not match the
# 'snprintf' you are meant to use (no word boundary before the 's').
if grep -nE '(^|[^A-Za-z0-9_])(sprintf|strcpy|strcat|gets)[[:space:]]*\(' fmt.c; then
    echo "FAIL: still using an unbounded write -- use snprintf (bounded by cap)"
    exit 1
fi
echo "PASS: bounded under AddressSanitizer, and the unbounded write is gone"
exit 0
