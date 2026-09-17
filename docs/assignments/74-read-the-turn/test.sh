#!/bin/sh
# Structural floor for a reading-for-review of jichi's own source (M627). The
# learner's READING.md must make all FIVE readings CODE_REVIEW.md names --
# abstraction->concrete, control flow, data flow, execution, and the review
# lens -- and cite where it read, as file:symbol anchors. Whether the reading
# is INSIGHTFUL is your judgment; that it is complete, anchored, and checked
# against a recorded run a script can check.
#
# Two tiers, deliberately. The subject is the real tree, so the honest check
# is "do your anchors resolve?" -- but the two-sided proof in
# tests/e2e/curriculum_graders.py runs this grader in a COPY of
# docs/assignments with no src/ beside it. So: the floor below runs anywhere
# and is what the proof rests on; the anchor-resolution gate runs only when
# ../../../src exists (a learner in the jichi checkout), where it catches an
# invented citation. Skipping a gate you cannot run is not the same as
# passing it, and the output says which happened (M625's principle, applied
# to one check instead of the whole verdict).
cd "$(dirname "$0")" || exit 1
[ -f READING.md ] || { echo "FAIL: READING.md missing -- write your reading of one turn (the five sections CODE_REVIEW.md names)"; exit 1; }
[ "$(grep -c . READING.md)" -ge 12 ] || { echo "FAIL: READING.md is basically empty"; exit 1; }

for section in "## Abstraction to concrete" "## Control flow" "## Data flow" "## Execution" "## Review"; do
    grep -q "^$section" READING.md || {
        echo "FAIL: READING.md is missing the '$section' section -- one reading per section, all five"; exit 1; }
done

grep -q "jc_agent_run_turn" READING.md || {
    echo "FAIL: the reading never names jc_agent_run_turn -- the turn's entry point is where a reading of a turn starts"; exit 1; }
grep -q "oa_build_request" READING.md && grep -q "an_build_request" READING.md || {
    echo "FAIL: the abstraction reading must name BOTH concrete build_request implementations (oa_build_request, an_build_request) -- an abstraction you have followed to one target is a guess about the others"; exit 1; }
grep -q "docs/reading/traces/" READING.md || {
    echo "FAIL: the Execution reading names no recorded run under docs/reading/traces/ -- a reading checked against nothing is an assumption"; exit 1; }

# Anchors: `path/file.c:symbol` (or `file.h:symbol`), the form the reading
# guides use. At least six distinct ones -- a reading that cites two places
# has read two places.
anchors=$(grep -oE '[A-Za-z0-9_./-]*[A-Za-z0-9_]\.[ch]:[A-Za-z_][A-Za-z0-9_]*' READING.md | sort -u)
n=$(printf '%s\n' "$anchors" | grep -c .)
[ "$n" -ge 6 ] || {
    echo "FAIL: only $n distinct file:symbol anchor(s) -- cite where you read, as src/.../file.c:function (need >= 6)"; exit 1; }

# The resolution gate: only where the tree is reachable.
root=$(cd ../../.. 2>/dev/null && pwd)
if [ -n "$root" ] && [ -d "$root/src" ]; then
    for a in $anchors; do
        f=${a%%:*}; s=${a##*:}
        case "$f" in
            */*) target="$root/$f" ;;
            *)   target=$(find "$root/src" "$root/include" -name "$f" 2>/dev/null | head -1) ;;
        esac
        [ -n "$target" ] && [ -f "$target" ] || {
            echo "FAIL: anchor $a names a file that does not exist in this tree"; exit 1; }
        grep -q "$s" "$target" || {
            echo "FAIL: anchor $a: '$s' is not in $f -- an invented citation is the reviewer's own fluent-but-wrong"; exit 1; }
    done
    echo "PASS: five readings, both providers, a recorded run, $n anchors resolved against the tree"
else
    echo "PASS: five readings, both providers, a recorded run, $n anchors (form only -- src/ not reachable from here, so resolution was not checked)"
fi
exit 0
