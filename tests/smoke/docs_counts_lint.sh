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

t_plan 19

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

# --- 14-15 (M645): the two claims the operator found stale, and neither gated --
#
# The operator read the front page and asked why it said "Green end to end at
# M486 ... 12,422 checks / 0 failures, smoke 211 drivers / 1,141 checks" at M644.
# Every currency check in the tree was green: checks 9-11 hold the README's
# MILESTONE banner and its graded/trap counts, check 13 holds PROJECT_TIMELINE's
# driver count -- and none of them looks at the README's own test figures, or at
# the two pages that describe the smoke tier in the present tense. CONTRIBUTING
# said 68 drivers and VOCABULARY said 217, against 297 real: 229 and 80 behind.
#
# WHY NOT A TREE-WIDE SWEEP. The obvious gate -- "an exact driver count must
# carry an M-stamp or a date" -- was written, measured against the tree, and
# dropped: ~190 lines state a driver count and ~185 of them are ROADMAP,
# CHANGELOG, ANECDOTES, plans/ and PLATFORMS rows, where a historical figure is
# CORRECT and must not be "fixed" (docs/DECISIONS.md house rule). A gate that
# fires 185 times to catch 5 is an audit wearing a lint's clothes. The same
# measurement killed the docs-wide quote lint (1 verbatim quote in 110 blocks).
# So the universe is NAMED, as it is for CURRICULUM.md and SDLC.md above.
#
#   checked     -- the driver count in CONTRIBUTING.md's and VOCABULARY.md's
#                  present-tense description of the smoke tier (the line that
#                  states a count AND names Python -- one line in each file,
#                  verified by extraction, not assumed), and the milestone on
#                  the README's "Green end to end at **MNNN**" stamp.
#   not checked -- the README stamp's check COUNTS. A stamp records one past
#                  run, so its numbers are right for their milestone and wrong
#                  to overwrite; what rots is the stamp being ancient, which is
#                  what check 15 bounds. Counting unit checks needs ./run_tests,
#                  which the smoke tier cannot assume is built (the note above).

# 14: the present-tense tier descriptions. EXACT equality -- unlike check 13's
# tolerance, these sentences claim what the tier IS, not what a run measured.
c14_bad=''
for c14_f in CONTRIBUTING.md docs/VOCABULARY.md; do
    c14_n=$(grep -nE '[0-9]+ drivers' "$root/$c14_f" | grep -i python \
        | sed -n 's/^\([0-9][0-9]*\):.*/\1/p' | head -1)
    c14_v=$(grep -E '[0-9]+ drivers' "$root/$c14_f" | grep -i python \
        | sed -n 's/.*[^0-9]\([0-9][0-9]*\) drivers.*/\1/p' | head -1)
    if [ -z "$c14_v" ]; then
        c14_bad="$c14_bad $c14_f(no-figure-extracted)"
    elif [ "$c14_v" != "$real_drv" ]; then
        c14_bad="$c14_bad $c14_f:$c14_n=$c14_v"
    fi
done
if [ "$real_drv" -lt 100 ]; then
    t_fail "driver count came back $real_drv -- fix the extraction, not the floor"
elif [ -z "$c14_bad" ]; then
    t_ok "the present-tense tier descriptions both state the real driver count ($real_drv)"
else
    t_fail "smoke-tier driver count is stale in:$c14_bad -- the tree has $real_drv. \
These sentences say what the tier IS; re-measure with 'make smoke' and restate."
fi

# 15: the README's green-end-to-end stamp. A stamp may age, but not by an era:
# M486 sat on the front page until M645, 158 milestones and 901 unit checks later,
# under the words "Green end to end" -- which a reader takes as a claim about the
# tree they just cloned, not about August.
JC_README_STAMP_MAX_DRIFT=40
rs_ms=$(sed -n 's/.*Green end to end at \*\*M\([0-9][0-9]*\)\*\*.*/\1/p' \
    "$root/README.md" | head -1)
