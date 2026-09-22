#!/bin/sh
# smoke lint: every shipped assignment is named in the curriculum grader proof
# (M618, seam C3 of docs/analysis/2026-08-27-the-teaching-seams.md).
#
# tests/e2e/curriculum_graders.py proves every grader two-sided (pristine
# fixtures FAIL, the reference solution PASSES) -- but its spec list is
# HAND-ENUMERATED, so a newly added assignment that nobody wires in is a grader
# with no red-proof, invisible forever ("audit the universe", CLAUDE.md).
# Universe, stated: docs/assignments/*.md minus INDEX.md and *.solution.md.
# Floor at today's exact count; a new spec bumps it, on purpose.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
PY="$SMOKE_ROOT/tests/e2e/curriculum_graders.py"
tmp=$(smoke_tmp)

n=0
: > "$tmp/missing"
for f in "$SMOKE_ROOT"/docs/assignments/*.md; do
    b=$(basename "$f")
    case "$b" in INDEX.md|*.solution.md) continue ;; esac
    n=$((n+1))
    grep -q "$b" "$PY" || echo "$b" >> "$tmp/missing"
done

# --- 1: extraction floor ---------------------------------------------------------
# The floor and the message have to be the SAME number. They were not: the test
# read `-ge 79` while all three messages said 84, so five specs could vanish and
# this check would report a floor it was not enforcing (found at M674, while
# raising it). A message that states a different number than the check uses is
# worse than no message -- it is a green line telling you something untrue.
if [ "$n" -ge 89 ]; then
    t_ok "enumerated $n shipped specs (floor 89 -- today's exact count)"
else
    t_fail "enumerated only $n specs (floor 89) -- the glob or the layout broke"
fi

# --- 2: every spec is named in the two-sided proof --------------------------------
if [ ! -s "$tmp/missing" ]; then
    t_ok "all $n specs are named in curriculum_graders.py"
else
    t_fail "spec(s) shipped without a two-sided grader proof: $(tr '\n' ' ' < "$tmp/missing")"
fi
t_done
