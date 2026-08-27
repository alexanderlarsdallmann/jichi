#!/bin/sh
# smoke lint: the ROADMAP must not fall behind the milestones other docs cite.
#
# THE DEFECT THIS EXISTS FOR, measured 2026-08-17 (M463). Three checks already
# guard the project's "how current are we?" claims, and all three resolve to the
# SAME expression -- `grep '^### M...' docs/ROADMAP.md | tail -1`:
#
#   docs_counts_lint.sh      the ROADMAP banner vs the ROADMAP's newest entry
#   docs_counts_lint.sh      README's banner    vs the ROADMAP's newest entry
#   changelog_coverage_lint  CHANGELOG's newest vs the ROADMAP's newest entry
#
# So the ROADMAP is the single ground truth for currency, and NOTHING checked the
# ROADMAP itself. When it stopped being written the whole system froze together
# and stayed green: 43 commits and FOUR milestones (M459-M462) had landed and were
# cited by nine other pages -- the fleet, FreeBSD, OpenBSD, a phone, the spoken
# ghost suggestion -- while every currency check reported no drift at all.
#
# The fix is a ground truth OUTSIDE the ROADMAP: the reference pages cite a
# milestone when they record what it measured, so the highest milestone they cite
# is a lower bound on what has shipped. The ROADMAP's newest entry must reach it.
#
# WHY NOT GIT (the rejected alternative, recorded because it is the obvious one):
# "commits since docs/ROADMAP.md was last touched" is a better signal and cannot
# run here. The shipped tree is NOT a repository -- M451 unpacked `git archive`
# into a plain directory and found four checks silently missing because of it --
# and this tier runs on-target, on devices where `git` is often absent. A check
# that silently skips on the machines that matter is worse than a narrower one
# that always runs.
#
# SCOPE: top-level docs/*.md only. docs/plans/ and docs/proposals/ are excluded
# DELIBERATELY: a plan may legitimately name the milestone it intends to become,
# and that is a forward reference, not drift. Verified when this was written that
# no top-level page carries a forward reference (highest cited = highest shipped),
# so the scope costs nothing today and prevents a permanent red later.
. "$(dirname "$0")/_smoke.sh"

t_plan 2

root=$SMOKE_ROOT
RM="$root/docs/ROADMAP.md"

[ -f "$RM" ] || { t_fail "docs/ROADMAP.md is missing"; t_fail "cannot check currency"; t_done; }

# --- 1: the ROADMAP's newest ENTRY (a heading, not the banner) --------------
# The file is chronological, so the last milestone heading is the newest entry.
# Sub-milestones (M431b, M326y) sort with their parent: strip the suffix for the
# numeric comparison, exactly as changelog_coverage_lint.sh does.
rm_ms=$(grep -E '^#{2,3} M[0-9]' "$RM" | tail -1 | sed 's/^#* \(M[0-9a-z]*\).*/\1/')
rm_n=$(printf '%s' "$rm_ms" | sed 's/^M//; s/[a-z]*$//')

case "$rm_n" in
    ''|*[!0-9]*)
        t_fail "cannot read the ROADMAP's newest milestone heading (got '$rm_ms') -- the heading shape changed; fix this extraction, not the floor"
        t_fail "currency unchecked"
        t_done ;;
esac

# --- 2: the highest milestone any top-level reference page cites ------------
# A floor first: if the scan finds nothing, the extraction is broken and must say
# so rather than pass over an empty set (the M390/M393 lesson -- a lint that
# checks nothing is worse than no lint, because it reports success).
# docs/ROADMAP.md is EXCLUDED from the scan: it is the file under test, and
# measuring it against its own prose would rebuild the self-reference this lint
# exists to remove. It also produces a false red immediately -- an entry that says
# "written retrospectively (M463)" cites a milestone whose heading is, correctly,
# not there yet. (Found the first time this lint ran green-then-red, 2026-08-17.)
#
# The word boundary is done by TOKENISING, not by a regex escape: this scan was
# first written with `grep -ohE '\bM[0-9]{3}\b'`, and \b is a GNU extension that
# POSIX defines in neither BRE nor ERE. On OpenBSD 7.9 it matched nothing, so the
# ground truth became the empty string and only the floor above turned that into a
# failure instead of a silent pass. `tr -c` replaces every non-word byte with a
# newline, so ^...$ IS the word boundary, portably. (posix_utils_lint check 11 now
# forbids the escape outright; see docs/ANECDOTES.md.)
cited=$(ls "$root"/docs/*.md 2>/dev/null | grep -v '/ROADMAP\.md$' \
        | xargs cat 2>/dev/null \
        | tr -c '0-9A-Za-z_' '\n' \
        | grep -E '^M[0-9][0-9][0-9]$' \
        | sed 's/^M//' | sort -n | tail -1)
nfiles=$(ls "$root"/docs/*.md 2>/dev/null | grep -cv '/ROADMAP\.md$')

if [ "$nfiles" -lt 20 ] || [ -z "$cited" ]; then
    t_fail "the citation scan found $nfiles top-level docs and highest='$cited' -- the extraction is broken, not the docs"
    t_fail "currency unchecked"
    t_done
fi
t_ok "scanned $nfiles top-level docs pages; highest milestone cited is M$cited"

# The ROADMAP may legitimately be AHEAD (an entry written before other pages
# reference it). It may not be behind.
if [ "$rm_n" -ge "$cited" ]; then
    t_ok "the ROADMAP's newest entry ($rm_ms) reaches the highest milestone the docs cite (M$cited)"
else
    t_fail "the ROADMAP's newest entry is $rm_ms but docs/ cite M$cited -- $(( cited - rm_n )) milestone(s) have shipped with no ROADMAP entry, and every other currency check trusts this file so none of them can see it"
fi

t_done
