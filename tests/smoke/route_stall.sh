#!/bin/sh
# smoke: stall-aware routing escalation (M23a). Two mock servers: the FAST
# tier accepts and freezes after the SSE headers (the gemma symptom); the
# STRONG tier answers. The run must recover -- exit 0 with the strong
# model's answer -- and telemetry must carry a route event with
# reason=stall.
# (Port of tests/e2e/route_stall.py, M212.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$tmp/capf" "$tmp/caps"

cat > "$tmp/fast.mm" <<'EOF'
wire openai
rule
  stall header
EOF
cat > "$tmp/strong.mm" <<'EOF'
wire openai
rule
  text ROUTED_OK
EOF

mm_start "$tmp/fast.mm" "$tmp/capf"
F_PORT=$MM_PORT; F_PID=$MM_PID
mm_start "$tmp/strong.mm" "$tmp/caps"

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"fast","provider":"openai","model":"fast-model",
   "apiBase":"http://127.0.0.1:$F_PORT/v1","apiKey":"x"},
  {"name":"strong","provider":"openai","model":"strong-model",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "routing":{"enabled":true,"fast":"fast","strong":"strong"},
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      -q --no-session --timeout-stall 6 --log "$tmp/telemetry.jsonl" \
      --log-level metrics -p "say the marker" < /dev/null); rc=$?
kill "$F_PID" 2>/dev/null
wait "$F_PID" 2>/dev/null
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "the run recovered (exit 0 after escalation)"
else
    t_fail "rc=$rc -- stall escalation did not recover the turn"
fi
case "$out" in
    *ROUTED_OK*) t_ok "the strong model's answer reached stdout" ;;
    *) t_fail "strong answer missing: $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if grep '"event":"route"' "$tmp/telemetry.jsonl" 2>/dev/null \
   | grep -q '"reason":"stall"'; then
    t_ok "telemetry carries a route event with reason=stall"
else
    t_fail "no route/stall event in telemetry"
fi

t_done
