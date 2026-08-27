#!/bin/sh
# Structural floor for a plan: >= 3 milestones each with a SIZE estimate, and a
# retrospective comparing estimate vs actual. Whether the estimates were good is
# not the point -- LEARNING how wrong they were is, and a retro is where that
# lives. The floor checks the estimate + the retro exist and compare.
cd "$(dirname "$0")" || exit 1
ms=$(grep -cE '^[-*] .*(([^A-Za-z0-9_])[SML]([^A-Za-z0-9_]|$)|[0-9]+ *(d|h|hr|day|days|hour|hours|week|weeks|pt|pts))' PLAN.md)
[ "$ms" -ge 3 ] || { echo "FAIL: only $ms milestones carry a size estimate -- give each milestone a rough size (S/M/L or days)"; exit 1; }
grep -qiE '^##+ *retro' PLAN.md || { echo "FAIL: no retrospective section -- add a retro after the work"; exit 1; }
grep -qiE 'estimat' PLAN.md && grep -qiE 'actual' PLAN.md || { echo "FAIL: the retro does not compare estimate vs actual -- that comparison is the whole lesson"; exit 1; }
echo "PASS: $ms sized milestones and a retro comparing estimate vs actual"
