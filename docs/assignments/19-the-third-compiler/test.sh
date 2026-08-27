#!/bin/sh
# Artifact floor for the zig experiment report: structure + command-anchored
# findings + a real measurements table. The verdict's quality is judgment.
cd "$(dirname "$0")" || exit 1

echo "1..4"
rc=0
if [ ! -f REPORT.md ]; then
    echo "not ok 1 - REPORT.md is missing"
    exit 1
fi
ok=1
for sec in "Environment" "The claim, tested" "Measurements" \
           "The cross target" "What zig cc actually is"; do
    grep -q "^## $sec" REPORT.md || { ok=0; missing="$sec"; }
done
if [ "$ok" = 1 ]; then
    echo "ok 1 - all five sections present"
else
    echo "not ok 1 - missing section '## $missing'"
    rc=1
fi
if grep -q 'zig cc' REPORT.md && grep -cq 'make CC=' REPORT.md; then
    echo "ok 2 - findings are command-anchored (make CC=... present)"
else
    echo "not ok 2 - no build commands quoted in the report"
    rc=1
fi
rows=$(awk '/^## Measurements/{f=1;next} /^## /{f=0} f' REPORT.md | grep -c '^|')
if [ "$rows" -ge 4 ]; then
    echo "ok 3 - a measurements table with real rows"
else
    echo "not ok 3 - the measurements table has $rows lines (need >= 4)"
    rc=1
fi
xlen=$(awk '/^## The cross target/{f=1;next} /^## /{f=0} f' REPORT.md | wc -c)
if [ "$xlen" -ge 200 ]; then
    echo "ok 4 - the cross-target outcome is analyzed, not just attempted"
else
    echo "not ok 4 - the cross-target section is under 200 bytes"
    rc=1
fi
exit $rc
