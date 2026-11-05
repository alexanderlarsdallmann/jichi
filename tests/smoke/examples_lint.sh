#!/bin/sh
# smoke lint: every JSON under examples/ parses (M382); the bench inventory
# names every bench on disk (M906).
#
# The example configs are copy-paste material -- docs/MODELS.md says outright
# "copy it to local/config.json and adjust" -- so a broken example is poison
# that fails in the USER'S terminal, on their first attempt, as a jichi
# config error rather than as our defect. Nothing validated them: the
# scaffold packs' inline examples are unit-tested (tests/test_scaffold.c),
# but the 23 standalone examples/**/*.json were covered by nothing.
#
# The bench inventory is the same defect class in a list instead of a file:
# `jichi init --list` prints the domain benches from a hardcoded string in
# main.c, examples/ holds them as directories, and examples/README.md tables
# them -- three copies of the same set. `slm-lab` (added 2026-09) appeared in
# the directory and the README but not in the printf, so a user who read only
# `init --list` would not have found a bench that `init --copy` would have
# happily installed. Check 3 measures the printf by RUNNING the binary (the
# audit's spec -- grepping the C source would let a string edit rot it back
# without a build catching it), and diffs it against the directory set.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

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

# ---- 3: the init --list bench inventory names every bench on disk ---------
# A bench is a directory under examples/ that carries a START_HERE.md (13
# today -- the floor is that exact count). The printed list is extracted from
# the line between the "Domain benches" marker and the "Each has" marker.
benchdirs=$(ls "$SMOKE_ROOT"/examples/*/START_HERE.md 2>/dev/null \
    | sed 's|.*/examples/||; s|/START_HERE.md$||' | sort -u)
nb=$(printf '%s\n' "$benchdirs" | grep -c .)
if [ "$nb" -lt 13 ]; then
    t_fail "only $nb bench directories (floor 13) -- the layout moved"
    t_done
fi
listout=$(with_deadline 60 "$BIN" init --list < /dev/null 2>/dev/null \
    | sed -n '/Domain benches/,/Each has/p')
listnames=$(printf '%s\n' "$listout" \
    | sed -n 's/^  //p' \
    | tr ',' '\n' \
    | sed 's/[[:space:]]//g' \
    | grep -E '^[a-z][a-z0-9-]*$' | sort -u)
printf '%s\n' "$benchdirs" > "$tmp/bench.disk"
printf '%s\n' "$listnames" > "$tmp/bench.list"
missing=$(comm -23 "$tmp/bench.disk" "$tmp/bench.list" | tr '\n' ' ')
phantom=$(comm -13 "$tmp/bench.disk" "$tmp/bench.list" | tr '\n' ' ')
if [ -z "$missing" ] && [ -z "$phantom" ]; then
    t_ok "init --list names all $nb benches on disk, and none invented"
else
    t_fail "bench inventory drift: missing-from-list: ${missing:-none}; not-on-disk: ${phantom:-none}"
fi

t_done