#!/bin/sh
# smoke: the ambient budget panel (M431f), and its DEFAULT-OFF posture.
#
# WHY IT IS A FLAG. M347 rings the fuel gauge ONCE, at the first ~80% crossing, and
# its DECISIONS row explicitly rejected "a reminder per round or a countdown" -- on
# M323's evidence (1,038 warnings from one unthrottled condition) and the principle
# that a nag a model learns to skip is worse than one clear ask. That objection is a
# judgement about model behaviour, not a measurement. So this ships OFF, to be
# measured against a real run rather than to overrule a shipped decision.
#
# What is genuinely new since M347 is the RATE. Caps at takeoff plus one reading at
# 80% leave AUTONOMY.md's own "76% to 100%" band empty, and tokens-per-call --
# measured at 25-42k and RISING within a run -- is what turns "300k left" into "about
# eight calls left". That number is the reason the feature exists -- and it is checked
# in the UNIT tier, for the reason stated below.
#
# The default-off half (checks 1-2) is the load-bearing one: a feature that quietly
# turned itself on would be exactly the overruling this design refuses.
#
# WHAT THIS TIER COULD NOT CHECK UNTIL M441: the RATE and the projection. mockmodel
# emitted a `usage` block on a `text` reply and none on a `tool` round, so tokens_used
# stayed 0 through every tool round of a mocked run and there was no rate to compute --
# the panel correctly OMITTED the term rather than printing a guessed zero, and this
# driver had to say so and lean on test_env_panel's five unit assertions instead.
#
# M441 taught the mock to report usage on the tool path (the shape a real provider
# sends: a final chunk with an empty `choices`). Check 6 now covers the rate end to
# end. The unit assertions stay: they cover the omission and the projection arithmetic,
# which a mock with fixed 20/5 usage cannot exercise interestingly.
#
# The panel is told apart from M347's one-shot notice and M355's flight plan by its
# SLASH form -- the panel writes "5/8 tool calls" where those two write "5 of 8". An
# earlier cut of this driver grepped for "[envelope]" and passed on the flight plan's
# text while the panel was in fact absent.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Six rounds of tool calls, so the default cadence (every 5th) has room to fire once
# and the "not per round" property is observable rather than assumed.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"1"}
rule
  count 2
  tool write_file {"path":"b.txt","content":"2"}
rule
  count 3
  tool write_file {"path":"c.txt","content":"3"}
rule
  count 4
  tool write_file {"path":"d.txt","content":"4"}
rule
  count 5
  tool write_file {"path":"e.txt","content":"5"}
rule
  count 6
  tool write_file {"path":"f.txt","content":"6"}
rule
  text PANEL_DONE
EOF

# --- OFF by default -----------------------------------------------------------
mm_start "$tmp/replies.mm" "$tmp/off" 9
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 400k --max-tool-calls 8 \
    -p "write the files" < /dev/null) > /dev/null 2>&1; rc=$?
mm_stop

if [ "$rc" -eq 0 ]; then
    t_ok "the control run completed (rc=0)"
else
    t_fail "control run rc=$rc"
fi

# The SAME marker check 3 uses. An earlier cut grepped for "tokens/call", which this
# rig never emits (no chat usage, so no rate) -- so it would have passed with the panel
# fully switched on. A default-off assertion that cannot fail is worse than none.
if ! grep -lE "[0-9]+/8 tool calls" "$tmp"/off/req.* >/dev/null 2>&1; then
    t_ok "DEFAULT OFF: no panel in any of $(ls "$tmp"/off/req.* 2>/dev/null | wc -l) requests"
else
    t_fail "the panel appeared without being asked for -- it must not turn itself on"
fi

# --- ON with --budget-panel ---------------------------------------------------
mm_start "$tmp/replies.mm" "$tmp/on" 9
sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config2.json"
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    -q --no-session --auto --budget-panel \
    --budget-tokens 400k --max-tool-calls 8 \
    -p "write the files" < /dev/null) > /dev/null 2>&1; rc2=$?
mm_stop

if grep -lE "[0-9]+/8 tool calls" "$tmp"/on/req.* >/dev/null 2>&1; then
    t_ok "--budget-panel puts a reading in front of the model"
else
    t_fail "no panel with the flag on (rc2=$rc2), $(ls "$tmp"/on/req.* 2>/dev/null | wc -l) requests"
fi

# --- ARMED budgets only (the M347/M355 rule) ----------------------------------
# --max-reads was NOT given, so a "reads" term would be a limit that does not exist
# being reported as a fact about this run. Both armed terms must be present.
panel=$(grep -hoE "envelope\] [0-9]+/400000 tokens[^\"]{0,60}" "$tmp"/on/req.* 2>/dev/null | head -1)
if [ -n "$panel" ] && printf '%s' "$panel" | grep -q "tool calls" &&
   ! printf '%s' "$panel" | grep -q "reads"; then
    t_ok "the reading names the two armed budgets and no unarmed one"
else
    t_fail "armed-only shape wrong: [$panel]"
fi

# --- not a per-round nag -------------------------------------------------------
# The cadence is every 5th tool call plus each quintile of the token budget. Over
# ~6 rounds under a 400k budget that is a small number; one per round would be 6+.
# Counted in the LAST request, which carries the whole history.
last=$(ls "$tmp"/on/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)
n=$(grep -oE "[0-9]+/8 tool calls" "$last" 2>/dev/null | wc -l | tr -d ' ')
if [ -n "$n" ] && [ "$n" -ge 1 ] && [ "$n" -le 3 ]; then
    t_ok "throttled: $n reading(s) over 6 rounds, not one per round"
else
    t_fail "expected 1-3 readings in the final history, found '$n' -- the cadence is not throttling"
fi

# --- 6: the RATE is computed and shown (M441 made this checkable) --------------
# The term the panel exists for. A gauge that reads "412000/1500000 (27%)" tells a
# model how much is left; the RATE is what tells it whether what is left is enough --
# which is the difference between pacing a run and discovering at 80% that it cannot
# finish. Until M441 the mock reported no usage on a tool round, so tokens_used was 0,
# there was no rate, and the panel honestly omitted the term.
if grep -lE "tokens/call" "$tmp"/on/req.* >/dev/null 2>&1 &&
   grep -lE "calls left at this rate" "$tmp"/on/req.* >/dev/null 2>&1; then
    t_ok "the reading carries the spend rate and the projection"
else
    t_fail "no rate in the panel: [$(grep -hoE "envelope\] [0-9]+/400000 tokens[^\"]{0,110}" "$tmp"/on/req.* 2>/dev/null | head -1)]"
fi

t_done
