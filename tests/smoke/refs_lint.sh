#!/bin/sh
# smoke lint: the @-reference vocabulary may not drift from its docs (M377).
#
# The registry series (flags M370, subcommands M297, tools, slash commands
# M295, config keys M305/M371, events M366, tags M369, keys M372) owns every
# user-facing vocabulary EXCEPT this one: the @-references a user types into
# a plain message (@diff, @url:<u>, @ref:<name>, ...). The 2026-08-11 seam
# survey hand-audited them clean -- and that audit itself false-alarmed twice
# before reading the parser, which is the standing argument (TEST_INTEGRITY:
# prefer a lint to an audit) for extracting the tokens from jc_refs_scan's
# own strncmp calls and requiring each in REFERENCES.md.
#
# Scope: prefix/bare tokens only. The path kinds (@<file>, @photo.png) are
# recognized by shape, not by token, and are documented as prose.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)

REFS_SRC="$SMOKE_ROOT/src/command/jc_refs.c"
REFS_DOC="$SMOKE_ROOT/docs/REFERENCES.md"

# --- the vocabulary: the scanner's own token comparisons --------------------
grep -oE 'strncmp\(text \+ s, "[a-z]+:?"' "$REFS_SRC" \
    | sed 's/.*"\([a-z:]*\)"/\1/' | sort -u > "$tmp/tokens"
ntok=$(grep -c . "$tmp/tokens")
if [ "$ntok" -ge 10 ]; then
    t_ok "extracted $ntok @-reference tokens from jc_refs_scan"
else
    t_fail "suspiciously few tokens ($ntok) -- the scanner's shape moved; fix the extraction, not the floor"
fi

# --- every token documented as @<token> in REFERENCES.md --------------------
missing=""
while IFS= read -r tok; do
    grep -qF "@$tok" "$REFS_DOC" || missing="$missing @$tok"
done < "$tmp/tokens"
if [ -z "$missing" ]; then
    t_ok "every scanned @-reference token appears in REFERENCES.md"
else
    t_fail "undocumented @-reference(s):$missing"
fi

# --- the matcher can miss (two-sided; the config_keys_lint rule) ------------
if grep -qF "@zzz-invented-ref:" "$REFS_DOC"; then
    t_fail "the invented token is 'documented' -- the doc search is broken"
else
    t_ok "the matcher can miss: an invented token is not found"
fi

t_done