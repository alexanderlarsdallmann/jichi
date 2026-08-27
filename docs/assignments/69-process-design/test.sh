#!/bin/sh
# Structural floor for a design doc: it must ADDRESS every requirement. The floor
# checks that each requirement id in REQUIREMENTS.md is referenced in DESIGN.md
# (traceability) -- whether the design is GOOD is your judgment; whether it
# covers the requirements a script can check.
cd "$(dirname "$0")" || exit 1
[ "$(grep -c . DESIGN.md)" -ge 5 ] || { echo "FAIL: DESIGN.md is basically empty"; exit 1; }
missing=""
# The ids, as whole words: `tr -cs` turns runs of non-word characters into
# newlines, so `grep -x` then matches a complete token. (A `\b` word boundary
# would be shorter and is a GNU extension -- on a BSD it matches nothing and
# this loop would silently trace no requirements at all.)
for r in $(tr -cs 'A-Za-z0-9_' '\n' < REQUIREMENTS.md | grep -xE 'R[0-9]+' | sort -u); do
    grep -qE "(^|[^A-Za-z0-9_])$r([^A-Za-z0-9_]|$)" DESIGN.md || missing="$missing $r"
done
[ -z "$missing" ] || { echo "FAIL: DESIGN.md does not address requirement(s):$missing -- trace each requirement to the design that satisfies it"; exit 1; }
echo "PASS: the design traces every requirement"
