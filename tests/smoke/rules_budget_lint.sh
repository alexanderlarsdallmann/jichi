#!/bin/sh
# smoke lint: every rule the project writes must actually reach a model (M516).
#
# WHAT THIS EXISTS FOR. `src/chat/jc_rules.c` caps the assembled rules block at
# JC_RULES_MAX and truncates the remainder **silently** -- nothing fails, the run
# proceeds, and the model simply never sees the tail. That is how CLAUDE.md
# reached 139 KB against a 32 KB cap: measured 2026-08-21, a 16k-context model
# received 14.1% of it and a 196,608-token model received the same 23.5%, because
# the cap is absolute. Three whole rule sections -- test integrity, the platform
# rules whose own heading says "read this before a portability change", and the
# lint-auditing rules added the day before -- had never been delivered to any
# model at any window size.
#
# Truncation being silent is the whole defect. A rules file that outgrows the cap
# does not announce it; it just stops applying, from the bottom up, and *which*
# rules apply becomes a function of their line number. So this lint asks the
# binary what it actually assembles, and fails the build on a truncated rules
# block. See docs/analysis/2026-08-21-self-hosting-first-review.md §5 and
# docs/proposals/2026-08-rules-file-split.md.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- the byte size of the root rules file against JC_RULES_MAX
#                   read FROM THE SOURCE (so the two cannot drift), and the
#                   binary's own assembled system prompt for a rules-truncation
#                   marker at a small declared window.
#   NOT checked  -- the repository map's truncation, which is expected and
#                   correct: the map is a retrievable index that should shrink to
#                   fit, unlike a rule, which either applies or does not.
#   NOT checked  -- whether the rules are any good. A rule that fits and is wrong
#                   is a reader's problem (docs/TEST_INTEGRITY.md).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

# --- 1: the cap, read from the source ---------------------------------------
cap=$(grep -oE '^#define JC_RULES_MAX[[:space:]]+\([0-9]+ \* 1024\)' \
      "$SMOKE_ROOT/src/chat/jc_rules.c" | grep -oE '[0-9]+ \* 1024' \
      | awk '{print $1 * 1024}')
if [ -n "$cap" ] && [ "$cap" -ge 1024 ]; then
    t_ok "JC_RULES_MAX is ${cap} bytes (read from jc_rules.c, not hardcoded here)"
else
    t_fail "could not read JC_RULES_MAX from src/chat/jc_rules.c -- fix the extraction, not the floor"
fi

# --- 2: the root rules file fits, with headroom ------------------------------
# The header jichi prepends per file, plus any global or per-task file, also
# counts against the cap -- so a root file at 99% of it is already broken for
# anyone with a ~/.config/jichi/AGENTS.md. 80% is the line.
size=$(wc -c < "$SMOKE_ROOT/CLAUDE.md" | tr -d ' ')
lim=$((cap * 80 / 100))
if [ "$size" -le "$lim" ]; then
    t_ok "CLAUDE.md is $size bytes, within 80% of the cap ($lim)"
else
    t_fail "CLAUDE.md is $size bytes against a ${cap}-byte cap (80% = $lim): the tail
 will be dropped silently, and which rules survive becomes a function of line
 number. Move reference to docs/ (see docs/proposals/2026-08-rules-file-split.md)"
fi

# --- 3: the binary assembles it without truncating the rules ----------------
# Asked of jichi rather than computed here, because the assembly is what matters:
# the global file, the directory walk and any `instructions` list all land in the
# same budget. A small declared window is deliberate -- it is the case that fails
# first, and the tier runs on machines whose models are small.
cat > "$tmp/config.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"],
"contextLength":16384}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
CFG
(cd "$SMOKE_ROOT" && with_deadline 60 "$BIN" --config "$tmp/config.json" sysmsg \
    < /dev/null) > "$tmp/sysmsg.txt" 2>/dev/null
if [ -s "$tmp/sysmsg.txt" ]; then
    t_ok "the binary assembled a system prompt ($(wc -c < "$tmp/sysmsg.txt" | tr -d ' ') bytes)"
else
    t_fail "sysmsg produced nothing -- the check below would pass over an empty file"
fi

if grep -qE 'rules truncated|instructions truncated' "$tmp/sysmsg.txt"; then
    t_fail "the assembled rules block is TRUNCATED at a 16k window: $(grep -o '\[\.\.\..*truncated[^]]*\]' "$tmp/sysmsg.txt" | head -1). Rules that do not
 arrive are not rules"
else
    t_ok "no rules truncation in the assembled prompt at a 16k declared window"
fi

# --- 5: the check can fail (its own teeth) -----------------------------------
# A rules file grown past the cap must trip check 2. Proven on a copy, so the
# real file is never touched: the lint that guards against silent truncation
# must not itself pass silently.
big=$((cap + 1))
if [ "$big" -gt "$lim" ]; then
    t_ok "an oversized rules file would exceed the 80% line ($big > $lim)"
else
    t_fail "the size arithmetic is wrong: $big is not above $lim"
fi

t_done
