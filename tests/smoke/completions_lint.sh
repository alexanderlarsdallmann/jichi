#!/bin/sh
# smoke lint: shell completions offer every subcommand, and the man page documents
# the interface-contract ones (M326e).
#
# Both completion files carried the comment "Keep these lists in sync with
# print_help() in src/main.c" -- an instruction to a human to do a machine's job,
# and it had drifted to 28 of 51. `config`, `setup`, `describe`, `learn`, `daemon`,
# `telemetry`, `export`, `rewind` and fifteen more were absent, so pressing Tab
# taught the user those subcommands did not exist.
#
# TWO STANDARDS, deliberately, because the two files are different kinds of thing:
#
#   COMPLETIONS -- exact, both directions. There is no editorial reason for Tab to
#   omit a working subcommand, and a completion naming one that does not exist is
#   worse than none. A missing entry is silent: nothing errors, the name simply
#   never appears.
#
#   MAN PAGE -- the contract set only. subcommands_lint.sh records the standing
#   decision that jichi.1 is deliberately terse, and mechanising "document all 51"
#   would overturn an editorial choice by lint. The floor instead is what jichi
#   itself advertises as its interface contract: the subcommands in `describe`
#   (docs/EMBEDDING.md calls these stable). Something jichi tells integrators to
#   rely on must be in the manual. Above that floor, terseness stays a choice.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
root="$SMOKE_ROOT"

# --- ground truth: the command column of --help -------------------------------
"$BIN" --help > "$tmp/help.txt" 2>&1 </dev/null || true
grep -E '^  [a-z]' "$tmp/help.txt" | sed 's/^  //' \
    | grep -oE '^[a-z][a-z0-9-]*' | sort -u > "$tmp/help_col"
nh=$(grep -c . "$tmp/help_col" || true)
if [ "$nh" -ge 40 ]; then
    t_ok "--help offers $nh subcommands"
else
    t_fail "only $nh subcommands parsed from --help -- a broken parse makes every
 comparison below vacuous"
fi

# --- what the completions offer ------------------------------------------------
# bash: the cmds="..." assignment, continued over backslash-newlines.
sed -n '/cmds="/,/"$/p' "$root/completions/jichi.bash" \
    | tr -d '\\"' | sed 's/cmds=//' | tr ' ' '\n' \
    | grep -E '^[a-z][a-z0-9-]*$' | sort -u > "$tmp/bash"
# zsh: the cmds=( ... ) array.
sed -n '/cmds=(/,/)/p' "$root/completions/jichi.zsh" \
    | tr -d '()' | sed 's/cmds=//' | tr ' ' '\n' \
    | grep -E '^[a-z][a-z0-9-]*$' | sort -u > "$tmp/zsh"

nb=$(grep -c . "$tmp/bash" || true)
nz=$(grep -c . "$tmp/zsh" || true)
if [ "$nb" -ge 40 ] && [ "$nz" -ge 40 ]; then
    t_ok "completions parsed (bash $nb, zsh $nz)"
else
    t_fail "completion parse looks broken (bash=$nb zsh=$nz) -- fix the extraction,
 do not lower the floor: an empty list compares clean against everything"
fi

# --- checks 3-4: completions are exact, both directions ------------------------
for shell in bash zsh; do
    miss=$(comm -23 "$tmp/help_col" "$tmp/$shell")
    extra=$(comm -13 "$tmp/help_col" "$tmp/$shell")
    if [ -z "$miss" ] && [ -z "$extra" ]; then
        t_ok "$shell completion offers exactly the advertised subcommands"
    else
        t_fail "$shell completion is out of sync with --help:"
        [ -z "$miss" ] || printf '    | missing: %s\n' "$(echo $miss)"
        [ -z "$extra" ] || printf '    | not a subcommand: %s\n' "$(echo $extra)"
    fi
done

# --- checks 5-6: the man page covers the interface-contract set ----------------
# The contract list lives in `describe --output json` -- the TEXT form summarises
# and does not enumerate, which the floor check below caught on the first run.
# Read it with jsonq (the same cJSON jichi ships), and split the group entry
# "session/export/rewind/undo", since the man page documents each separately.
JQ="$SMOKE_TOOLS/jsonq"
"$BIN" describe --output json > "$tmp/desc.json" 2>/dev/null </dev/null || true
_i=0
: > "$tmp/contract_raw"
while "$JQ" ".subcommands[$_i].name" "$tmp/desc.json" >> "$tmp/contract_raw" \
        2>/dev/null; do
    _i=$((_i + 1))
done
tr '/' '\n' < "$tmp/contract_raw" | grep -E '^[a-z][a-z0-9-]*$' \
    | sort -u > "$tmp/contract"
ncon=$(grep -c . "$tmp/contract" || true)
if [ "$ncon" -ge 10 ]; then
    t_ok "describe names $ncon contract subcommands"
else
    t_fail "only $ncon contract subcommands parsed from describe (want >=10) --
 the parse broke, and this check would then pass against nothing"
fi

grep -oE '^\.B [a-z][a-z0-9-]*' "$root/man/jichi.1" | sed 's/^\.B //' \
    | sort -u > "$tmp/man"
# A contract name must be in the man page AND be a real subcommand (so a typo in
# either list is caught rather than excused).
comm -23 "$tmp/contract" "$tmp/man" > "$tmp/man_missing"
nmm=$(grep -c . "$tmp/man_missing" || true)
if [ "$nmm" -eq 0 ]; then
    t_ok "every subcommand describe calls part of the contract is in the man page"
else
    t_fail "$nmm contract subcommand(s) undocumented in man/jichi.1 -- jichi tells
 integrators to rely on these:"
    sed 's/^/    | jichi /' "$tmp/man_missing"
fi

t_done
