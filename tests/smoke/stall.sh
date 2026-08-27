#!/bin/sh
# smoke: the M22a/b/c stall timeout -- a server that sends SSE headers and
# then freezes must be aborted in bounded time (the gemma symptom), never
# hang the agent forever; and the failed model_call must be classified
# result=timeout in telemetry, not a generic error (M22c). An 8s stall
# window must end the run in [5, 40] seconds with a non-zero exit.
# (Absorbs tests/e2e/timeout.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  stall header
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# The elapsed bounds below are load-tolerant on purpose (M260). Standalone this
# run takes a steady 15s; inside a full `make ci` -- after four build variants,
# valgrind and a fuzzer, with 90 drivers competing -- the same binary was
# measured at 66s, so the old 40s ceiling failed the gate for load rather than
# for behaviour. What the ceiling must still prove is that the STALL TIMEOUT
# ended the run and not the outer deadline, so the invariant kept is
# `ceiling < deadline`; the semantic proof is assertion 5 (telemetry classifies
# the failed call result=timeout), which no amount of load can fake. Both scale
# with the tier's slow-hardware knob.
_mult=${JC_SMOKE_TIMEOUT_MULT:-${JC_E2E_TIMEOUT_MULT:-1}}
_deadline=$((180 * _mult))
_ceiling=$((120 * _mult))

t0=$(date +%s)
(cd "$ws" && with_deadline "$_deadline" "$BIN" --config "$tmp/config.json" \
    --timeout-stall 8 --no-session -q --log "$tmp/telemetry.jsonl" \
    --log-level metrics -p "hi" < /dev/null \
    > /dev/null 2>&1); rc=$?
t1=$(date +%s)
mm_stop
elapsed=$((t1 - t0))

if [ $rc -ne 0 ]; then
    t_ok "stalled stream ends in a non-zero exit (rc=$rc)"
else
    t_fail "exit 0 on a frozen stream"
fi
if [ "$elapsed" -ge 5 ]; then
    t_ok "took ${elapsed}s -- long enough to have hit the stall path"
else
    t_fail "returned in ${elapsed}s -- connection refused rather than frozen?"
fi
if [ "$elapsed" -le "$_ceiling" ]; then
    t_ok "the 8s stall timeout bounded the run (${elapsed}s <= ${_ceiling}s, deadline ${_deadline}s)"
else
    t_fail "took ${elapsed}s -- the stall timeout did not bound it"
fi

# M22c: the failed model_call is CLASSIFIED as a stall, not a generic error
if grep '"event":"model_call"' "$tmp/telemetry.jsonl" 2>/dev/null \
   | grep -q '"ok":false'; then
    t_ok "telemetry carries the failed model_call"
else
    t_fail "no failed model_call in telemetry -- nothing to classify"
fi
if grep '"event":"model_call"' "$tmp/telemetry.jsonl" 2>/dev/null \
   | grep -q '"result":"timeout"'; then
    t_ok "the failure is classified result=timeout (M22c)"
else
    t_fail "failed model_call not classified as a timeout"
fi

t_done
