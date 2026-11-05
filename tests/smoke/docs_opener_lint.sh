#!/bin/sh
# smoke lint: the number of living top-level pages without an opener, or without a
# "Read further" section, never grows (M946; plan 2026-10-03, step 7.6).
#
# WHY THIS EXISTS. DOC_REVIEW.md's standard says every page opens with who it is for
# (and who should read something else), what you can do after reading it, and what
# you need first; and a technical page ends with "Read further". Measured on
# 2026-10-04 over the 171 living top-level pages: 10 have an opener a pattern can
# find, 4 a "Read further" section. Bringing 161 pages to the standard at once is
# not proposed (DOC_REVIEW: pages are brought to it as they are revised), so this
# is a CEILING: a new page without the parts makes the count rise, and a revised
# page that gains them makes it fall -- and then the ceiling here must fall with it,
# so the gain is recorded and cannot be spent by the next page.
#
# CHECKED      -- docs/*.md, except the records and indexes that are not read as
#                 pages for a reader (ROADMAP, ANECDOTES, DECISIONS, DEFERRED,
#                 DRIVE_LOG, BIBLIOGRAPHY, README, PROJECT_TIMELINE).
# AN OPENER    -- within the first 40 lines, "who this is for", "who it is for",
#                 "this page is for" or "**Audience", in any case.
# READ FURTHER -- a heading of any level whose text contains "Read further".
#   1  the pages read are the tree's (floor 171)
#   2  pages without an opener: exactly the ceiling (161)
#   3  pages without "Read further": exactly the ceiling (167)
# NOT CHECKED  -- whether an opener says the right things, or a "Read further" names
#                 good sources: a reader's job (DOC_REVIEW.md §3).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
root=$(cd "$(dirname "$0")/../.." && pwd)
JC_OPENER_FLOOR=171
JC_NO_OPENER_CEIL=161
JC_NO_READFURTHER_CEIL=167

pages=$(cd "$root/docs" && ls *.md | grep -v -x -E \
    '(ROADMAP|ANECDOTES|DECISIONS|DEFERRED|DRIVE_LOG|BIBLIOGRAPHY|README|PROJECT_TIMELINE)\.md')
n=0; no_op=0; no_rf=0
for p in $pages; do
    n=$((n + 1))
    if ! head -n 40 "$root/docs/$p" | grep -q -i -E 'who (this|it) is for|this page is for|\*\*audience'; then
        no_op=$((no_op + 1))
    fi
    if ! grep -q -i -E '^#+ .*read further' "$root/docs/$p"; then
        no_rf=$((no_rf + 1))
    fi
done

if [ "$n" -ge "$JC_OPENER_FLOOR" ]; then
    t_ok "the pages read are the tree's ($n living top-level pages, floor $JC_OPENER_FLOOR)"
else
    t_fail "only $n living top-level pages under $root/docs (floor $JC_OPENER_FLOOR)"
fi

# ceil_check WHAT COUNT CEIL
ceil_check() {
    if [ "$2" -eq "$3" ]; then
        t_ok "pages without $1: $2, at the ceiling"
    elif [ "$2" -gt "$3" ]; then
        t_fail "pages without $1: $2, above the ceiling of $3 -- a new or revised page lacks it (DOC_REVIEW.md)"
    else
        t_fail "pages without $1: $2, below the ceiling of $3 -- good: lower the ceiling in this lint to $2"
    fi
}
ceil_check "an opener" "$no_op" "$JC_NO_OPENER_CEIL"
ceil_check "a Read further section" "$no_rf" "$JC_NO_READFURTHER_CEIL"

t_done
