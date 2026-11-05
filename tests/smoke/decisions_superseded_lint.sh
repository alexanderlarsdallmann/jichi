#!/bin/sh
# smoke lint: a superseded decision names the milestone that superseded it,
# and that milestone exists (M628).
#
# THE NORM. PROJECT_RECORDS.md tells the learner "never delete a decision;
# supersede it -- the wrong turn is half the value of the register", and
# jichi's own docs/DECISIONS.md had 302 rows and no way to say a row was
# reversed. M628 introduced the convention: a superseded row KEEPS its text
# and gains a leading `**Superseded at M<n>** --` in its decision cell. This
# lint holds the marker to its shape and to the ROADMAP: a marker naming a
# milestone with no `### M<n>` entry is a retraction pointing at nothing.
#
# UNIVERSE, STATED: every `Superseded at M<n>` marker in a TABLE ROW of
# docs/DECISIONS.md (lines starting with `|`). The header's own prose shows the
# marker's shape with a literal `M<n>` -- the first draft of this lint flagged
# its own documentation, which is the usual way a universe turns out too wide.
# Today that set is EMPTY -- the register was created at M293 and is not
# back-filled, and no row has been reversed since the convention exists. A
# lint over an empty set proves only that the format is guarded for the first
# marker; its teeth are shown by planting a bogus one (the ROADMAP entry
# records the ritual). The count is printed so the day the set is non-empty
# is visible.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
D="$SMOKE_ROOT/docs/DECISIONS.md"
R="$SMOKE_ROOT/docs/ROADMAP.md"
tmp=$(smoke_tmp)

# --- 1: every marker is well-formed (bold, one milestone number) ------------------
grep -n "^| .*Superseded at" "$D" > "$tmp/markers" || true
n=$(grep -c . "$tmp/markers" || true)
bad=$(grep -v '\*\*Superseded at M[0-9][0-9]*\*\*' "$tmp/markers" || true)
if [ -z "$bad" ]; then
    t_ok "$n superseded marker(s), each in the form **Superseded at M<n>**"
else
    t_fail "malformed superseded marker(s): $(printf '%s' "$bad" | tr '\n' ';' | head_bytes 200)"
fi

# --- 2: every named milestone has a ROADMAP entry ---------------------------------
missing=""
for m in $(sed -n 's/.*\*\*Superseded at \(M[0-9][0-9]*\)\*\*.*/\1/p' "$tmp/markers" | sort -u); do
    grep -q "^### $m " "$R" || missing="$missing $m"
done
if [ -z "$missing" ]; then
    t_ok "every superseding milestone has a ROADMAP entry"
else
    t_fail "superseded-at names milestone(s) with no ROADMAP entry:$missing"
fi
# --- 3: every detail anchor is defined exactly once (the 2026-10-05 audit
# found 14 ids defined twice; a same-milestone decision recorded in a second
# section reused the first section's ordinal, and 14 of 28 index rows
# silently resolved to the wrong row).
grep -oE '<a id="[a-z0-9-]+">' "$D" | sed 's/<a id="\([^"]*\)">/\1/' | sort | uniq -d > "$tmp/dupids" || true
if [ -s "$tmp/dupids" ]; then
    t_fail "duplicate detail anchor ids (ambiguous index links): $(tr '\n' ' ' < "$tmp/dupids")"
else
    t_ok "every detail anchor id is defined exactly once"
fi

# --- 4: every in-register detail link resolves to a defined anchor.
grep -oE '\]\(#dec-[a-z0-9-]+\)' "$D" | sed 's/^](#//; s/)$//' | sort -u > "$tmp/refs"
grep -oE '<a id="[a-z0-9-]+">' "$D" | sed 's/<a id="\([^"]*\)">/\1/' | sort -u > "$tmp/defs"
comm -23 "$tmp/refs" "$tmp/defs" > "$tmp/broken" || true
if [ -s "$tmp/broken" ]; then
    t_fail "in-register detail links to undefined anchors: $(tr '\n' ' ' < "$tmp/broken")"
else
    t_ok "every in-register detail link resolves to a defined anchor"
fi

t_done

