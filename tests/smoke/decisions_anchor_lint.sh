#!/bin/sh
# smoke lint: DECISIONS.md anchors must be unique, well-formed, and resolvable (M904, def-319).
#
# THE DEFECT THIS EXISTS FOR, measured 2026-10-05 (JICHI-DECISIONS-GAP-ANALYSIS;
# re-measured 2026-10-07): the M170 project-rename sweep (2026-07-27) wrote nine
# `<a id="dec-m?">` anchors -- literal question marks, not milestone numbers --
# and eight `[M?](#dec-m?)` links pointing at them. A later fix (d602c7d6)
# attributed most of the audit's 14 rot spots but left these nine, because they
# are malformed in BOTH halves: the anchor id contains `?`, and the link
# `(M?)` does not name the milestone. The class is what a future reader hits:
# click the link, and the target is one of two identical `#dec-m?` anchors, so
# the browser lands on whichever it finds first -- or, if the sweep had made
# them unique-but-wrong, on the wrong decision entirely.
#
# Three checks, each a distinct rot:
#   1. every `<a id>` value is unique (a duplicate makes one link ambiguous).
#   2. no anchor id is DUPLICATED and contains `?`. A lone `?` id is the legal
#      "milestone not yet attributed" state: six rows (the invariant-vs-goal
#      finding-10 decision, the diagram-per-chapter rule, the two ANECDOTES
#      #39 lessons, the two #40 lessons) are recorded in anecdotes and the
#      zigodot analysis docs, not in a ROADMAP entry, so their milestone is
#      genuinely undecided (M904 left them `M?`; the audit's original rot was
#      that TWO rows shared each of dec-m?/dec-m?-2, making links ambiguous).
#      A `?` id that appears twice or is pointed at by a link is the rot class.
#   3. every in-file `(#id)` link resolves to an `<a id="id">` (a link to a
#      removed or retyped id is the anchor-rot the audit found in GATE_INTEGRITY
#      before 77cfac89 fixed it).
#
# GREP AND SED ONLY (the tier's premise).
. "$(dirname "$0")/_smoke.sh"

t_plan 4

G=/usr/bin/grep
[ -x "$G" ] || G=grep
DEC="$SMOKE_ROOT/docs/DECISIONS.md"

# Floor: DECISIONS.md is a large register (400+ anchors). An empty or tiny
# extraction means the file moved or the glob broke -- a silent pass over an
# empty set is worse than no lint (the M390 lesson).
nanchors=$($G -oE '<a id="[^"]+"' "$DEC" | $G -c .)
if [ "$nanchors" -lt 100 ]; then
    t_fail "only $nanchors <a id> anchors in DECISIONS.md -- the register moved or the extraction broke"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_done
fi
t_ok "scanned $nanchors anchors in DECISIONS.md"

# --- 1: duplicate anchor ids
dups=$($G -oE '<a id="[^"]+"' "$DEC" | /usr/bin/sed 's/<a id="//; s/"$//' | sort | uniq -d)
if [ -z "$dups" ]; then
    t_ok "every anchor id is unique"
else
    t_fail "duplicate anchor id(s) make a link ambiguous: $(printf '%s' "$dups" | tr '\n' ' ')"
fi

# --- 2: no DUPLICATED '?' id. A lone `?` id is the legal "milestone not yet
# attributed" state (six rows are recorded in anecdotes / zigodot analysis docs
# with no ROADMAP entry); the rot class is the M170 sweep's ORIGINAL state,
# where two rows each shared `dec-m?` and `dec-m?-2`, so a link to either id
# could not say which decision it meant.
dupq=$($G -oE '<a id="[^"]*\?[^"]*"' "$DEC" | /usr/bin/sed 's/<a id="//; s/"$//' | sort | uniq -d)
if [ -z "$dupq" ]; then
    t_ok "no anchor id is both a '?' placeholder and duplicated (an ambiguity)"
else
    t_fail "duplicated '?' anchor id(s) make a link ambiguous: $(printf '%s' "$dupq" | tr '\n' ' ')"
fi

# --- 3: every in-file (#id) link resolves to a defined anchor
# Collect defined ids, then each referenced id, and diff. A referenced id with
# no matching definition is the anchor rot the audit found in GATE_INTEGRITY
# (three line-anchors off by +1002/+2730/+2746) before 77cfac89 fixed them.
defined=$($G -oE '<a id="[^"]+"' "$DEC" | /usr/bin/sed 's/<a id="//; s/"$//' | sort -u)
referenced=$($G -oE '\(#[^)]+\)' "$DEC" | /usr/bin/sed 's/^(#//; s/)$//' | sort -u)
unresolved=$(printf '%s\n' "$referenced" | while read -r id; do
    [ -z "$id" ] && continue
    printf '%s\n' "$defined" | $G -qxF "$id" || printf '%s\n' "$id"
done)
if [ -z "$unresolved" ]; then
    t_ok "every in-file (#id) link resolves to a defined anchor"
else
    t_fail "link target(s) with no matching <a id>: $(printf '%s' "$unresolved" | tr '\n' ' ')"
fi

t_done
