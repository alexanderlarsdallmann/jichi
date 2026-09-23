#!/bin/sh
# smoke: the deferred register must not advertise work that is done, or hide
# work that is open (M657).
#
# docs/DEFERRED.md exists for one reason, stated at its head: "'recorded as the
# next slice' is only honest if the record is findable." It has failed that job
# structurally three times, each time the SAME way and each time found by a
# person reading the page rather than by anything running:
#
#   M463  two `## Open` sections whose rows had all closed, retitled by hand --
#         "an `## Open` heading with no rows is how this page ends up
#         advertising work that does not exist."
#   M492  four rows done and still filed under an Open heading, with the
#         closure appended BEHIND a reason still written in the present tense,
#         so `--api-base` went on reading "there is no base-URL flag" for four
#         milestones after main.c began parsing one. The page then wrote the
#         rule down: "A closed row under an *Open* heading is not [fine]."
#   M657  six struck-through rows under three Open headings, one Open heading
#         with zero rows, and -- the one that costs most -- illumos absent from
#         the register entirely while PLATFORMS.md, LOW_MEMORY.md and
#         PLATFORM_RETEST.md all carry it as the cheapest never-compiled row.
#
# Three hand-audits of the same invariant is the project's own threshold for
# preferring a lint to an audit. So: the STRUCTURE is checked here forever, and
# the reasons stay a human's job.
#
# WHAT IS AND IS NOT COVERED, stated rather than implied (the M305 rule):
#   checked -- every `## Open`/`## Narrowed` section holds at least one data
#              row; no row under one of those headings is struck through or
#              marked CLOSED/DONE; every platform PLATFORMS.md files as *Never
#              compiled* is named in THIS page's own never-compiled section;
#              and every `path:line` anchor on the page resolves to a file with
#              at least that many lines.
#
#   NOT checked -- whether a reason is still TRUE. That is the M326b rule and it
#              is deliberately a human's job: a reason may hold judgement,
#              evidence, or an unchecked factual claim, and only the third is
#              mechanically decidable at all -- one claim at a time, by going
#              and looking. A lint that tried would encode today's answers and
#              rot exactly like the rows it was policing.
#
#   NOT checked -- path-only mentions (`scripts/tier-v-guix.sh`, `src/main.c`).
#              The page legitimately names artifacts that do NOT exist -- the
#              Guix rig is named precisely as something deliberately not written
#              blind -- so requiring every path to resolve would fail the page
#              for being honest. An anchor carrying a LINE NUMBER is a different
#              claim: it says "here it is in today's tree", and that claim rots.
#              Check 5's universe is those anchors only, and it verifies the
#              anchor is RESOLVABLE, not that the line still holds the subject.
#
#   NOT checked -- the `## Closed` sections. A closed row is allowed to say
#              anything; the invariant is about what an OPEN heading promises.
#
# Floors are the exact counts on the day this was written, per the house rule:
# a lint whose extraction silently returns nothing is green and worthless.
. "$(dirname "$0")/_smoke.sh"

t_plan 5

root=$(cd "$(dirname "$0")/../.." && pwd)
DF="$root/docs/DEFERRED.md"
PL="$root/docs/PLATFORMS.md"
tmp=$(smoke_tmp)

# BOUNDS, not equalities, and the reason is the same one PLATFORM_RETEST's
# driver states for its own drift tolerance: "requiring it to equal today would
# make every new driver a failing build, which is how a gate gets silenced."
# Here the population moves in BOTH directions -- a closed row lowers it, a new
# deferral raises it -- and this check's job is to notice that the EXTRACTION
# broke, which yields something near zero, not to pin the census. Written at
# 26/58 on 2026-09-18 and floored well below that; both directions were seen
# within hours, when illumos was compiled and its row closed (26/57).
JC_DEF_MIN_SECTIONS=20
JC_DEF_MIN_ROWS=45

# Sections and their data rows, in one pass. A section runs from its `## ` to
# the next one; it is OPEN when the heading begins "Open" or "Narrowed". A data
# row is a table row that is neither the separator nor one of the two column
# headers this page uses -- excluding those matters, because a section holding
# only a header would otherwise read as populated.
awk '
/^## / {
    open = ($0 ~ /^## (Open|Narrowed)/)
    sec  = $0
    if (open) { print "SEC\t" sec > SECF; }
    next
}
open && /^[|] / && $0 !~ /^[|]---/ &&
       $0 !~ /^[|] Deferred [|] Why [|] Where [|]/ &&
       $0 !~ /^[|] Was deferred [|] Closed by [|]/ {
    print "ROW\t" sec "\t" $0 > ROWF
}
' SECF="$tmp/sections" ROWF="$tmp/rows" "$DF"
: > "$tmp/sections.x"; : > "$tmp/rows.x"
[ -f "$tmp/sections" ] && cp "$tmp/sections" "$tmp/sections.x"
[ -f "$tmp/rows" ] && cp "$tmp/rows" "$tmp/rows.x"

