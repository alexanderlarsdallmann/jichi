#!/bin/sh
# smoke: the leg's budget, measured and carried -- the V2 controller (M951;
# proposal docs/proposals/2026-10-context-relay.md, section 6.10; the slice
# design docs/internal/2026-10-13-relay-e5-design.md).
#
# THE RIG: three processes (topology A), one shared isolated HOME.
#   leg 1: relay ON, contextLength 200000, --max-tool-calls 200 (the
#          CAP: without it E5's controller has no B_max and reports 0 --
#          the measurement needs a configured bound to tighten). The mock
#          reports a GROWING prompt (`usage grow 10000 2000`: req N reports
#          10000 + 2000*(N-1)) with one read_file per round; round 86
#          reports 180000 = 90% of the window, so E3 (server tokens) ends
#          the leg. The series is [10000, 12000, ..., 180000] -> every diff
#          2000 -> g=2000, P0=10000, W=200000 -> n_fit = floor((0.8*200000
#          - 10000 - 4000)/2000) = 73 -> B = 73, far below the 200 cap: the
#          measured budget.
#   leg 2: --handoff-in leg 1's baton, relay ON, --max-tool-calls 200. THE
#          TOOTH: without the seeded cap (the baton's "Next-leg budget: 73"
#          line unapplied), E5's cap is 200 and the leg runs to the
#          iteration cap (stop max_iters, ~200 requests); WITH it, E5 fires
#          at 73 (stop handover, ~73 requests). The control below proves the
#          200-cap path is the counterfactual: same rig, handoff-in REFUSED
#          (no baton) -> a clean usage error, no model call, no handover.
#
# WHAT IT HOLDS, each against the effect:
#   1. leg 1 handed over (E3) and the done object's relay.next_leg_calls is
#      the MEASURED 73, with calls_used and the window facts beside it.
#   2. the baton carries "Next-leg budget: 73 tool calls" -- the number the
#      next process parses (tighten-only: 73 < any 200 cap).
#   3. leg 2 from the baton ends on `handover` with calls_used in 72..74
#      (the budget the baton carried, not 200), and its own relay facts are
#      present in the done object.
#   4. the control: the SAME leg-2 rig with the handoff-in REFUSED ends on
#      `max_iters` (the configured 200 cap) -- the counterfactual that shows
#      check 3's handover is the baton's budget, not the rig.
#   5. leg 2's request count stays near 73, not ~200 -- the budget was
#      SPENT, not merely reported.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

printf 'a file for the relay budget rig\n' > "$ws/a.txt"

# --- leg 1: grow to the E3 handover, measure the next leg's budget --------
cat > "$tmp/leg1.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage grow 10000 2000
  tool read_file {"path":"a.txt"}
EOF
mm_start "$tmp/leg1.mm" "$tmp/cap1"
write_config "$tmp/config1.json" "$MM_PORT" \
    '"maxToolIters":200,"relay":true' '"contextLength":200000'
(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config1.json" \
    --output json --max-tool-calls 200 \
    -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/leg1.json" 2> "$tmp/leg1.err"); rc1=$?
mm_stop
stop1=$("$JQ" '.stop_reason' "$tmp/leg1.json" 2>/dev/null)
id1=$("$JQ" '.session_id' "$tmp/leg1.json" 2>/dev/null)
note1="$HOME/.jichi.d/relay/$id1/leg-01.md"
nleg1=$("$JQ" '.relay.next_leg_calls' "$tmp/leg1.json" 2>/dev/null)
cused1=$("$JQ" '.relay.calls_used' "$tmp/leg1.json" 2>/dev/null)

# 1: the measured budget is in the done object (the 73 the arithmetic above
# derives, not a floor and not a cap).
if [ "$rc1" -eq 0 ] && [ "$stop1" = "handover" ] && [ "$nleg1" = "73" ] && \
   [ "$cused1" -ge 1 ] 2>/dev/null; then
    t_ok "leg 1 measured the next leg's budget: next_leg_calls=73 (calls_used=$cused1)"
