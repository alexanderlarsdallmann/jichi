#!/bin/sh
# Runner for the wrong-suspect task: the fixed program must print the right
# total AND the debugging record must exist with its four sections.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

echo "1..5"
cc -std=c89 -pedantic -Wall -Wextra -o csvsum main.c fields.c total.c || exit 1
out="$(./csvsum)"
if [ "$out" = "total: 16" ]; then
    echo "ok 1 - csvsum prints total: 16"
else
    echo "not ok 1 - csvsum printed '$out', want 'total: 16'"
    exit 1
fi
rc=0
i=1
for sec in Symptom "Dead ends" "Root cause" Lesson; do
    i=$((i + 1))
    if grep -q "^## $sec" NOTES.md 2>/dev/null; then
        echo "ok $i - NOTES.md has ## $sec"
    else
        echo "not ok $i - NOTES.md is missing the '## $sec' section"
        rc=1
    fi
done
exit $rc