if [ -z "$rs_ms" ] || [ -z "$rm_ms" ]; then
    t_fail "cannot read the README's green-end-to-end stamp (readme='$rs_ms' \
roadmap='$rm_ms') -- the sentence shape changed and this check reads nothing"
elif [ $((rm_ms - rs_ms)) -le "$JC_README_STAMP_MAX_DRIFT" ]; then
    t_ok "the README's green stamp is within $JC_README_STAMP_MAX_DRIFT milestones (M$rs_ms vs M$rm_ms)"
else
    t_fail "the README stamps 'Green end to end' at M$rs_ms; the ROADMAP's newest \
entry is M$rm_ms -- $((rm_ms - rs_ms)) milestones. Re-run the gate and restate the \
numbers with today's milestone; do not just bump the M."

fi

# 16: the corpus size in PROSE, which is where it rotted for the fifth time.
# Check 11 above holds the "N tasks / M points)" parenthetical in this same file
# and was green throughout, while two paragraphs earlier the page said "a full
# sweep of the 8-task corpus" over an 11-task corpus -- the identical rot the
# header describes finding in a fourth home, caught there by shape and missed
# here because the sentence spells it "8-task" instead. Extraction by MEANING,
# not by one phrasing: any "N-task corpus" in the page must be the counted N.
c16_bad=$(grep -oE '[0-9]+-task corpus' "$root/docs/BENCH_LOCAL_GPU.md" \
    | sed 's/-task corpus//' | sort -u | grep -v "^$btasks$")
if [ "$btasks" -lt 2 ]; then
    t_fail "bench corpus counted $btasks tasks -- fix the extraction, not the floor"
elif [ -z "$c16_bad" ]; then
    t_ok "BENCH_LOCAL_GPU's prose corpus size matches the counted corpus ($btasks tasks)"
else
    t_fail "BENCH_LOCAL_GPU calls it a $(echo $c16_bad | tr '\n' ' ')-task corpus; \
$btasks specs exist under tests/bench/corpus. Same rot, fifth home."
fi

# --- 17-18 (M646): the two figures the M646 recount found wrong in M620's table --
#
# PROJECT_TIMELINE's ~30 figures are re-counted, not incremented; checks 12-13
# bound how stale the page may get. Neither notices a figure that was COPIED
# rather than counted in a revision that said it counted everything. Both of
# these were.
#
#   checked     -- the fuzz-target count against the JC_FUZZ_TARGETS table, and
#                  whether the documentation line figure was measured over the
#                  English-only universe its own page count uses.
#   not checked -- drift in the doc line figure. Deliberately: it grew 5.3% in
#                  26 milestones, so any tolerance tight enough to catch an
#                  8,000-line universe error would fire on ordinary growth every
#                  few weeks, and a gate that cries wolf gets silenced. Check 18
#                  tests the UNIVERSE instead, which growth cannot trip.

# 17: fuzz targets. The page said 21 for 25 milestones; the table had 19, and had
# 19 at the M620 commit too -- carried forward, not counted.
ft=$(awk '/^const struct jc_fuzz_target JC_FUZZ_TARGETS\[\] = \{/,/^};/' \
    "$root/tests/fuzz/jc_fuzz_targets.c" 2>/dev/null | grep -cE '^[[:space:]]*\{ *"')
tl_ft=$(sed -n 's/.*+ \*\{0,2\}\([0-9][0-9]*\)\*\{0,2\} fuzz targets.*/\1/p' "$TL" | head -1)
if [ -z "$tl_ft" ] || [ "$ft" -lt 5 ]; then
    t_fail "cannot compare fuzz targets (claimed='$tl_ft' counted=$ft) -- fix the \
extraction, not the floor"
elif [ "$tl_ft" = "$ft" ]; then
    t_ok "PROJECT_TIMELINE's fuzz-target count matches JC_FUZZ_TARGETS ($ft)"