nsec=$(wc -l < "$tmp/sections.x" | tr -d '[:space:]')
nrow=$(wc -l < "$tmp/rows.x" | tr -d '[:space:]')

# 1: the extraction reached the page at all.
if [ "$nsec" -ge "$JC_DEF_MIN_SECTIONS" ] && [ "$nrow" -ge "$JC_DEF_MIN_ROWS" ]; then
    t_ok "the register yields $nsec open sections and $nrow open rows \
(floors $JC_DEF_MIN_SECTIONS/$JC_DEF_MIN_ROWS)"
else
    t_fail "the register yielded $nsec open sections and $nrow open rows, under \
the floors of $JC_DEF_MIN_SECTIONS/$JC_DEF_MIN_ROWS -- the page's heading or \
table shape changed and the checks below are reading almost nothing. Fix the \
extraction, not the floor."
fi

# 2: no open heading with nothing under it. This is the M463 failure: a heading
# that outlives its rows goes on advertising work that does not exist.
empty=""
while IFS="$(printf '\t')" read -r _ sec; do
    if ! grep -Fq "$(printf 'ROW\t%s\t' "$sec")" "$tmp/rows.x" 2>/dev/null; then
        empty="$empty$sec; "
    fi
done < "$tmp/sections.x"
if [ -z "$empty" ]; then
    t_ok "every open section holds at least one row ($nsec sections)"
else
    t_fail "open section(s) with no rows at all: $empty-- retitle to Closed or \
delete it. An '## Open' heading with no rows advertises work that does not \
exist (M463)."
fi

# 3: no closed row under an open heading. This is the M492 failure, and the one
# the page states as its single unacceptable state.
#
# M710 WIDENED THE VOCABULARY, because the check was evaded by a synonym on its
# first real encounter. A row settled that day was written `**DECIDED AT M710:`
# and left under its Open heading; this check read it as open, because it knew
# only CLOSED and DONE. The author caught it by reading the page -- which is
# exactly the failure mode the lint's own header records happening three times
# and was written to end.
#
# The five are the words that assert a row is FINISHED. `WITHDRAWN` is
# deliberately NOT among them: the strict-green row withdraws a recommendation
# and stays legitimately open, so that word marks a claim rather than a row.
# Capitals are load-bearing for the same reason -- an open row may reason in
# prose about what was decided, and `-E` is case-sensitive here on purpose.
# Verified at M710: the five fire on ZERO open rows as the page then stood, so
# widening cost no false positive.
struck=$(grep -E '~~|\*\*(CLOSED|DONE|DECIDED|SETTLED|RESOLVED)' "$tmp/rows.x" \
    | cut -f2 | sort -u | tr '\n' ';')
if [ -z "$struck" ]; then
    t_ok "no struck-through or CLOSED/DONE/DECIDED/SETTLED/RESOLVED row sits \
under an open heading"
else
    t_fail "closed row(s) under open heading(s): $struck -- move them to a \
'## Closed' section or delete them. The page's own rule: 'A closed row under an \
*Open* heading is not [fine]' (M492)."
fi

# 4: the cross-register universe check -- enumerate the never-compiled set from
# PLATFORMS.md, which OWNS platform verdicts, and require each to be findable
# HERE. Bounded to this page's own never-compiled section on purpose: a bare
# page-wide grep for "illumos" was already green while the row was missing,
# because a closed row elsewhere mentions the word. Audit the universe, not the
# result (M508/M510/M511).
# Anchored on the SECTION heading, not on the column header: `| Platform |
# State | What that means for you |` appears twice in PLATFORMS.md (### Partly
# verified uses it too), and anchoring on the header alone latched onto the
# first table and read four partly-verified rows as the never-compiled set --
# caught by this check's own floor rather than by inspection, which is what the
# floor is for.
awk '/^### Never compiled/{s=1;next} s&&/^### /{exit} s&&/^[|] \*\*/' "$PL" \
    | grep -i 'never compiled' > "$tmp/nc"
