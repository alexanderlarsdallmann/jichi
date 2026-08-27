#!/bin/sh
# smoke: numbers the docs ADVERTISE must equal the numbers we can COUNT (M259).
#
# docs/CURRICULUM.md states how many graded tasks and trap cases exist. Those
# figures had drifted -- 68/41 claimed against 74/46 real -- because each
# milestone incremented the PREVIOUS claim rather than recounting, so an error
# entered once was inherited forever and read as freshly verified. Nobody
# audits a number that looks maintained; so count it instead.
#
# Ground truth, cross-checkable by hand:
#   graded tasks = assignment specs carrying a `verify:` line (each yields
#                  exactly two grader assertions: pristine rejected, solution
#                  accepted -- and the recorded run shows 74 of each)
#   trap cases   = literal grade("...") calls in the driver (the two-sided pairs
#                  come from the SOLUTIONS loop, which passes a variable)
# M326t EXTENDED IT TO THE RELEASE BANNER, because that is where it rotted next.
# docs/ROADMAP.md's "Where we stand" is the north star's status paragraph, and it
# had drifted 30 milestones (still "M296") while claiming 74/46 against the 77/55
# this very lint was holding correct in CURRICULUM.md -- and while, in its own
# text, declaring the numbers "measured, not incremented, now enforced by
# tests/smoke/docs_counts_lint.sh". The flagship claim was the unlinted one.
#
# WHAT IS AND IS NOT COVERED, stated rather than implied (the M305 rule):
#   checked  -- graded tasks, trap cases, scaffold packs, the banner's
#               "latest milestone" against this file's own newest entry, and
#               (M379) the bench corpus: tasks/points counted from
#               tests/bench/corpus specs vs the claims in the bench README and
#               BENCH_LOCAL_GPU -- "## The eight tasks" sat over an 11-task
#               table, the exact rot this lint was built for, in a fourth home
# M497 EXTENDED IT TO THE RETROSPECTIVE, because that is where it rotted next.
# docs/PROJECT_TIMELINE.md reports ~30 measured figures and sat at **M296** for
# fifteen days and 201 milestones while every currency check in the tree stayed
# green -- milestone_currency_lint asks whether the ROADMAP is behind the pages that
# CITE a milestone, and this page does not cite, it REPORTS. The operator noticed,
# not a test. Checks 12-13 below bound that drift.
#
#   NOT checked -- the suite sizes. Those are written as LOWER BOUNDS ("over
#               10,000 unit checks"), per the M307 decision that a figure which
#               only grows is stated as a bound. A bound does not need updating
#               to stay true, so it cannot rot the way an exact count did; and
#               counting unit checks would mean running ./run_tests (6.5s, and
#               not guaranteed built when the smoke tier runs).
. "$(dirname "$0")/_smoke.sh"

t_plan 13

