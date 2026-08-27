#!/bin/sh
# Two-sided by construction, and the instrument is AddressSanitizer -- the same
# tool jichi's own CI runs (make SAN=1). shout() frees its result and then hands
# it back; main reads it (use-after-free) and frees it again (double-free).
#   * pristine  -> ASan traps (nonzero exit)                    -> FAIL
#   * remove the premature free -> runs clean AND prints HELLO  -> PASS
# It "works" without ASan on a good day -- which is exactly why a plain
# compile-and-run proves nothing about a dangling pointer.
cd "$(dirname "$0")" || exit 1
trap 'rm -f shout' EXIT
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"
build() { "$1" $SAN -o shout shout.c 2>/dev/null; }
if ! build "${CC:-cc}"; then
    if command -v clang >/dev/null 2>&1 && build clang; then :; else
        echo "FAIL: does not compile, or this cc has no AddressSanitizer (need gcc/clang)"; exit 1; fi
fi

out=$(./shout 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: AddressSanitizer trapped -- the dangling pointer is still there:"
    echo "$out" | head -3
    exit 1
fi
case "$out" in
    *HELLO*) : ;;
    *) echo "FAIL: no memory error, but the output is wrong (want HELLO): $out"; exit 1 ;;
esac
echo "PASS: clean under AddressSanitizer, and the output is correct"
exit 0
