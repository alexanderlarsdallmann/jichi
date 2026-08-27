#!/bin/sh
# smoke: the `context` subcommand sizes the prompt the agent would get (M311).
#
# `context` and `sysmsg` are dispatched before main()'s asset load, so each
# must load rules/memory/glossary/repo-map/skills/styles itself. `sysmsg`
# did; `context` did not -- so it printed `rules ~0` on every project,
# under-reporting the contributor M308 measured at 70% of a call, while
# COMPACTION.md's example output showed `rules ~800`. On this repository the
# true figure is ~15,200 tokens of system prompt against ~750 reported: the
# gauge was off by 5x on the largest thing in the window.
#
# Four checks. The parity one is the point:
#
#   1. no rules file  -> rules ~0 (the zero must still be reachable, or
#      check 2 proves nothing).
#   2. a rules file   -> rules > 0.
#   3. a BIGGER rules file -> a bigger number (not a constant).
#   4. `context`'s system-prompt tokens == the byte estimate of what
#      `sysmsg` actually prints. This is what stops the two surfaces from
#      describing different prompts, whichever one a future section is
#      added to.
#
# Check 4 relies on the byte/4 estimate being uncalibrated here: smoke_home
# gives a fresh HOME, so there is no calibration.json and the ratio is 1.0.
# If a future change calibrates from something else, this check will say so
# loudly rather than drift.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# repoMap off: it would add a second variable to check 3's comparison, and
# rules are what this driver is about. contextLimit fixes the percentage.
cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"lowResource":false,"contextLimit":32000,"maxRetries":0}
EOF

# M312 renders one line per non-zero section, so a zero section prints NO line
# at all (fourteen zeroes would bury the two lines that matter). An absent line
# therefore means 0 -- deliberately, and worth spelling out: a bare sed would
# yield the empty string here and `[ "" -gt 0 ]` is a shell error, not a
# comparison, so the zero case has to be named rather than fallen into.
ctx_rules() {
    _cr=$( (cd "$ws" && "$BIN" --config "$tmp/config.json" context < /dev/null \
           2>/dev/null) | sed -n 's/^ *rules  *~\([0-9]*\) *$/\1/p' )
    if [ -z "$_cr" ]; then
        echo 0
    else
        echo "$_cr"
    fi
}
ctx_sys() {
    (cd "$ws" && "$BIN" --config "$tmp/config.json" context < /dev/null \
        2>/dev/null) | sed -n 's/^  system prompt *~\([0-9]*\).*/\1/p'
}

# --- 1: the zero is reachable ------------------------------------------------
rm -f "$ws/AGENTS.md" "$ws/CLAUDE.md"
r0=$(ctx_rules)
if [ "$r0" = "0" ]; then
    t_ok "no rules file: context reports rules ~0"
else
    t_fail "expected rules ~0 with no AGENTS.md, got '$r0'"
fi

# --- 2: a rules file is counted ---------------------------------------------
i=0
: > "$ws/AGENTS.md"
while [ "$i" -lt 20 ]; do
    echo "Project rule $i: prefer the explicit form over the clever one." \
        >> "$ws/AGENTS.md"
    i=$((i + 1))
done
r1=$(ctx_rules)
if [ -n "$r1" ] && [ "$r1" -gt 0 ]; then
    t_ok "with an AGENTS.md: context reports rules ~$r1 (was 0 before M311)"
else
    t_fail "rules still reported as '$r1' with a 20-line AGENTS.md -- \
the subcommand is not loading instruction files (M311 regression)"
fi

# --- 3: the number tracks the file, it is not a constant ---------------------
i=0
while [ "$i" -lt 60 ]; do
    echo "Project rule $i: prefer the explicit form over the clever one." \
        >> "$ws/AGENTS.md"
    i=$((i + 1))
done
r2=$(ctx_rules)
if [ -n "$r2" ] && [ -n "$r1" ] && [ "$r2" -gt "$r1" ]; then
    t_ok "a 4x larger AGENTS.md reports more ($r1 -> $r2)"
else
    t_fail "rules figure did not grow with the file ($r1 -> $r2)"
fi

# --- 4: context and sysmsg describe the SAME prompt --------------------------
# jc_compact_estimate_text is bytes/4 (uncalibrated), and `sysmsg` prints the
# built prompt plus one trailing newline that jc_context_report never sees.
sysbytes=$(cd "$ws" && "$BIN" --config "$tmp/config.json" sysmsg < /dev/null \
           2>/dev/null | wc -c)
sysbytes=$((sysbytes - 1))
want=$((sysbytes / 4))
got=$(ctx_sys)
if [ "$got" = "$want" ]; then
    t_ok "context's system prompt (~$got) is the estimate of sysmsg's $sysbytes bytes"
else
    t_fail "context says ~$got, sysmsg is $sysbytes bytes (~$want) -- the two \
surfaces are describing different prompts"
fi

# --- 5: the section lines account for the whole system prompt (M312) ----------
# The user-visible form of the invariant: no unexplained remainder. Checked at
# the report level because that is where the old form was wrong -- six named
# sub-parts against a stated total, with the rest unaccounted.
#
# Also the reason check 1 is not left to stand alone: "no rules line" is what a
# TOTALLY ABSENT breakdown looks like too, so something has to prove the lines
# exist. Summing them does.
report=$( (cd "$ws" && "$BIN" --config "$tmp/config.json" context < /dev/null \
          2>/dev/null) )
lines=$(printf '%s\n' "$report" | sed -n 's/^    [a-z][a-z ]*~\([0-9]*\) *$/\1/p')
nlines=$(printf '%s\n' "$lines" | grep -c .)
partsum=$(printf '%s\n' "$lines" | awk '{s += $1} END {printf "%d", s+0}')
stated=$(printf '%s\n' "$report" | sed -n 's/^  system prompt *~\([0-9]*\).*/\1/p')
# Each line's token figure is an independent integer division, so the sum can sit
# up to one token per line below the stated total. Any real gap is far larger:
# before M312 the unnamed remainder here was the entire persona + craft + safety.
slack=$((stated - partsum))
if [ "$nlines" -ge 4 ] && [ "$slack" -ge 0 ] && [ "$slack" -le "$nlines" ]; then
    t_ok "$nlines section lines account for the system prompt ($partsum of ~$stated)"
else
    t_fail "sections ($partsum over $nlines lines) do not account for ~$stated \
-- unexplained remainder of $slack tokens"
fi

t_done
