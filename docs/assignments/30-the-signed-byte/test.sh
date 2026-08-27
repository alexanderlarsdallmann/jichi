#!/bin/sh
# Two-sided by construction. `char`'s signedness is IMPLEMENTATION-DEFINED
# (C89 3.1.2.5): signed on x86, unsigned on most ARM. bytesum.c sums bytes
# through a signed `char`, so a byte >= 0x80 sign-extends on one platform and
# not the other -- the same source prints a different answer depending on the
# compiler's choice. This is NOT undefined behaviour (nothing traps, no
# sanitizer catches it); it is non-portability, and the instrument for it is
# to COMPILE BOTH WAYS AND DIFF.
#
# The grader forces each choice with -fsigned-char / -funsigned-char:
#   * pristine  -> the two builds disagree            -> FAIL (non-portable)
#   * a fix that reads bytes as `unsigned char`       -> both builds agree
#     AND print the correct byte-value sum (1023)     -> PASS
# ACCOUNT.md must name the implementation-defined behaviour and the fix.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

CC=${CC:-cc}
FLAGS="-std=c89 -pedantic -Wall -Wextra -Werror"
$CC $FLAGS -fsigned-char   -o bytesum_s bytesum.c 2>/dev/null || {
    echo "FAIL: does not compile clean under strict C89 -Werror"; exit 1; }
$CC $FLAGS -funsigned-char -o bytesum_u bytesum.c 2>/dev/null || {
    echo "FAIL: does not compile clean under strict C89 -Werror"; exit 1; }

s=$(./bytesum_s 2>&1)
u=$(./bytesum_u 2>&1)
if [ "$s" != "$u" ]; then
    echo "FAIL: the answer depends on char's signedness -- not portable:"
    echo "  -fsigned-char:   $s"
    echo "  -funsigned-char: $u"
    exit 1
fi
# Portable now: it must be portable to the RIGHT answer -- the byte values,
# 1+128+255+127+192+64+254+2 = 1023 -- not a signedness-independent wrong one.
case "$s" in
    *1023*) : ;;
    *) echo "FAIL: portable, but the sum is wrong (want 1023): $s"; exit 1 ;;
esac

[ -f ACCOUNT.md ] || { echo "FAIL: ACCOUNT.md missing (name the behaviour + fix)"; exit 1; }
grep -qiE 'implementation.?defined|signedness|signed' ACCOUNT.md || {
    echo "FAIL: ACCOUNT.md never names why char's signedness is the trap"; exit 1; }
grep -qiE 'unsigned char' ACCOUNT.md || {
    echo "FAIL: ACCOUNT.md must name the fix (unsigned char for raw bytes)"; exit 1; }

echo "PASS: portable across both char signednesses, sum correct, accounted for"
exit 0
