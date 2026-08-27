#!/bin/sh
# smoke: the prompt-cache prefix sentinel (M365). The M31 contract -- the
# system+tools prefix must stay byte-stable between calls or the provider
# cache silently stops matching -- had no runtime checker; the sentinel warns
# when the system prompt's hash changes on 3 CONSECUTIVE turns, because no
# legitimate cause (a mode switch, midnight, one memory write) fires every
# turn. Phase A drives real churn deterministically: four turns that each
# `remember` a new note refresh the memory section every turn, so the prompt
# hash moves on turns 2,3,4 and the sentinel fires exactly once. Phase B
# (plain turns, nothing varying) must stay silent -- the M310 absence pair.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

{
    echo "wire openai"
    i=1
    t=1
    while [ "$t" -le 4 ]; do
        echo "rule"
        echo "  count $i"
        echo "  tool remember {\"note\":\"churn note $t\"}"
        i=$((i + 1))
        echo "rule"
        echo "  count $i"
        echo "  text noted"
        i=$((i + 1))
        t=$((t + 1))
    done
    echo "rule"
    echo "  text ok"
} > "$tmp/replies.mm"

cat > "$tmp/run.pd" <<'EOF'
delay 1200
send "note one\r"
delay 2200
send "note two\r"
delay 2200
send "note three\r"
delay 2200
send "note four\r"
delay 2200
send "done now\r"
delay 2200
send "/exit\r"
waitexit 15
assertexit 0
EOF

mm_start "$tmp/replies.mm" "$tmp/capA"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/a.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto \
    --log "$tmp/telemA.jsonl" --log-level metrics \
    2> "$tmp/errA" > /dev/null); rca=$?
mm_stop

# --- 1: the churn session ran all five turns -----------------------------------
nmc=$(grep -c '"event":"model_call"' "$tmp/telemA.jsonl"); [ -n "$nmc" ] || nmc=0
if [ "$rca" -eq 0 ] && [ "$nmc" -ge 9 ]; then
    t_ok "phase A ran its five turns ($nmc model calls)"
else
    t_fail "phase A rc=$rca, model calls=$nmc"
fi

# --- 2: the sentinel fired ------------------------------------------------------
if grep -q '\[prefix\] the system prompt has changed' "$tmp/errA" "$tmp/a.log" 2>/dev/null; then
    t_ok "the [prefix] churn warning fired"
else
    t_fail "no churn warning after four per-turn memory writes"
fi

# --- 3: exactly one telemetry event (once per session, the M323 rule) -----------
nA=$(grep -c '"event":"prefix_churn"' "$tmp/telemA.jsonl"); [ -n "$nA" ] || nA=0
if [ "$nA" -eq 1 ]; then
    t_ok "exactly one prefix_churn telemetry event"
else
    t_fail "expected 1 prefix_churn event, found $nA"
fi

# --- Phase B: nothing varies per turn -> silence --------------------------------
{
    echo "wire openai"
    echo "rule"
    echo "  text ok"
} > "$tmp/replies2.mm"
cat > "$tmp/run2.pd" <<'EOF'
delay 1200
send "one\r"
delay 2000
send "two\r"
delay 2000
send "three\r"
delay 2000
send "four\r"
delay 2000
send "/exit\r"
waitexit 15
assertexit 0
EOF
mm_start "$tmp/replies2.mm" "$tmp/capB"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/b.log" "$tmp/run2.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto \
    --log "$tmp/telemB.jsonl" --log-level metrics \
    2> "$tmp/errB" > /dev/null); rcb=$?
mm_stop

nB=$(grep -c '"event":"prefix_churn"' "$tmp/telemB.jsonl"); [ -n "$nB" ] || nB=0
if [ "$rcb" -eq 0 ] && [ "$nB" -eq 0 ] \
   && ! grep -q '\[prefix\]' "$tmp/errB" "$tmp/b.log" 2>/dev/null; then
    t_ok "phase B (stable prompt): the sentinel stays silent"
else
    t_fail "phase B rc=$rcb, events=$nB -- a stable prompt was accused"
fi

# --- 5: memory actually moved in phase A (the churn was real, not assumed) ------
if [ "$(grep -c 'churn note' "$ws/.jichi/memory.md" 2>/dev/null)" -ge 3 ]; then
    t_ok "phase A's memory writes landed (the churn source is real)"
else
    t_fail "memory.md holds $(grep -c 'churn note' "$ws/.jichi/memory.md" 2>/dev/null) notes"
fi

t_done