else
    t_fail "PROJECT_TIMELINE claims $tl_ft fuzz targets; JC_FUZZ_TARGETS has $ft. \
Count the table; do not carry the number forward."
fi

# 18: the documentation line figure must have been measured over the SAME set as
# the page count beside it -- English only. M620 measured the lines over
# docs/**.md INCLUDING docs/i18n/ and the pages over docs/**.md EXCLUDING it, then
# added the translations again as "plus ~8,000", overstating English docs by ~8,000
# lines and double-counting them in the grand total. This does not measure drift:
# it asks which of the two candidate universes the claim is nearer, which stays
# correct however much either grows.
en=$(git -C "$root" ls-files docs 2>/dev/null | grep '\.md$' | grep -v '^docs/i18n/' \
    | (cd "$root" && xargs cat 2>/dev/null) | wc -l | tr -d '[:space:]')
all=$(git -C "$root" ls-files docs 2>/dev/null | grep '\.md$' \
    | (cd "$root" && xargs cat 2>/dev/null) | wc -l | tr -d '[:space:]')
tl_dl=$(sed -n 's/.*\*\*~\([0-9][0-9]*\),\([0-9][0-9][0-9]\) lines\*\* across.*/\1\2/p' \
    "$TL" | head -1)
if [ -z "$en" ] || [ "$en" -lt 1000 ]; then
    # Not a repository (the shipped tree, M463) -- cannot enumerate; say so.
    t_skip_one "docs line universe: not a git checkout here, nothing to enumerate against"
elif [ -z "$tl_dl" ]; then
    t_fail "PROJECT_TIMELINE states no '**~N,NNN lines** across' documentation \
figure -- the sentence shape changed and this check is reading nothing"
else
    d_en=$((tl_dl - en)); [ "$d_en" -lt 0 ] && d_en=$((-d_en))
    d_all=$((tl_dl - all)); [ "$d_all" -lt 0 ] && d_all=$((-d_all))
    if [ "$d_en" -lt "$d_all" ]; then
        t_ok "PROJECT_TIMELINE's doc line figure is on the English-only universe \
($tl_dl vs $en English, $all incl. i18n)"
    else
        t_fail "PROJECT_TIMELINE claims $tl_dl documentation lines, which is nearer \
the i18n-INCLUSIVE count ($all) than the English-only count ($en) its page count \
uses. That is the M620 defect: the lines and the pages were measured over \
different sets, and the translations were then added a second time."
    fi
fi

# --- 19: SCAFFOLDING.md's pack count is the registry's -----------------------
# THE DEFECT, found at M675 while adding the 32nd pack: the page said "the full
# set is 30" and the registry held 31. It had been wrong for at least one pack
# before this milestone touched it, and nothing could have said so -- the number
# is prose, the registry is C, and no check joined them. Same shape as the
# BUILD.md gap two milestones ago: a page summarising a thing that grows, with
# nobody holding the summary to the thing.
#
# Counted from the PACKS[] table by its entry lines, not by `init --list`, so
# this needs no binary and runs in the lint tier.
_pk=$(awk '/^static const struct jc_scaffold_pack PACKS\[\] = \{/ { f = 1; next }
           f && /^\};/                                            { exit }
           f && /^    \{ "/                                       { n++ }
           END                                                    { print n + 0 }' \
      "$SMOKE_ROOT/src/scaffold/jc_scaffold.c")
_doc=$(sed -n 's/.*the full set is \([0-9][0-9]*\).*/\1/p' \
       "$SMOKE_ROOT/docs/SCAFFOLDING.md" | head -1)
if [ "${_pk:-0}" -ge 25 ] && [ "$_pk" = "${_doc:-}" ]; then
    t_ok "SCAFFOLDING.md's pack count matches the registry ($_pk)"
else
    t_fail "SCAFFOLDING.md says ${_doc:-<none found>} packs, PACKS[] has ${_pk:-0}.
The registry is the fact; the page is a summary of it. (A count under 25 means
the awk stopped matching the table -- fix that before the prose.)"
fi

t_done