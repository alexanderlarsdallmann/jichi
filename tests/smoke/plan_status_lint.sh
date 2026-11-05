#!/bin/sh
# smoke lint: a plan page must not say "not built" after its work shipped (M903, def-319).
#
# THE DEFECT THIS EXISTS FOR, measured 2026-10-05 (JICHI-PLANS-GAP-ANALYSIS) and
# re-measured 2026-10-07 against the then-current head: four plan pages carried
# a "planned, not built" status line whose subject the ROADMAP records as done:
#
#   docs/plans/M92-budget-outcome-clarity.md   "planned (committed before
#       implementation)" -- M92 shipped 2026-07-14, thirteen days BEFORE the
#       page was last touched (the M170 rename sweep), so the touch was
#       mechanical, not a status review. Exactly the case a header check that
#       survives reformatting exists for.
#   docs/plans/M93-superseded-read-elision.md  -- M93 done (ROADMAP).
#   docs/plans/M94-M95-followups.md            -- M94 done (eager elision),
#       M95 done (mentor prompt dedup); the page's "one candidate deliberately
#       not built" clause keeps its M92-stretch section honest.
#   docs/plans/2026-09-files-and-structures.md -- "planned, not built" while
#       the ROADMAP records the course complete ("76-80 shipped as the plan
#       named them").
#
# Seven pages total claim not-built while the ROADMAP says their work shipped;
# the three the audit leaves white are precisely the ones whose built-ness has
# NO mechanical signal: 2026-08-daemon-fleet-worker (the fleet itself was
# withdrawn as a concept), 2026-08-tui-fence-grant (M388 shipped the *design*
# the page names, but the page's own scope -- the TUI affordance -- has no
# done milestone), and 2026-08-model-facing-orchestration (Tier 0 landed as
# M431; Tiers 1-3 are deliberately deferred, and the ROADMAP line that says
# "shipped" refers to the v0.9.0 release, not this page). So the lint keys on
# the two signals that ARE mechanical and whose union is exactly the four:
#
#   Rule A -- the FILENAME is milestone-named (M###-*.md), the header claims
#            planned/not-built, no supersession marker, and the ROADMAP has a
#            heading line "M### ... done" for that number. A rename or
#            reformat of the prose does not hide it: the filename IS the claim.
#   Rule B -- the ROADMAP carries a completion word (complete|shipped|built|
#            landed) within 20 characters AFTER the page's dedated basename,
#            EXCEPT on a "<name> experiment" line. The window is what keeps
#            tui-fence-grant and model-facing-orchestration white (their
#            nearby ROADMAP prose says "design stage" / names the v0.9.0
#            release, never a completion word within 20c of the filename). The
#            experiment exclusion is what keeps daemon-fleet-worker white: the
#            audit frames the fleet as "withdrawn as a concept" -- an
#            EXPERIMENT -- and the ROADMAP line "Finding 2 of the
#            daemon-fleet-worker experiment, landed ..." says a FINDING landed,
#            not that the named deliverable was built. "<name> experiment" is
#            an investigation, and a completion word about its findings is not
#            a completion word about the plan.
#
# Both rules run over the status line only (the first 12 lines, case-insensitive
# match on the word "status") so the check survives the reformatting that the
# M170 sweep did to the M92 page. A page that has reviewed its status carries a
# supersession marker ("Status updated" / "shipped at" / "built at" /
# "superseded by" / "now done") and Rule A exempts it.
#
# GREP AND SED ONLY (the tier's premise; a scripting language here would be
# refused by smoke_lint).
. "$(dirname "$0")/_smoke.sh"

t_plan 4

G=/usr/bin/grep
[ -x "$G" ] || G=grep

plans=$(ls "$SMOKE_ROOT/docs/plans/"*.md "$SMOKE_ROOT/docs/proposals/"*.md 2>/dev/null)
n=$(printf '%s\n' "$plans" | $G -c '\.md$')
# Floor: the corpus is 60+ pages. A count under 40 means the glob broke.
if [ "$n" -lt 40 ]; then
    t_fail "only $n plan/proposal pages found -- the glob or layout broke"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_done
fi
t_ok "scanned $n plan/proposal pages"

