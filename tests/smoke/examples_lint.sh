#!/bin/sh
# smoke lint: every JSON under examples/ parses (M382).
#
# The example configs are copy-paste material -- docs/MODELS.md says outright
# "copy it to local/config.json and adjust" -- so a broken example is poison
# that fails in the USER'S terminal, on their first attempt, as a jichi
# config error rather than as our defect. Nothing validated them: the
# scaffold packs' inline examples are unit-tested (tests/test_scaffold.c),
# but the 23 standalone examples/**/*.json were covered by nothing.
#
# Parse-validation only, stated scope (the M305 rule): whether an example's
# KEYS are current is config_keys_lint's territory (docs+examples are its
# search corpus), and whether the config semantically works needs a live
# model. This lint answers one question: does the file a user copies parse.
. "$(dirname "$0")/_smoke.sh"

t_plan 2

n=0
bad=""
for f in "$SMOKE_ROOT"/examples/*.json "$SMOKE_ROOT"/examples/*/*.json; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    if ! "$SMOKE_TOOLS/jsonq" '.' "$f" > /dev/null 2>&1; then
        bad="$bad ${f#"$SMOKE_ROOT"/}"
    fi
done

if [ "$n" -ge 15 ]; then
    t_ok "found $n example JSON files (floor 15)"
else
    t_fail "only $n example JSONs found -- the glob moved; fix it, not the floor"
fi

if [ -z "$bad" ]; then
    t_ok "every example JSON parses (jsonq)"
else
    t_fail "unparseable example(s):$bad"
fi

t_done