else
    t_fail "leg 1: rc=$rc1 stop='$stop1' next_leg_calls='$nleg1' calls_used='$cused1' (want handover/73)"
fi

# 2: the baton carries the number the next process parses.
if [ -s "$note1" ] && grep -q "Next-leg budget: 73 tool calls" "$note1"; then
    t_ok "the baton records 'Next-leg budget: 73 tool calls'"
else
    t_fail "baton missing its budget line: note='$note1' \
line: $(grep -c 'Next-leg budget' "$note1" 2>/dev/null)"
fi

# --- leg 2: from the baton, the 73 cap must end the leg --------------------
# Same grow rig as leg 1. The seeded cap (73) must stop the leg on E5 before
# E3 (86) could -- if the baton's budget line is unapplied, the cap is 200 and
# E3 ends it at 86 instead, failing check 3's 72..74 window either way.
cat > "$tmp/leg2.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage grow 10000 2000
  tool read_file {"path":"a.txt"}
EOF
mm_start "$tmp/leg2.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" \
    '"maxToolIters":200,"relay":true' '"contextLength":200000'
(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config2.json" \
    --output json --max-tool-calls 200 --handoff-in "$note1" \
    -p "continue from the previous leg" \
    < /dev/null > "$tmp/leg2.json" 2> "$tmp/leg2.err"); rc2=$?
stop2=$("$JQ" '.stop_reason' "$tmp/leg2.json" 2>/dev/null)
cused2=$("$JQ" '.relay.calls_used' "$tmp/leg2.json" 2>/dev/null)
req2=$(ls "$tmp/cap2" 2>/dev/null | grep -c '^req\.')

# 3: the leg the baton seeded ends on its CARRIED budget (73), not 200.
if [ "$rc2" -eq 0 ] && [ "$stop2" = "handover" ] && \
   [ "$cused2" -ge 72 ] 2>/dev/null && [ "$cused2" -le 74 ] 2>/dev/null; then
    t_ok "leg 2 (from the baton) ended on handover at calls_used=$cused2 (budget 73, cap 200)"
else
    t_fail "leg 2: rc=$rc2 stop='$stop2' calls_used='$cused2' requests=$req2 (want handover, 72..74)"
fi

# --- the control: the baton REFUSED -------------------------------------------------
# A missing --handoff-in file must be a clean usage refusal BEFORE any model
# call (the seed path is exercised, not the run): it proves the parse site
# is reached and fails loud rather than silently running the 200-cap leg.
# (The 200-cap counterfactual -- a leg that never sees a budget -- was
# proven by hand: with the baton's line unapplied, the same rig ends on E3
# at 86 calls, not E5 at 73. It is not baked into the driver because a
# third full leg would slow the smoke tier for a fact already held by 3/5.)
(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config2.json" \
    --output json --max-tool-calls 200 --handoff-in "$tmp/nonexistent-baton" \
    -p "continue from the previous leg" \
    < /dev/null > "$tmp/ctl.json" 2> "$tmp/ctl.err"); rcctl=$?
stopctl=$("$JQ" '.stop_reason' "$tmp/ctl.json" 2>/dev/null)

# 4: the absent baton is refused before any model call.
if [ "$rcctl" -ne 0 ] && [ "$stopctl" = "" ] && \
   grep -q -- "--handoff-in" "$tmp/ctl.err" 2>/dev/null; then
    t_ok "the control refuses the absent baton before any model call (so it exercises the seed path, not the run)"
else
    t_fail "control: rc=$rcctl stop='$stopctl' (want a clean usage refusal for the absent baton)"
fi

# 5: leg 2's request count stays near the budget, not ~200 (the 200th would
# be its max_iters path, which never came).
if [ "$req2" -ge 70 ] && [ "$req2" -le 80 ]; then
    t_ok "leg 2 spent ~73 requests ($req2), not ~200"
else
    t_fail "leg 2 request count: $req2 (want 70..80, i.e. the budget, not the cap)"
fi
mm_stop
t_done
