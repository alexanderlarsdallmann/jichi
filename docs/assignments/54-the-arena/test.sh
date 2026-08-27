#!/bin/sh
# The capstone: implement arena.c to pass the provided suite (test_arena.c, the
# spec -- do not edit it) under AddressSanitizer, and leave a one-line DESIGN.md
# naming the shape. ASan is the instrument: a too-loose bounds check or a bad
# alignment shows up when the suite writes into the memory arena_alloc hands out.
cd "$(dirname "$0")" || exit 1
trap 'rm -f arena_test' EXIT
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

$CC $SAN -o arena_test test_arena.c arena.c 2>/dev/null || {
    echo "FAIL: does not compile (need gcc/clang with AddressSanitizer)"; exit 1; }
out=$(./arena_test 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: the arena does not satisfy the suite (or ASan trapped):"
    echo "$out" | head -3
    exit 1
fi
case "$out" in
    *ok*) : ;;
    *) echo "FAIL: unexpected output (want ok): $out"; exit 1 ;;
esac

[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'bump|arena|stack|reset|lifetime' DESIGN.md || {
    echo "FAIL: DESIGN.md should name the approach (a bump/arena allocator, reset not free)"; exit 1; }
echo "PASS: the arena works under AddressSanitizer, and the design is written down"
exit 0
