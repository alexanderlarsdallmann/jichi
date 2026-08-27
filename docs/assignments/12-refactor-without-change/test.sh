#!/bin/sh
# The refactor gate, both halves mechanical: behaviour unchanged (the tests)
# AND the smells actually gone (the greps).
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -o test_dur dur.c test_dur.c || exit 1
./test_dur || exit 1

rc=0
if [ "$(grep -c '86400' dur.c)" = "1" ] &&
   grep -q '#define SECONDS_PER_DAY 86400' dur.c; then
    echo "ok 9 - the literal survives only as SECONDS_PER_DAY"
else
    echo "not ok 9 - 86400 must appear exactly once, in the #define"
    rc=1
fi
if [ "$(grep -c 'h < 0 || h > 23' dur.c)" = "1" ]; then
    echo "ok 10 - the validation block exists exactly once"
else
    echo "not ok 10 - the guard block is still duplicated (or gone)"
    rc=1
fi
exit $rc
