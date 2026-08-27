#!/bin/sh
# Two-sided by construction. The pristine fold() accumulates into a signed
# int; the running hash overflows, which is UNDEFINED BEHAVIOUR (not the
# wrap-around a hash wants). It "works" at -O0 -- so a plain compile-and-run
# proves NOTHING. This grader compiles with UndefinedBehaviorSanitizer and
# runs:
#   * pristine  -> the sanitizer TRAPS (nonzero exit) -> FAIL
#   * a fix that makes the wrap-around DEFINED (unsigned arithmetic, the
#     hash's intended semantics) -> runs clean AND prints the one correct,
#     well-defined value -> PASS
# ACCOUNT.md must name the UB and the fix (why unsigned is the right tool).
cd "$(dirname "$0")" || exit 1

build() {
    "$1" -std=c89 -pedantic -Wall -Wextra -Werror \
        -fsanitize=undefined -fno-sanitize-recover=all \
        -o fold fold.c 2>/dev/null
}
if ! build "${CC:-cc}"; then
    if command -v clang >/dev/null 2>&1 && build clang; then
        :
    else
        echo "FAIL: does not compile clean under -Werror + UBSan (need gcc/clang)"
        exit 1
    fi
fi

out=$(./fold 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: UBSan trapped -- the undefined behaviour is still there:"
    echo "$out" | head -3
    exit 1
fi
# No UB now: the fold must be the one well-defined 32-bit unsigned result.
case "$out" in
    *"3942863036"*) : ;;
    *) echo "FAIL: no UB, but the hash is wrong (want 3942863036): $out"; exit 1 ;;
esac

[ -f ACCOUNT.md ] || { echo "FAIL: ACCOUNT.md missing (name the UB + the fix)"; exit 1; }
grep -qiE 'overflow|undefined' ACCOUNT.md || {
    echo "FAIL: ACCOUNT.md never names the undefined behaviour"; exit 1; }
grep -qiE 'unsigned|wrap|modul' ACCOUNT.md || {
    echo "FAIL: ACCOUNT.md must name the fix (why unsigned / defined wrap-around)"; exit 1; }

echo "PASS: no UB under the sanitizer, hash correct, the fix is accounted for"
exit 0
