#!/bin/sh
# smoke: the journal names the stop a run really had (M732).
#
# THE DEFECT, found by reading one run of the M731 drive. bonsai 16 was stopped by
# the drive's wall-clock limit (SIGINT). Its output stream said
# `"aborted":true, "stop_reason":"interrupted"`; its journal's `end` event said
# `"stop_reason":"done"`. The end event passed JC_OK to jc_agent_stop_reason, on a
# comment's assumption that "reaching this line means the loop returned normally --
# a transport error never gets here". An interrupt reaches it, and so -- check 3
# asks -- does a transport error. The journal is the record the measurement
# scripts read (tests/measure/*.py), so interrupted runs were joining the `done`
# population with nothing to say so.
#
# THE CHECKS. One run that answers (the denominator: `done` was always right), one
# interrupted mid-call, one whose only model call fails with HTTP 500, and the
# interrupted run's two records compared -- a run must not be `interrupted` in its
# stream and `done` in its journal.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

end_stop() {   # JOURNAL -> the end event's stop_reason
    grep '"event":"end"' "$1" 2>/dev/null | tail -1 | "$JQ" .stop_reason 2>/dev/null
}

# --- 1: a run that answers journals `done` (the case that was always right) ---
cat > "$tmp/answer.mm" <<'EOF'
wire openai
rule
  text FINE
EOF
mm_start "$tmp/answer.mm" "$tmp/cap1"
write_config "$tmp/config1.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config1.json" -q --no-session \
    --auto --journal "$tmp/done.jsonl" -p "answer" < /dev/null) > /dev/null 2>&1
mm_stop
if [ "$(end_stop "$tmp/done.jsonl")" = "done" ]; then
    t_ok "an answering run journals stop_reason done"
else
    t_fail "the denominator is broken: $(grep '"event":"end"' "$tmp/done.jsonl" 2>/dev/null | head_bytes 200)"
fi

# --- 2: SIGINT mid-call journals `interrupted` ---------------------------------
# The mock stalls mid-stream and the stall window is 60 s, so the signal lands in
# an in-flight call (the tests/smoke/signals.sh arrangement).
cat > "$tmp/stall.mm" <<'EOF'
wire openai
rule
  stall mid
EOF
mm_start "$tmp/stall.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && exec "$BIN" --config "$tmp/config2.json" -q --no-session --auto \
    --journal "$tmp/int.jsonl" --output jsonl --timeout-stall 60 -p "hang" \
    < /dev/null > "$tmp/int.stream" 2>/dev/null) &
pid=$!
sleep 3
kill -INT "$pid" 2>/dev/null
wait "$pid"; rc=$?
mm_stop
if [ "$rc" -eq 130 ] && [ "$(end_stop "$tmp/int.jsonl")" = "interrupted" ]; then
    t_ok "an interrupted run exits 130 and journals stop_reason interrupted"
else
    t_fail "rc=$rc, journal end: $(grep '"event":"end"' "$tmp/int.jsonl" 2>/dev/null | head_bytes 200)"
fi

# --- 3: a failed model call journals `error` -----------------------------------
cat > "$tmp/fail.mm" <<'EOF'
wire openai
rule
  status 500
  body {"error":{"message":"the model server fell over"}}
EOF
mm_start "$tmp/fail.mm" "$tmp/cap3"
write_config "$tmp/config3.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" -q --no-session \
    --auto --journal "$tmp/err.jsonl" -p "fail" < /dev/null) > /dev/null 2>&1
mm_stop
if [ "$(end_stop "$tmp/err.jsonl")" = "error" ]; then
    t_ok "a run whose model call failed journals stop_reason error"
else
    t_fail "a failed model call journals: $(grep '"event":"end"' "$tmp/err.jsonl" 2>/dev/null | head_bytes 200)"
fi

# --- 4: the two records of one run agree -----------------------------------------
st_stream=$(grep '"type":"done"' "$tmp/int.stream" 2>/dev/null | tail -1 | "$JQ" .stop_reason 2>/dev/null)
if [ -n "$st_stream" ] && [ "$st_stream" = "$(end_stop "$tmp/int.jsonl")" ]; then
    t_ok "the interrupted run's stream and journal name the same stop ($st_stream)"
else
    t_fail "stream says '${st_stream:-nothing}', journal says '$(end_stop "$tmp/int.jsonl")'"
fi

t_done
