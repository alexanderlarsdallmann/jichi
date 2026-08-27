#!/bin/sh
# Artifact-check floor for the design task: structure only, by design --
# quality is /check's and a human's job (the three-layer assessment model).
cd "$(dirname "$0")" || exit 1

echo "1..5"
rc=0
if [ ! -f DESIGN.md ]; then
    echo "not ok 1 - DESIGN.md is missing"
    exit 1
fi
ok=1
for sec in "Problem" "Requirements" "Design" "Alternatives considered" "Test plan"; do
    grep -q "^## $sec" DESIGN.md || { ok=0; missing="$sec"; }
done
if [ "$ok" = 1 ]; then
    echo "ok 1 - all five sections present"
else
    echo "not ok 1 - missing section '## $missing'"
    rc=1
fi
if [ "$(grep -c '^```' DESIGN.md)" -ge 2 ]; then
    echo "ok 2 - a fenced block (pseudo-code / format example / diagram)"
else
    echo "not ok 2 - no fenced block in the design"
    rc=1
fi
alts="$(awk '/^## Alternatives considered/{f=1;next} /^## /{f=0} f' DESIGN.md | grep -c '^- ')"
if [ "$alts" -ge 2 ]; then
    echo "ok 3 - at least two rejected alternatives"
else
    echo "not ok 3 - fewer than two bullets under Alternatives considered"
    rc=1
fi
tests="$(awk '/^## Test plan/{f=1;next} /^## /{f=0} f' DESIGN.md | grep -c '^- ')"
if [ "$tests" -ge 3 ]; then
    echo "ok 4 - a test plan with at least three bullets"
else
    echo "not ok 4 - fewer than three bullets under Test plan"
    rc=1
fi
if [ "$(wc -c < DESIGN.md)" -ge 1500 ]; then
    echo "ok 5 - not trivially short"
else
    echo "not ok 5 - under 1500 bytes; this is a sketch, not a design"
    rc=1
fi
exit $rc
