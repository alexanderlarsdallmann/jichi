#!/bin/sh
# smoke lint: CHANGELOG.md must not fall systemically behind the ROADMAP (M431).
#
# WHY THIS EXISTS, AND WHY IT WAITED. This was a DEFERRED row, declined for a stated
# reason rather than by oversight: "a changelog entry is a JUDGEMENT about what is
# user-visible, and a check demanding one per milestone would be satisfied by a line
# of noise. A file full of dutiful non-entries is worse than a gap a reader can see,
# because the gap is honest." That argument is correct and this lint does not
# contradict it. The row also named its own revisit condition -- "revisit if the file
# drifts again DESPITE the note" -- and that condition has now been met: M402 added
# the coverage note after 75 milestones of silent drift, and M431 then shipped with
# no entry anyway. So the gap is bounded here, and only bounded.
#
# WHAT IS AND IS NOT COVERED, stated rather than implied (the M305 rule):
#
#   checked      -- that the newest milestone NAMED in CHANGELOG.md is within
#                   JC_CL_MAX_DRIFT of the ROADMAP's newest entry. 75 milestones of
#                   drift (M402's measured failure) can never recur silently.
#
#   NOT checked  -- that any individual milestone has an entry. A milestone that
#                   changed nothing user-visible -- a measurement, a docs pass, an
#                   internal lint -- correctly gets no bullet, and this lint lets a
#                   run of them by. It follows that this would NOT have caught M431's
#                   own omission, which was a drift of ONE. That is a deliberate
#                   limitation, not an oversight: catching a single skipped milestone
#                   requires demanding an entry per milestone, which is the thing the
#                   DEFERRED row rejects and which this lint must not smuggle in.
#                   Judgement stays with the author; only systemic silence is a build
#                   failure.
#
#   NOT checked  -- the CONTENT of any entry. Whether a bullet is honest, or
#                   user-visible, or well written, is not mechanizable, and
#                   docs/TEST_INTEGRITY.md's counterweight applies: some defects only
#                   a reader can find.
#
# Deliberately NOT git-dependent. A tighter rule could ask git whether the newest
# milestone's commit touched src/ or include/ and demand an entry only then -- precise,
# and it would have caught M431. It is refused because the published snapshot ships a
# FRESH history (docs/plans/2026-08-public-snapshot.md), so a history-dependent gate
# behaves differently in the tree people actually acquire, and a lint that changes its
# mind between the development repo and the release is worse than a looser one.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
root=$(cd "$(dirname "$0")/../.." && pwd)
CL="$root/CHANGELOG.md"
RM="$root/docs/ROADMAP.md"

# The ceiling on acceptable drift. Small enough that M402's 75 is impossible;
# generous enough that a run of measurement- or docs-only milestones needs no bullet.
JC_CL_MAX_DRIFT=10

# --- ground truth ------------------------------------------------------------
# The ROADMAP is chronological, so its newest entry is the last milestone heading --
# the same extraction docs_counts_lint uses for the banner, deliberately, so the two
# cannot disagree about which milestone is newest. Lettered milestones (M326z) reduce
# to their number for comparison.
rm_ms=$(grep -E '^#{2,3} M[0-9]' "$RM" | tail -1 | sed 's/^#* \(M[0-9a-z]*\).*/\1/')
rm_n=$(printf '%s' "$rm_ms" | sed 's/^M//; s/[a-z]*$//')

# Every milestone the changelog names, highest wins.
#
# Tokenised with `tr -c` rather than bounded with \b, which is a GNU regex
# extension POSIX defines in neither BRE nor ERE. The old pattern was
# `\(M…\)|\bM…\b`, and on a BSD grep only the FIRST alternative survived -- so
# this check kept reporting a number while silently ignoring every unparenthesised
# citation. It degraded rather than failing, which is why no floor caught it.
# Tokenising makes the parenthesised and bare forms the same case.
cl_n=$(tr -c '0-9A-Za-z_' '\n' < "$CL" \
       | grep -E '^M[0-9]+[a-z]*$' \
       | sed 's/^M//; s/[a-z]*$//' | sort -n | tail -1)

# --- 1: the extraction floor -------------------------------------------------
# Both sides must have parsed. A changed heading shape or a reworded changelog must
# fail LOUDLY here rather than leave the comparison below checking nothing (the M393
# lesson: put a floor under the ground truth, and fix the extraction, never the floor).
if [ -n "$rm_n" ] && [ -n "$cl_n" ] && [ "$rm_n" -ge 400 ] && [ "$cl_n" -ge 300 ]; then
    t_ok "extraction floor: ROADMAP newest $rm_ms, CHANGELOG names up to M$cl_n"
else
    t_fail "extraction floor tripped (roadmap='${rm_ms:-none}'/${rm_n:-none} changelog='${cl_n:-none}') -- a heading or reference shape moved; fix the extraction, not the floor"
fi

# --- 2: the changelog does not lead the roadmap ------------------------------
# A changelog naming a milestone the ROADMAP has no entry for is the other direction
# of the same drift, and it is cheap to catch here.
if [ -n "$cl_n" ] && [ -n "$rm_n" ] && [ "$cl_n" -le "$rm_n" ]; then
    t_ok "the changelog names no milestone newer than the ROADMAP's newest entry"
else
    t_fail "CHANGELOG names M$cl_n but the ROADMAP's newest entry is $rm_ms -- one of them is wrong"
fi

# --- 3: the drift is bounded --------------------------------------------------
if [ -n "$cl_n" ] && [ -n "$rm_n" ]; then
    drift=$((rm_n - cl_n))
    if [ "$drift" -lt 0 ]; then
        # Check 2 owns this case and has already failed the build. Reporting a
        # NEGATIVE drift as "within the ceiling" would be a reassuring green over a
        # broken state -- the exact shape M431 exists to remove. Found by this
        # lint's own teeth run.
        t_fail "drift is negative ($drift): the changelog leads the ROADMAP, see check 2"
    elif [ "$drift" -le "$JC_CL_MAX_DRIFT" ]; then
        t_ok "changelog drift is $drift milestone(s), within the $JC_CL_MAX_DRIFT ceiling"
    else
        t_fail "CHANGELOG stops at M$cl_n while the ROADMAP reached $rm_ms -- $drift milestones of drift, over the $JC_CL_MAX_DRIFT ceiling. Write up the user-visible changes in the band, or (if genuinely none are user-visible) say so in the [Unreleased] coverage note and name the newest milestone there."
    fi
else
    t_fail "cannot compute drift (see the floor above)"
fi

t_done
