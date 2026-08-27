#!/bin/sh
# Meta-grader for the hollow-gate trap. Two-sided by construction: the
# learner's repaired gate must pass on their fixed code AND fail on the
# original buggy code (kept under original/) -- a gate that cannot fail is
# the disease this task exists to cure. A third check confirms the code
# fix is real, independent of the gate.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

echo "1..3"
rc=0
sh gate.sh >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "ok 1 - your gate is green on your fixed code"
else
    echo "not ok 1 - your gate fails on your own code (fix rot13.c too)"
    rc=1
fi
cp rot13.c _mine.c
cp original/rot13.c rot13.c
sh gate.sh >/dev/null 2>&1
got=$?
cp _mine.c rot13.c
rm -f _mine.c
if [ "$got" -ne 0 ]; then
    echo "ok 2 - your gate goes red on the original buggy code"
else
    echo "not ok 2 - your gate is still green on the buggy code: hollow"
    rc=1
fi
cc -std=c89 -pedantic -Wall -Wextra -o _direct rot13.c test_rot13.c 2>/dev/null \
    && ./_direct >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "ok 3 - the tests themselves pass on your code"
else
    echo "not ok 3 - the real tests still fail on rot13.c"
    rc=1
fi
rm -f _direct
exit $rc
