#!/bin/sh
# smoke: the system prompt carries today's date (M352).
#
# The model has no clock. Everything in this project is dated -- the
# registers, the analysis pages, the memory practice "convert relative dates
# to absolute" -- and a model that is not told the date does not leave dates
# blank; it guesses from its training priors. One ISO line in the environment
# block fixes it: numeric %Y-%m-%d only, immune to the LC_TIME that the
# display layer deliberately localizes.
#
# The `sysmsg` subcommand shares the one prompt builder with the live request
# (M311: report and prompt cannot describe different prompts), so this driver
# is offline and instant. The midnight race (date computed here vs inside
# jichi) is real but needs the run to straddle 00:00 in the same second;
# accepted.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)

write_config "$tmp/c.json" 9
today=$(date +%Y-%m-%d)
"$BIN" --config "$tmp/c.json" sysmsg > "$tmp/out" 2>/dev/null

# --- 1: today's actual date, in the environment block -------------------------
if grep -q "^- Today's date: $today\$" "$tmp/out"; then
    t_ok "the prompt says today is $today"
else
    t_fail "no correct date line: $(grep -i "today" "$tmp/out" | head -1)"
fi

# --- 2: ISO shape, anchored -- no localized month names, no extra decoration --
if grep -Eq "^- Today's date: 20[0-9]{2}-[0-9]{2}-[0-9]{2}\$" "$tmp/out"; then
    t_ok "the line is bare ISO (locale-proof by construction)"
else
    t_fail "the date line drifted from the ISO shape"
fi

t_done
