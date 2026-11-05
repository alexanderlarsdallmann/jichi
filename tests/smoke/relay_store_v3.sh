#!/bin/sh
# smoke: session store v3 + the leg's surfaces (M954; proposal 8.5 / 7; design
# docs/internal/2026-10-14-topology-b-design.md B-4).
#
# THE RIG: one isolated HOME, two PROCESSes (topology A: a fresh session per
# leg, --handoff-in chains them). Every model call reports 92.5% of the
# window (E3), so each leg hands over at its first round boundary and writes a
# baton -- leg 1 hands to leg 2, leg 2 hands to a leg 3 that never starts (the
# driver does not run it: two legs is enough to prove the chain). Both legs
# run under --journal so the leg_start / leg_end events are readable.
#
# WHAT IT HOLDS, each against the effect:
#   1. leg 1's session file carries relay_id (its own id), leg 1, notes_path
#      (the baton it wrote) and NO prev_session (it was the first leg).
#   2. leg 2's session file carries relay_id (leg 1's id), leg 1, the SAME
#      notes_path shape, and prev_session = leg 1's session id -- the chain,
#      readable from the store alone.
#   3. leg 2's journal has a leg_start and a leg_end, paired -- the leg's
#      surface the operator (or the next process) reads to rebuild the chain.
#
# WITHOUT the v3 codec (the fields dropped from serialize) leg 1's file has
# none of them and checks 1 and 2 both fail; without the journal events,
# check 3 fails. The driver is red-first against the B-4a store commit.
# (The M951 lesson: do NOT set BIN here -- the smoke lib's BIN honours
# JC_SMOKE_BIN, the mutant sweep's seam.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3

tmp=$(smoke_tmp)
ws=$(smoke_tmp)
home=$(smoke_tmp)
chmod 700 "$home" 2>/dev/null || true
JQ="$SMOKE_TOOLS/jsonq"

printf 'a file for the relay store rig\n' > "$ws/a.txt"

cat > "$tmp/sv.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 185000 5
  tool read_file {"path":"a.txt"}
EOF

# --- leg 1: the first leg, no --handoff-in ----------------------------------
mm_start "$tmp/sv.mm" "$tmp/cap1"
write_config "$tmp/config1.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true' '"contextLength":200000'
HOME="$home"
export HOME
(cd "$ws" && with_deadline 120 "$BIN" \
    --config "$tmp/config1.json" --output json --max-tool-calls 50 \
    --journal "$tmp/leg1.jsonl" \
    -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/leg1.json" 2> "$tmp/leg1.err"); rc1=$?
mm_stop

id1=$("$JQ" '.session_id' "$tmp/leg1.json" 2>/dev/null)
stop1=$("$JQ" '.stop_reason' "$tmp/leg1.json" 2>/dev/null)
note1="$home/.jichi.d/relay/$id1/leg-01.md"

# 1: leg 1's session file is a v3 relay leg: relay_id = its own id, leg = 1,
# notes_path = the baton it wrote, and NO prev_session (it was the first leg).
if [ -n "$id1" ] && [ "$stop1" = "handover" ] && [ -f "$note1" ]; then
    s1="$home/.jichi.d/sessions/$id1.json"
    r1=$("$JQ" '.relay_id' "$s1" 2>/dev/null)
    l1=$("$JQ" '.leg' "$s1" 2>/dev/null)
    n1=$("$JQ" '.notes_path' "$s1" 2>/dev/null)
    p1=$("$JQ" '.prev_session' "$s1" 2>/dev/null)
    if [ "$r1" = "$id1" ] && [ "$l1" = "1" ] && [ "$n1" = "$note1" ] && \
       { [ -z "$p1" ] || [ "$p1" = "null" ]; }; then
        t_ok "leg 1's session is v3 (relay_id self, leg 1, notes_path, no prev)"
    else
        t_fail "leg 1 v3: relay_id='$r1' leg='$l1' notes='$n1' prev='$p1' \
(want $id1 / 1 / $note1 / unset)"
    fi
else
    t_fail "leg 1: rc=$rc1 stop='$stop1' note=$note1 (want handover + baton)"
fi

# --- leg 2: --handoff-in leg 1's baton --------------------------------------
mm_start "$tmp/sv.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true' '"contextLength":200000'
HOME="$home"
export HOME
(cd "$ws" && with_deadline 120 "$BIN" \
    --config "$tmp/config2.json" --output json --max-tool-calls 50 \
    --journal "$tmp/leg2.jsonl" --handoff-in "$note1" \
    -p "continue from the previous leg" \
    < /dev/null > "$tmp/leg2.json" 2> "$tmp/leg2.err"); rc2=$?
mm_stop

id2=$("$JQ" '.session_id' "$tmp/leg2.json" 2>/dev/null)
stop2=$("$JQ" '.stop_reason' "$tmp/leg2.json" 2>/dev/null)
s2="$home/.jichi.d/sessions/$id2.json"
r2=$("$JQ" '.relay_id' "$s2" 2>/dev/null)
p2=$("$JQ" '.prev_session' "$s2" 2>/dev/null)
l2=$("$JQ" '.leg' "$s2" 2>/dev/null)

# 2: leg 2's session carries the chain: relay_id = leg 2's OWN id (in topology
# A each leg is a fresh session, so its relay is the relay dir it writes to),
# prev_session = leg 1's id (the baton's relay dir, which is leg 1's own dir),
# leg = 1 (leg 2's own relay dir has just one baton). If the baton's relay dir
# is absent, the run refused the seed and the fields are unset -- prev fails.
if [ -n "$id2" ] && [ "$stop2" = "handover" ] && \
   [ "$r2" = "$id2" ] && [ "$p2" = "$id1" ] && [ "$l2" = "1" ]; then
    t_ok "leg 2's session chains to leg 1 (relay_id self, prev_session = leg 1)"
else
    t_fail "leg 2 chain: rc=$rc2 id2='$id2' relay_id='$r2' prev='$p2' leg='$l2' \
(want relay_id=$id2 prev=$id1 leg=1)"
fi

# 3: leg 2's journal has a leg_start and a leg_end, paired -- the leg's
# surface. leg_start is the run's first journal row; leg_end pairs with it
# by leg number.
if [ -f "$tmp/leg2.jsonl" ]; then
    ls1=$(grep -c '"leg_start"' "$tmp/leg2.jsonl" 2>/dev/null)
    le1=$(grep -c '"leg_end"' "$tmp/leg2.jsonl" 2>/dev/null)
    if [ "$ls1" -ge 1 ] && [ "$le1" -ge 1 ]; then
        t_ok "leg 2's journal has leg_start and leg_end (the paired leg surface)"
    else
        t_fail "leg 2 journal: leg_start=$ls1 leg_end=$le1 (want >=1 each)"
    fi
else
    t_fail "leg 2's journal file is missing: $tmp/leg2.jsonl"
fi

t_done