# The section's ROWS, not the section. Proved necessary by tooth: the section's
# own prose explains why illumos was missing, so a section-wide grep stayed green
# with the row deleted -- vacuous, and exactly the failure this check was written
# to catch one level up. Audit the universe, not the result.
awk '/^## Open — platforms never compiled/{v=1;next} v&&/^## /{exit}
     v&&/^[|] /&&!/^[|]---/&&!/^[|] Deferred [|] Why [|] Where [|]/' "$DF" > "$tmp/ncsec"
# Floor 1, not 2 (M658). It was 2 the night it was written -- macOS and illumos
# -- and illumos was COMPILED hours later, which dropped the set to one and made
# this check fail. That is the floor doing its job: it said the universe changed
# and left the judgement to a person. The judgement is that the extraction is
# fine and the world moved, so the floor follows the world down. At 0 it fails
# again, deliberately: a matrix with no never-compiled row at all would be worth
# stopping for, whether because the last one was measured or because the table
# shape changed underneath this check.
ncn=$(wc -l < "$tmp/nc" | tr -d '[:space:]')
if [ "$ncn" -lt 1 ]; then
    t_fail "PLATFORMS.md yields $ncn never-compiled rows (expected at least 1) \
-- either the table shape changed and this check is reading nothing, or every \
platform has now been compiled. Both are worth stopping for."
else
    missing=""
    while read -r line; do
        name=$(printf '%s' "$line" | sed 's/^| *//; s/ *|.*//; s/\*\*//g')
        found=0
        # A row names its platform any of the ways the matrix spells it
        # ("macOS / Darwin", "Solaris / illumos"): one token is enough.
        for tok in $(printf '%s' "$name" | tr '/' ' '); do
            if grep -Fqi "$tok" "$tmp/ncsec"; then found=1; fi
        done
        [ "$found" -eq 1 ] || missing="$missing$name; "
    done < "$tmp/nc"
    if [ -z "$missing" ]; then
        t_ok "every never-compiled platform in PLATFORMS.md has a row here ($ncn)"
    else
        t_fail "platform(s) PLATFORMS.md files as Never compiled and this \
register does not carry: $missing-- the register exists to make deferrals \
findable, and a platform nobody has compiled is a deferral"
    fi
fi

# 5: every path:line anchor resolves. A line number is a claim about today's
# tree; a path alone is not (see the header).
grep -oE '`(src|tests|include|scripts|docs)/[A-Za-z0-9_/.-]+\.[a-z]+(:[0-9]+)?`' "$DF" \
    | tr -d '`' | sort -u > "$tmp/cites"
# `ncite`, and deliberately not the obvious two-letter name: smoke_lint check 3
# forbids netcat anywhere in the tier, and its pattern matches that name
# preceded by any non-letter -- which a shell variable reference is. The check is
# blunt on purpose; renaming one variable is cheaper than widening it, and this
# comment avoids the literal sequence for the same reason.
ncite=$(wc -l < "$tmp/cites" | tr -d '[:space:]')
grep ':[0-9]' "$tmp/cites" > "$tmp/anchors" || :
na=$(wc -l < "$tmp/anchors" | tr -d '[:space:]')
# THE FLOOR IS ON THE CITATIONS, NOT ON THE ANCHORS (M661b, third revision and
# the last). The subset carrying a LINE NUMBER is what this check verifies, and
# that subset only ever shrinks as rows close and their anchors are rewritten
# without line numbers -- correctly, because a closed row's line number rots and
# nobody re-checks it. Flooring on it meant a healthy edit tripped the gate three
# times in two days. So the floor now sits on the thing that is stable and large
# -- every `path/to/file.ext` citation on the page -- which still collapses to
# near zero if the extraction breaks, while the verified subset is free to be
# empty. That is the distinction the first two versions could not draw.
JC_DEF_MIN_CITES=15
if [ "$ncite" -lt "$JC_DEF_MIN_CITES" ]; then
    t_fail "only $ncite file citations extracted (floor $JC_DEF_MIN_CITES) -- the \
citation spelling changed and this check is reading nothing"
else
    bad=""
    while read -r a; do
        f=${a%:*}; l=${a##*:}
        if [ ! -f "$root/$f" ]; then
            bad="$bad$a (no such file); "
        else
            n=$(wc -l < "$root/$f" | tr -d '[:space:]')
            [ "$n" -ge "$l" ] || bad="$bad$a (file has $n lines); "
        fi
    done < "$tmp/anchors"
    if [ -z "$bad" ]; then
        t_ok "every path:line anchor resolves ($na of $ncite citations carry a line)"
    else
        t_fail "unresolvable anchor(s): $bad-- a citation with a line number \
claims a location in today's tree"
    fi
fi

t_done
