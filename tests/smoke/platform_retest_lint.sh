#!/bin/sh
# smoke: a platform verdict must stay computable (M647).
#
# docs/PLATFORMS.md keeps each verified platform as its OWN stamped datum rather
# than overwriting the last one, because "it passes on a small machine" and "it
# passes on that architecture" are different claims. That only works if every row
# says WHEN it was measured: an unstamped row reads as current forever, and the
# whole matrix is built on the opposite promise.
#
# docs/PLATFORM_RETEST.md turns that into a policy -- a row is re-run when the
# tree changes under it (T1-T7 there) or when its COVERAGE DEBT, the drivers in
# the tree today minus the drivers the row ran, gets large enough that citing it
# in the present tense stops being honest. Debt is arithmetic on two numbers, and
# this driver checks that both remain readable.
#
# WHAT IS AND IS NOT COVERED, stated rather than implied (the M305 rule):
#   checked     -- every row of the Verified table CITES at least one milestone,
#                  ISO date, or "every milestone" SOMEWHERE in the row; no row
#                  claims more smoke drivers than the tree has ever had;
#                  PLATFORM_RETEST.md's own "drivers in the tree" figure is
#                  neither an overclaim nor an era behind; and the page is
#                  reachable from the docs index.
#
#   NOT checked, and stated because the first version of this header overclaimed
#   it: check 2 does NOT verify that the stamp belongs to the MEASUREMENT. A row
#   citing M465 for its verdict and M466 for a footnote satisfies it either way,
#   and stripping only the verdict's stamp leaves it green -- proved by tooth,
#   not assumed. What it guarantees is that the row is DATABLE at all, which is
#   the precondition coverage debt needs. Tying a stamp to a specific column
#   would mean imposing a column layout on a table whose Evidence cells are
#   deliberately free prose.
#
#   Check 3's universe is the TABLE ROWS, not the page. The per-platform prose
#   sections below the table quote driver counts from historical runs, where a
#   figure larger than today's tree would be a transcription error rather than an
#   overclaim about the present -- and a perturbation there correctly does NOT
#   fire this check. That was verified by perturbing both inside and outside.
#   not checked -- whether a row's verdict is still TRUE. Nothing here re-runs
#                  anything; a lint cannot boot a NetBSD guest. This driver keeps
#                  the staleness *visible*, which is the precondition for the
#                  policy, not the policy.
#   not checked -- the trigger classes T1-T5/T7. Detecting them means classifying
#                  every diff, and a classifier wrong in the quiet direction is
#                  worse than a rule a human has read. PLATFORM_RETEST.md says so
#                  in its own words rather than pretending otherwise.
#
# Floored at the exact row count on the day it was written (20), per the house
# rule: a lint whose extraction silently returns nothing is green and worthless.
. "$(dirname "$0")/_smoke.sh"

t_plan 5

root=$(cd "$(dirname "$0")/../.." && pwd)
PL="$root/docs/PLATFORMS.md"
PR="$root/docs/PLATFORM_RETEST.md"
tmp=$(smoke_tmp)
rows="$tmp/rows"

# The platform table ONLY, anchored on its own header and bounded by the blank
# line after it. The looser "everything under ### Verified" bound was written
# first and was wrong: that section also holds the alternate-build-front-end
# table (gcc/clang/zig cc/g++), whose rows are toolchains and carry no milestone
# by design, so check 2 reported seven "unstamped platform rows" that are not
# platform rows. Extraction by anchor, not by section.
awk '/^\| Platform \| Arch \/ libc \| Evidence \|/{v=1;next} v&&/^$/{exit} v' "$PL" \
    | grep '^| ' | grep -v '^|---' > "$rows"

n=$(wc -l < "$rows" | tr -d '[:space:]')
JC_PLAT_MIN_ROWS=18
if [ "$n" -ge "$JC_PLAT_MIN_ROWS" ]; then
    t_ok "the Verified table yields $n rows (floor $JC_PLAT_MIN_ROWS)"
else
    t_fail "the Verified table yielded $n rows, under the floor of \
$JC_PLAT_MIN_ROWS -- the table shape changed and the checks below are reading \
almost nothing. Fix the extraction, not the floor."
fi

# 2: every row is stamped.
unstamped=$(grep -vE 'M[0-9]{3}|[0-9]{4}-[0-9]{2}-[0-9]{2}|every milestone' "$rows" \
    | sed 's/|[^|]*$//' | cut -c1-48 | tr '\n' ';')
if [ -z "$unstamped" ]; then
    t_ok "every Verified row carries a milestone or a date ($n rows)"
else
    t_fail "Verified row(s) citing no milestone or date at all: $unstamped -- an \
undatable row reads as current forever, which is the one thing this matrix \
promises not to do, and its coverage debt cannot be computed"
fi

# 3: no row claims more drivers than the tree has. An overclaimed test count is a
# different kind of wrong from a stale one, so it fails at once.
real_drv=$(ls "$root"/tests/smoke/*.sh 2>/dev/null \
    | grep -v -e '/run\.sh$' -e '/_smoke\.sh$' | wc -l | tr -d '[:space:]')
over=$(grep -oE '[0-9]+ (of [0-9]+ )?drivers' "$rows" \
    | grep -oE '^[0-9]+' | sort -rn | head -1)
if [ "$real_drv" -lt 100 ] || [ -z "$over" ]; then
    t_fail "cannot compare driver claims (max claimed='$over' counted=$real_drv) \
-- fix the extraction, not the floor"
elif [ "$over" -le "$real_drv" ]; then
    t_ok "no Verified row claims more smoke drivers than exist (max $over of $real_drv)"
else
    t_fail "a Verified row claims $over smoke drivers; only $real_drv exist"
fi

# 4: PLATFORM_RETEST's own tree-driver figure, which its whole debt table is
# arithmetic on. A BOUND, not equality: requiring it to equal today would make
# every new driver a failing build, which is how a gate gets silenced.
JC_PLAT_MAX_DRIFT=25
pr_drv=$(sed -n 's/.*with \*\*\([0-9][0-9]*\) drivers\*\* in the tree.*/\1/p' "$PR" | head -1)
if [ -z "$pr_drv" ]; then
    t_fail "PLATFORM_RETEST.md states no '**N** drivers in the tree' figure -- \
the sentence shape changed and this check is reading nothing"
elif [ "$pr_drv" -gt "$real_drv" ]; then
    t_fail "PLATFORM_RETEST.md computes debt against $pr_drv drivers; only \
$real_drv exist, so every debt figure on that page is overstated"
elif [ $((real_drv - pr_drv)) -le "$JC_PLAT_MAX_DRIFT" ]; then
    t_ok "PLATFORM_RETEST's debt baseline is within $JC_PLAT_MAX_DRIFT of the tree ($pr_drv of $real_drv)"
else
    t_fail "PLATFORM_RETEST computes debt against $pr_drv drivers; $real_drv \
exist -- every debt on that page is $((real_drv - pr_drv)) too low. Re-run the \
count and restate the table."
fi

# 5: reachable. A policy nobody is routed to is a policy nobody applies (M510).
if grep -q 'PLATFORM_RETEST.md' "$root/docs/README.md"; then
    t_ok "PLATFORM_RETEST.md is listed in the docs index"
else
    t_fail "PLATFORM_RETEST.md is not linked from docs/README.md"
fi

t_done
