#!/bin/sh
# The detection grader: the real fix must pass the full suite (a
# symptom-patch does not), and the verdict must exist with its evidence.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -o test_wc wc_words.c test_wc.c || exit 1
./test_wc || exit 1

rc=0
ok=1
for sec in "Claim" "Evidence" "Verdict"; do
    grep -q "^## $sec" VERDICT.md 2>/dev/null || ok=0
done
if [ "$ok" = 1 ]; then
    echo "ok 6 - VERDICT.md carries claim, evidence, and verdict"
else
    echo "not ok 6 - VERDICT.md missing (or missing a section)"
    rc=1
fi
exit $rc