root=$(cd "$(dirname "$0")/../.." && pwd)
graded=$(grep -l '^verify:' "$root"/docs/assignments/*.md | wc -l | tr -d ' ')
traps=$(grep -c 'grade("' "$root/tests/e2e/curriculum_graders.py" | tr -d ' ')

claim_tasks=$(sed -n 's/.*\*\*\([0-9][0-9]*\) graded tasks\*\*.*/\1/p' \
    "$root/docs/CURRICULUM.md" | head -1)
claim_traps=$(sed -n 's/.*\*\*\([0-9][0-9]*\) trap cases\*\*.*/\1/p' \
    "$root/docs/CURRICULUM.md" | head -1)

if [ -z "$claim_tasks" ]; then
    t_fail "CURRICULUM.md states no '**N graded tasks**' figure to check"
elif [ "$claim_tasks" = "$graded" ]; then
    t_ok "CURRICULUM.md's graded-task count matches the assignments ($graded)"
else
    t_fail "graded tasks: CURRICULUM.md says $claim_tasks, specs with verify: are $graded"
fi

if [ -z "$claim_traps" ]; then
    t_fail "CURRICULUM.md states no '**N trap cases**' figure to check"
elif [ "$claim_traps" = "$traps" ]; then
    t_ok "CURRICULUM.md's trap count matches the grader ($traps)"
else
    t_fail "trap cases: CURRICULUM.md says $claim_traps, the grader has $traps"
fi

# The scaffold-pack count, cited in prose as "all N packs".
packs=$(sed -n '/struct jc_scaffold_pack PACKS\[\]/,/^};/p' \
    "$root/src/scaffold/jc_scaffold.c" | grep -cE '^ +\{ *"')
claim_packs=$(sed -n 's/.*all \([0-9][0-9]*\) packs.*/\1/p' \
    "$root/docs/SDLC.md" | head -1)

if [ -z "$claim_packs" ]; then
    t_fail "SDLC.md states no 'all N packs' figure to check"
elif [ "$claim_packs" = "$packs" ]; then
    t_ok "the cited scaffold-pack count matches the compiled-in table ($packs)"
else
    t_fail "packs: SDLC.md says $claim_packs, the PACKS table has $packs"
fi

# --- the release banner ------------------------------------------------------
# Scraped from the ★ TODO block only (the first 120 lines), so a figure quoted
# later in a milestone entry -- where a HISTORICAL count is correct and must not
# be "fixed" -- is out of scope. The banner is the only place in this file that
# claims a CURRENT number.
banner="$root/docs/ROADMAP.md"
tmp=$(smoke_tmp)
bfile="$tmp/banner"
sect="$tmp/section"

# TWO nested extractions, and the outer one is not optional.
#
# (1) The ★ TODO section alone -- from its heading to the next level-2 heading.
#     Without this bound the paragraph anchor below matches the M326t milestone
#     entry too, which QUOTES the banner's own sentence while explaining that it
#     had rotted. A red-before-green run showed that case still failing, but for
#     the wrong reason, one prose edit away from silently scraping the wrong
#     region: the M295 lesson is to put a floor under the ground truth so a
#     changed shape fails loudly instead of leaving the lint checking something
#     else. Check 4 below is that floor.
#
# (2) Within it, the "Measured, not incremented" paragraph -- because the
#     banner's opening deliberately NARRATES the old wrong figures ("claimed 74
#     graded tasks and 46 trap cases"). That is honest prose a whole-block
#     scrape reads as a current claim, and `head -1` would prefer the historical
#     74 over the real 77. Rewording it to dodge the scrape would delete the
#     account of what went wrong in order to make a test easier.
sed -n '/^## ★ TODO/,/^## [^★]/p' "$banner" > "$sect"
sed -n '/Measured, not incremented/,/^>$/p' "$sect" > "$bfile"

b_tasks=$(sed -n 's/.*[^0-9]\([0-9][0-9]*\) graded tasks.*/\1/p' "$bfile" | head -1)
b_traps=$(sed -n 's/.*[^0-9]\([0-9][0-9]*\) trap cases.*/\1/p' "$bfile" | head -1)
b_packs=$(sed -n 's/.*\*\*\([0-9][0-9]*\)\*\* scaffold packs.*/\1/p' "$bfile" | head -1)

if [ -z "$b_tasks" ] || [ -z "$b_traps" ] || [ -z "$b_packs" ]; then
    t_fail "the ROADMAP banner states no graded/trap/pack figures to check (reworded out of reach?)"
elif [ "$b_tasks" = "$graded" ] && [ "$b_traps" = "$traps" ] && [ "$b_packs" = "$packs" ]; then
    t_ok "the ROADMAP banner's counts match the ground truth ($graded/$traps/$packs)"
else
    t_fail "ROADMAP banner: $b_tasks/$b_traps/$b_packs vs real $graded/$traps/$packs"
fi

# The banner and CURRICULUM.md must not merely each be right -- they must agree.
# They did not: the banner said 74/46 while CURRICULUM.md said 77/55, and each
# page read alone looked maintained.
if [ "$b_tasks" = "$claim_tasks" ] && [ "$b_traps" = "$claim_traps" ]; then
    t_ok "the ROADMAP banner and CURRICULUM.md state the same figures"
else
    t_fail "banner says $b_tasks/$b_traps, CURRICULUM.md says $claim_tasks/$claim_traps"
fi

# "latest milestone **MNNN**" must be this file's newest entry. The file is
# chronological, so the newest entry is the last milestone heading in it --
# which is how the drift was visible at a glance and still went unnoticed for
# 30 milestones.
# From the ★ TODO section, NOT from the narrowed paragraph above (which does not
# contain it) and NOT from the whole file (where a milestone entry could quote
# the phrase). The two figures live in different regions of the same block.
b_ms=$(sed -n 's/.*latest milestone \*\*\(M[0-9][0-9a-z]*\)\*\*.*/\1/p' "$sect" | head -1)
last_ms=$(grep -E '^#{2,3} M[0-9]' "$banner" | tail -1 | sed 's/^#* \(M[0-9a-z]*\).*/\1/')

if [ -z "$b_ms" ] || [ -z "$last_ms" ]; then
    t_fail "cannot compare milestones (banner='$b_ms', last entry='$last_ms')"
elif [ "$b_ms" = "$last_ms" ]; then
    t_ok "the banner's latest milestone is this file's newest entry ($b_ms)"
else
    t_fail "banner says latest milestone $b_ms, the file's newest entry is $last_ms"
fi

# --- 9-10 (M401): the README says all of this too, and nobody was checking.
#
# THE DEFECT THIS EXISTS FOR. Checks 4-6 pin the ROADMAP banner and CURRICULUM.md
# and were written *because* those two disagreed (74/46 vs 77/55). The README's
# own "## Roadmap" section states the same two things -- a release checklist with
# the graded/trap counts, and "latest milestone MNNN" -- and was pinned by
# nothing. It said **74 graded tasks and 46 trap cases** (the figures M262 had
# already measured wrong) and **latest milestone M229** while the ROADMAP said
# M400: 171 milestones of drift, on the front page, the one document every
# reader opens first. Fixing the two registers and leaving the shop window stale
# is the M326t lesson unlearned.
#
# Same ground truth, same extraction shape as 4-6 -- deliberately, so a future
# recount cannot satisfy one page and miss the other.
readme="$root/README.md"
rsect="$tmp/readme_roadmap"
# FLATTENED to one line before matching. The claim "77 graded tasks and 55 trap
# cases" wraps across a line break in the README's prose, and the first cut of
# this check reported "states no figures" for a page that stated them plainly --
# a lint whose verdict depends on where a sentence happens to wrap is a lint that
# will be silenced by the next reflow. The subject is the claim, not its
# typography.
sed -n '/^## Roadmap/,/^## Layout/p' "$readme" | tr '\n' ' ' > "$rsect"

r_tasks=$(sed -n 's/.*\*\*\([0-9][0-9]*\) graded tasks.*/\1/p' "$rsect" | head -1)
r_traps=$(sed -n 's/.*and \([0-9][0-9]*\)  *trap cases\*\*.*/\1/p' "$rsect" | head -1)
if [ -z "$r_tasks" ] || [ -z "$r_traps" ]; then
    t_fail "README's Roadmap section states no graded/trap figures to check (reworded out of reach?)"
elif [ "$r_tasks" = "$graded" ] && [ "$r_traps" = "$traps" ]; then
    t_ok "README's release checklist matches the ground truth ($r_tasks/$r_traps)"
else
    t_fail "README says $r_tasks/$r_traps graded/trap, real $graded/$traps -- the front page is the last place drift should live"
fi

r_ms=$(sed -n 's/.*latest milestone \(M[0-9][0-9a-z]*\).*/\1/p' "$rsect" | head -1)
if [ -z "$r_ms" ]; then
    t_fail "README's Roadmap section names no latest milestone"
elif [ "$r_ms" = "$last_ms" ]; then
    t_ok "README's latest milestone is the ROADMAP's newest entry ($r_ms)"
else
    t_fail "README says latest milestone $r_ms, the ROADMAP's newest entry is $last_ms -- bump both banners together (they move in one edit)"
fi

# --- 6b (M486): the README's milestone COUNT, not just its banner ------------
# The banner above was kept current by this very check for months while three
# sentences on the same page said "400 of them" and "healthy across 400
# milestones" -- 83 behind, on the front page, next to a number a lint was
# holding correct. A maintained neighbour is what makes a stale figure invisible:
# nobody re-reads a paragraph whose adjacent number is obviously fresh.
#
# Ground truth is the same expression every currency check here resolves to, and
# the tolerance is deliberate: the count is prose and moves when someone rewords,
# the banner moves every milestone, so demanding equality would fail the build
# between a milestone landing and its README paragraph catching up. Ten is the
# window changelog_coverage_lint already uses for the same reason.
_lastn=$(printf '%s' "$last_ms" | tr -dc '0-9')
_counts=$(grep -oE '\*\*[0-9]{3} of them\*\*|across[[:space:]]+[0-9]{3}[[:space:]]+milestones|[0-9]{3} milestones before' "$root/README.md" \
          | grep -oE '[0-9]{3}' | sort -u)
if [ -z "$_counts" ]; then
    t_fail "README states no milestone count -- reworded out of reach? (this check is now measuring nothing)"
elif [ -z "$_lastn" ]; then
    t_fail "could not read a milestone number from the ROADMAP's newest entry ($last_ms)"
else
    _stale=""
    for c in $_counts; do
        _d=$((_lastn - c)); [ "$_d" -lt 0 ] && _d=$((0 - _d))
        [ "$_d" -gt 10 ] && _stale="$_stale $c"
    done
    if [ -z "$_stale" ]; then
        t_ok "README's milestone count(s) are within 10 of the ROADMAP's newest entry ($_lastn)"
    else
        t_fail "README claims$_stale milestone(s) against the ROADMAP's $_lastn -- the front page is the last place drift should live"
    fi
fi

# --- 7-8 (M379): the bench corpus. Ground truth: one spec.md per corpus dir,
# each carrying exactly one `points:` frontmatter line.
btasks=$(ls -d "$root"/tests/bench/corpus/*/ 2>/dev/null | wc -l | tr -d ' ')
bpoints=$(grep -h '^points:' "$root"/tests/bench/corpus/*/spec.md 2>/dev/null \
    | awk '{s += $2} END {print s + 0}')

bclaim=$(sed -n 's/^\([0-9][0-9]*\) tasks, \([0-9][0-9]*\) points total.*/\1 \2/p' \
    "$root/tests/bench/README.md" | head -1)
if [ "$btasks" -lt 8 ]; then
    t_fail "counted only $btasks corpus tasks -- the corpus moved; fix the count, not the floor"
elif [ "$bclaim" = "$btasks $bpoints" ]; then
    t_ok "bench README states the counted corpus ($btasks tasks, $bpoints points)"
else
    t_fail "bench README claims '$bclaim' vs counted '$btasks $bpoints' (state 'N tasks, M points total')"
fi

gclaim=$(sed -n 's/.*, \([0-9][0-9]*\) tasks \/ \([0-9][0-9]*\) points).*/\1 \2/p' \
    "$root/docs/BENCH_LOCAL_GPU.md" | head -1)
if [ "$gclaim" = "$btasks $bpoints" ]; then
    t_ok "BENCH_LOCAL_GPU's reference-bench figures match the corpus ($gclaim)"
else
    t_fail "BENCH_LOCAL_GPU claims '$gclaim' vs counted '$btasks $bpoints'"
fi

# --- 12: the retrospective's stamped milestone is not far behind the ROADMAP ---
# A BOUND, not equality. Requiring the timeline to name the newest milestone would
# make it red on every commit and would be satisfied by bumping one number -- which
# is precisely the drift this file exists against ("each milestone incremented the
# previous claim instead of recounting"). A bound says: fall behind by a phase and
# nobody minds, fall behind by a wave and re-measure the page.
JC_TL_MAX_DRIFT=40
TL="$root/docs/PROJECT_TIMELINE.md"
# The row reads `| Milestones | **M1 - M497** (...)` with an EN DASH, which is three
# bytes -- a `.` in POSIX sed matches one byte, so match "any run of non-digits"
# instead of a single character. (First version used `.` and check 12 reported
# "reading nothing", which is the floor doing its job.)
tl_ms=$(sed -n 's/^| Milestones | \*\*M1[^0-9]*\([0-9][0-9]*\)\*\*.*/\1/p' "$TL" | head -1)
rm_ms=$(grep '^### M' "$root/docs/ROADMAP.md" | tail -1 \
    | sed -n 's/^### M\([0-9][0-9]*\).*/\1/p')
if [ -z "$tl_ms" ] || [ -z "$rm_ms" ]; then
    t_fail "cannot read the milestone claim (timeline='$tl_ms' roadmap='$rm_ms') \
-- the row shape changed and this check is now reading nothing"
elif [ $((rm_ms - tl_ms)) -le "$JC_TL_MAX_DRIFT" ]; then
    t_ok "PROJECT_TIMELINE is current within $JC_TL_MAX_DRIFT milestones (M$tl_ms vs M$rm_ms)"
else
    t_fail "PROJECT_TIMELINE reports the project at M$tl_ms; the ROADMAP's newest \
entry is M$rm_ms -- $((rm_ms - tl_ms)) milestones of drift in a page whose every \
figure claims to be measured. Re-measure it; do not bump the number."
fi

# --- 13: and the figure a reader can verify in one command -------------------
# The driver count is the cheapest of the page's ~30 figures to count here (no git,
# no suite run -- the M463 constraint: the shipped tree is not a repository). An
# OVERCLAIM fails at once, because claiming coverage that does not exist is a
# different kind of wrong from being a little behind.
tl_drv=$(sed -n 's/.*+ \([0-9][0-9]*\) POSIX-sh smoke drivers.*/\1/p' "$TL" | head -1)
real_drv=$(ls "$root"/tests/smoke/*.sh 2>/dev/null \
    | grep -v -e '/run\.sh$' -e '/_smoke\.sh$' | wc -l | tr -d '[:space:]')
if [ -z "$tl_drv" ] || [ "$real_drv" -lt 100 ]; then
    t_fail "cannot compare driver counts (claimed='$tl_drv' counted=$real_drv) -- \
fix the extraction, not the floor"
elif [ "$tl_drv" -gt "$real_drv" ]; then
    t_fail "PROJECT_TIMELINE claims $tl_drv smoke drivers; only $real_drv exist. \
An overclaimed test count is worse than a stale one."
elif [ $((real_drv - tl_drv)) -le 25 ]; then
    t_ok "PROJECT_TIMELINE's driver count is within 25 of the tree ($tl_drv of $real_drv)"
else
    t_fail "PROJECT_TIMELINE claims $tl_drv smoke drivers; $real_drv exist -- \
$((real_drv - tl_drv)) behind, so its test figures are a different era's"
fi

t_done
