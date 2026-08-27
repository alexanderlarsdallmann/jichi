#!/bin/sh
# smoke lint: a documented claim that CITES a test must cite one that exists
# (M576).
#
# WHY THIS EXISTS, and it is the one actionable half of a larger finding. Nine
# false claims were planted one at a time into the real documentation and every
# lint was run against each:
#
#   caught (4/4 mechanical)      a wrong advertised number; a command-line option
#                                that does not exist; a function name that does
#                                not resolve; an unindexed top-level page
#   MISSED (0/5 prose)           "any other key refuses the call immediately";
#                                "the count persists for the whole session";
#                                "the digits were chosen to satisfy DIN 5009";
#                                "FIVE refusals in a row end the turn"; a false
#                                description attached to a real anchor
#
# The five that survived are about BEHAVIOUR, LIFETIME, RATIONALE and THRESHOLDS
# -- exactly what a reader relies on, and exactly the kind that was wrong seven
# times in one session. No lint here can read a sentence and tell whether it is
# true. `docs/analysis/2026-08-24-trusting-generated-documentation.md` has the
# full measurement.
#
# SO THE CONVENTION IS: a documented claim about behaviour NAMES THE DRIVER AND
# CHECK THAT PINS IT --
#
#     three refusals in a row end the turn (`tests/smoke/deny_stops.sh` check 3)
#
# That does not make the sentence machine-verifiable. It does three things that
# are worth having: a reader can check it in one step, a change that breaks the
# behaviour breaks a NAMED test, and a claim with no test to cite becomes
# VISIBLY a claim with no test -- which the prose currently hides.
#
# AND THIS LINT MAKES THE CITATION ITSELF MECHANICAL. It cannot tell whether the
# sentence is true, but it can guarantee the thing you were told to go and read
# is really there, and really has that check.
#
# IT FOUND ONE BEFORE IT WAS WRITTEN. docs/analysis/2026-08-23-chrome-width.md
# cited `tests/smoke/chrome_width_lint.sh` check 4. That driver existed when the
# page was written and M557 DELETED IT three milestones later -- the chrome
# sentences moved into the catalog, so there were no copies left to keep in step.
# The page went on pointing at it for twenty milestones. A citation rots exactly
# like the code it describes.
#
# NOT EVERY "check N" IS A CITATION, which is the universe question. docs/ holds
# 242 mentions of "check N"; only 46 name a driver alongside. The rest are prose
# references inside a milestone entry that named its driver paragraphs earlier.
# Demanding a citation everywhere would fail ~198 legitimate sentences, so this
# lint governs the citations that EXIST rather than mandating new ones: write one
# and it must resolve; write prose and nothing is imposed.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
cd "$SMOKE_ROOT" || exit 1

# Extraction, printed rather than merely counted: a lint whose extraction is
# wrong reports a clean universe it never looked at.
#
# GREP AND SED ONLY. The smoke tier validates builds on machines that have no
# scripting language beyond POSIX sh -- that is the tier's whole premise, and
# smoke_lint refuses any driver that reaches for one. The first version of this
# extraction used one and was rejected on its first run, which is that lint doing
# exactly its job. (Its check greps for the interpreter's NAME anywhere in the
# file, so even a comment explaining the rule trips it -- hence this wording.)
$G -rno '`\(tests/smoke/\)\?[a-z_0-9]*\.sh`[^`]\{0,40\}check [0-9][0-9]*' \
    "$SMOKE_ROOT"/docs --include='*.md' 2>/dev/null \
  | sed 's|`tests/smoke/|`|' \
  | sed 's|^\([^:]*\):\([0-9]*\):`\([a-z_0-9]*\)\.sh`.*check \([0-9][0-9]*\)$|\1:\2:\3:\4|' \
  > "$tmp/cites"
ncite=$($G -c . "$tmp/cites" 2>/dev/null || true)
[ -n "$ncite" ] || ncite=0

# ---- 1: the universe is the one measured, and the matcher can say no -------
# Floored at today's count so the extraction cannot silently collapse, and a
# citation naming a driver that certainly does not exist must NOT be extracted
# as valid -- otherwise checks 2 and 3 pass by finding nothing.
if [ "$ncite" -ge 44 ] &&
   ! $G -q 'a_driver_that_does_not_exist' "$tmp/cites"
then
    t_ok "$ncite test citations extracted from docs/ (measured: 46)"
else
    t_fail "citation extraction collapsed: $ncite found (want >= 44). Checks 2 \
and 3 test nothing on an empty set. First lines: \
$(head -2 "$tmp/cites" 2>/dev/null | tr '\n' ' ')"
fi

# ---- 2: every cited driver exists -----------------------------------------
bad=""
while IFS=: read -r f n drv num; do
    [ -n "$drv" ] || continue
    [ -f "tests/smoke/$drv.sh" ] || bad="$bad
  $f:$n cites $drv.sh"
done < "$tmp/cites"
if [ -z "$bad" ]; then
    t_ok "every cited driver exists"
else
    t_fail "documentation cites a driver that is not there:$bad
A citation is a promise that a reader can go and check. When the driver is
deleted the promise rots silently -- which is how a page pointed at
chrome_width_lint.sh for twenty milestones after M557 removed it. Either update
the citation or, if the mention is historical, drop the citation syntax so it no
longer reads as something to verify."
fi

# ---- 3: and every cited CHECK NUMBER is inside that driver's plan ---------
# The half that catches drift rather than deletion: a driver that loses checks,
# or a citation copied from a longer sibling, points at a check that never runs.
bad=""
while IFS=: read -r f n drv num; do
    [ -n "$drv" ] || continue
    [ -f "tests/smoke/$drv.sh" ] || continue
    plan=$($G -o 't_plan [0-9]*' "tests/smoke/$drv.sh" | head -1 | cut -d' ' -f2)
    [ -n "$plan" ] || plan=0
    [ "$num" -le "$plan" ] || bad="$bad
  $f:$n cites $drv.sh check $num, but its plan is $plan"
done < "$tmp/cites"
if [ -z "$bad" ]; then
    t_ok "every cited check number is within its driver's plan"
else
    t_fail "a citation points past the end of its driver:$bad
The check numbers in a driver are positional, so a removed check silently
renumbers everything after it. Re-read the driver and cite what it now says."
fi

# ---- 4: the convention is written down where a writer will meet it -------
# A convention nobody states is a convention nobody follows, and this lint would
# then police an empty set forever. Asserted against the page that teaches it.
if $G -q 'names the driver and check that pins it' \
        docs/analysis/2026-08-24-trusting-generated-documentation.md
then
    t_ok "the citation convention is stated in the analysis that motivates it"
else
    t_fail "the convention is not written down, so this lint governs a habit \
nobody was told about. It belongs in \
docs/analysis/2026-08-24-trusting-generated-documentation.md, with the \
measurement that produced it."
fi

t_done
