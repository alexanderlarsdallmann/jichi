#!/bin/sh
# Artifact-check floor for the porting survey: structure + a classified
# findings table + a non-trivial wall argument. Whether the wall is RIGHT
# is judgment (instructor / /check + Layer 3 comparison), per the
# three-layer model.
cd "$(dirname "$0")" || exit 1

echo "1..4"
rc=0
if [ ! -f REPORT.md ]; then
    echo "not ok 1 - REPORT.md is missing"
    exit 1
fi
ok=1
for sec in "Environment" "Method" "Findings" "The wall" "What WSL gives you"; do
    grep -q "^## $sec" REPORT.md || { ok=0; missing="$sec"; }
done
if [ "$ok" = 1 ]; then
    echo "ok 1 - all five sections present"
else
    echo "not ok 1 - missing section '## $missing'"
    rc=1
fi
n=$(grep -cE '\|.*(missing-header|missing-symbol|semantic|runtime)' REPORT.md)
if [ "$n" -ge 5 ]; then
    echo "ok 2 - at least five classified findings ($n rows)"
else
    echo "not ok 2 - only $n classified table rows (need >= 5)"
    rc=1
fi
walllen=$(awk '/^## The wall/{f=1;next} /^## /{f=0} f' REPORT.md | wc -c)
if [ "$walllen" -ge 300 ]; then
    echo "ok 3 - the wall is argued, not just named"
else
    echo "not ok 3 - the wall section is under 300 bytes; argue it"
    rc=1
fi
if grep -qE 'file:line|\.c:[0-9]|\.h:[0-9]|Makefile' REPORT.md; then
    echo "ok 4 - findings are anchored to real locations"
else
    echo "not ok 4 - no file:line anchors anywhere in the report"
    rc=1
fi
exit $rc
