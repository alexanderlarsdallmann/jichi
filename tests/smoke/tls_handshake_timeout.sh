#!/bin/sh
# smoke: a TLS handshake that runs out of timeouts.connect is said to be one -- not a
# dead endpoint, not a stalled model (M877: the demos drive's N47 (d)).
#
# THE CASE (2026-10-02, the drive's runs 0094-0095). The HRZ gateway was slow, not
# down: measured from the drive's host, TCP connected in 0.002 s and the TLS handshake
# took 20.8 s. jichi's connect timeout is CURLOPT_CONNECTTIMEOUT, and libcurl counts
# the handshake against it, so the 20 s bound fired on a gateway that was listening.
# jichi read "TCP connected" as "connected" and said the stream had stalled -- a
# model's fault -- when the request had never been sent.
#
# THE STAND-IN. The mock model speaks plain HTTP and reads until the end of a
# request's headers. A TLS ClientHello has none, so an https:// apiBase pointed at it
# connects over TCP and then waits in the handshake until the connect bound fires.
#
#   1  the failure says TCP connected and the TLS handshake did not finish within
#      the bound, and that the request was never sent
#   2  it is not reported as a stalled stream, nor the run as out of time (the
#      footer read "ran out of wall-clock time" before M877)
#   3  the model_call telemetry carries the same diagnosis (its `transport`)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

cat > "$tmp/h.mm" <<'EOF'
wire openai
rule
  text NEVER_SENT
EOF
mm_start "$tmp/h.mm" "$tmp/cap"
cat > "$tmp/cfg.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"https://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "timeouts":{"connect":2}}
EOF
WS=$(smoke_tmp)
(cd "$WS" && with_deadline 60 "$BIN" --config "$tmp/cfg.json" --no-session \
    --log "$tmp/tel.jsonl" -p "hello" < /dev/null > "$tmp/out" 2> "$tmp/err")
mm_stop

if grep -q 'TCP connected, but the TLS handshake did not finish within 2s' "$tmp/err" &&
   grep -q 'the request was never sent' "$tmp/err" &&
   ! grep -q 'NEVER_SENT' "$tmp/out"; then
    t_ok "a handshake that ran out of the connect bound is named as one"
else
    t_fail "stderr: $(grep -i 'http' "$tmp/err" | head_bytes 400)"
fi
if [ -s "$tmp/err" ] && ! grep -qE 'stream stalled|stream went dead|model stalled' "$tmp/err" &&
   ! grep -q 'ran out of wall-clock time' "$tmp/err"; then
    t_ok "it is not reported as a stalled stream, nor the run as out of time"
else
    t_fail "stderr: $(grep -iE 'stall|dead|wall-clock' "$tmp/err" | head_bytes 300)"
fi
if grep '"event":"model_call"' "$tmp/tel.jsonl" 2>/dev/null |
       grep -q '"transport":"TCP connected, but the TLS handshake'; then
    t_ok "the model_call telemetry carries the same diagnosis"
else
    t_fail "telemetry: $(grep '"event":"model_call"' "$tmp/tel.jsonl" 2>/dev/null | head_bytes 300)"
fi

t_done