# --- 1 (Rule A): milestone-named file, not-built header, no supersession, done in ROADMAP
redA=""
for f in $plans; do
    base=$(basename "$f" .md)
    case "$base" in M[0-9]*) ;; *) continue ;; esac
    st=$(sed -n '1,12p' "$f" | $G -i "status" | head -1)
    [ -z "$st" ] && continue
    printf '%s\n' "$st" | $G -qiE 'planned|not built|designed, not' || continue
    # A page whose status has been reviewed carries a supersession marker and
    # is exempt: "shipped as M###" / "is superseded" / "built at" / "Status
    # updated" / "superseded by" / "now done". (The marker check runs over the
    # whole page, so a fix that says `the "planned" header is superseded`
    # exempts itself -- and the four fixed pages each say exactly that.)
    $G -qiE 'status updated|shipped as|shipped at|built at|superseded|now done' "$f" && continue
    m=$(printf '%s' "$base" | tr -c '0-9A-Za-z_' '\n' | $G -E '^M[0-9]+$' | head -1)
    [ -z "$m" ] && continue
    $G -qE "^### $m .*(done|landed)" "$SMOKE_ROOT/docs/ROADMAP.md" || continue
    redA="$redA $base"
done
if [ -z "$redA" ]; then
    t_ok "no milestone-named plan page claims not-built after its milestone is done"
else
    t_fail "Rule A -- planned/not-built header whose milestone the ROADMAP records done:$redA"
fi

# --- 2 (Rule B): the ROADMAP says the named deliverable is complete, but the
# page's status still claims not-built.
redB=""
for f in $plans; do
    base=$(basename "$f" .md)
    st=$(sed -n '1,12p' "$f" | $G -i "status" | head -1)
    [ -z "$st" ] && continue
    printf '%s\n' "$st" | $G -qiE 'planned|not built|designed, not' || continue
    $G -qiE 'status updated|shipped as|shipped at|built at|superseded|now done' "$f" && continue
    # Dedate: strip a leading YYYY-MM- or YYYY-MM-DD- so the ROADMAP citation
    # ("the files-and-structures course is complete") matches the page name
    # without its write-date.
    ded=$(printf '%s' "$base" | sed -E 's/^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]-//; s/^[0-9][0-9][0-9][0-9]-[0-9][0-9]-//')
    [ -z "$ded" ] && continue
    # A completion word on the SAME line as the (dedated) page name, excluding
    # an "<name> experiment" line (a finding from an investigation, not the
    # plan). (The earlier 20-char window was a grep -oE interval expression;
    # same-line is the same rot, narrower, and interval-free for the
    # illumos-awk lint.)
    hit=$($G -F -- "$ded" "$SMOKE_ROOT/docs/ROADMAP.md" 2>/dev/null |
          $G -viE "experiment" |
          $G -iE "complete|shipped|built|landed" | head -1)
    [ -n "$hit" ] && redB="$redB $base"
done
if [ -z "$redB" ]; then
    t_ok "no not-built plan page whose named deliverable the ROADMAP records complete/shipped"
else
    t_fail "Rule B -- the ROADMAP records the deliverable complete but the status still claims not-built:$redB"
fi

# --- 3: the extraction must still see the four audit-named pages.
# Green on checks 1+2 with these pages DELETED or RENAMED would be a silent
# universe collapse -- the floor's job. So each named page must exist AND
# carry a status line the not-built or supersession regex reads (a status
# line that matches neither means the extraction shape moved, which is
# exactly what this lint is supposed to notice).
missing=""
for p in M92-budget-outcome-clarity M93-superseded-read-elision M94-M95-followups 2026-09-files-and-structures; do
    f="$SMOKE_ROOT/docs/plans/$p.md"
    [ -f "$f" ] || { missing="$missing $p(missing)"; continue; }
    st=$(sed -n '1,12p' "$f" | $G -i "status" | head -1)
    printf '%s\n' "$st" | $G -qiE 'planned|not built|designed, not|superseded|shipped' ||
        missing="$missing $p(status-line-unreadable)"
done
if [ -z "$missing" ]; then
    t_ok "all four audit-named pages are present and their status lines are readable (universe intact)"
else
    t_fail "audit-named pages missing or unreadable to the extraction -- a silent universe collapse: $missing"
fi

t_done
