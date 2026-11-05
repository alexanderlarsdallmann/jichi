#!/bin/sh
# smoke: a dead-stream fence firing on a REUSED (keep-alive) connection is
# classified as a stream death, not a connect timeout (M1062, SV:0813).
#
# THE FINDING. Run 0077's telemetry row said fail_class: timeout-connect and
# "the request was never sent" for a model call that had REUSED a live
# keep-alive connection and died mid-stream (the VPN dropped; nothing arrived
# and the deadStream fence fired). The classifier's "was it connected?" answer
# came from jc_sockwatch_tcp_up(&g_sockwatch, connect_time), and BOTH inputs go
# quiet on a reused transfer: libcurl leaves CURLINFO_CONNECT_TIME at 0 for a
# reused connection (the M880 note), and the per-transfer sockwatch (reset by
# apply_common, M882) counts only sockets THIS transfer opened. tcp_up == 0 for
# a socket that was established and had carried earlier calls.
#
# REPRODUCTION (RED FIRST). mockmodel, wire openai, --keep-alive (MM_KEEPALIVE=1
# makes the connection REUSED):
#   - request 1 (the lead): a tool call whose result triggers request 2.
#   - request 2 (the tool result, distinctive word PROMPTWORD): stall none --
#     the mock reads the request, captures it, sends ZERO bytes (no headers, no
#     body), and holds the socket until the peer aborts. That is the
#     VPN-drop / dead-stream signature: status 0, nothing arrives.
# conns must show requests=2 (TWO model calls, ONE connection) -- the
# reused-connection premise. A small timeouts.deadStream (5) fires the fence at
# ~5 s with the stall cap off (fence_only), so the failure must name
# timeouts.deadStream, and the class must be the stream-death class (timeout-stall),
# NOT timeout-connect.
#
# RED on the unfixed tree: the failed row's fail_class IS timeout-connect and the
# message says "could not connect within 20s (timeouts.connect) -- the request
# was never sent". GREEN on the fixed tree: fail_class timeout-stall and the
# message names timeouts.deadStream.

. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# count scopes the 1-based request number (mm_select), so request 1 is a tool
# call (whose result triggers request 2) and request 2 -- on the SAME kept
# connection -- is the dead stream. Both rules need `count` (a bare action rule
# is a catch-all that would match every request and loop forever).
cat > "$tmp/dead.mm" <<'MM'
wire openai
rule
  count 1
  tool read_file {"path":"subject.txt"}
rule
  count 2
  stall none
MM

MM_KEEPALIVE=1 mm_start "$tmp/dead.mm" "$tmp/capa"

cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":true,"maxRetries":0,
"timeouts":{"connect":20,"stall":0,"request":0,"deadStream":5}}
EOF

# The prompt carries the distinctive word that request 2 (the tool result) will
# echo back, so the stall targets exactly the REUSED second request.
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" --no-session \
    --log "$tmp/telemetry.jsonl" --log-level metrics \
    -q --auto -p "PROMPTWORD" < /dev/null > "$tmp/out.txt" 2>&1) || true

# The mock flushes capa/conns when jichi closes the pooled connection (jichi
# exit). Wait for it (poll, ~3 s) BEFORE mm_stop kills the mock, so the
# precondition reads a real conns, not a lost race.
_c=0
while [ ! -f "$tmp/capa/conns" ] && [ "$_c" -lt 3 ]; do
    kill -0 "$MM_PID" 2>/dev/null || break
    sleep 1
    _c=$((_c + 1))
done
mm_stop

# 1: THE PRECONDITION -- both model calls rode ONE connection (it was REUSED).
# If this is 1, or two separate requests=1 lines, the finding is not reproduced:
# the connection was never reused and the test is vacuous. Say so; never loosen.
if [ -f "$tmp/capa/conns" ]; then
    reqs=$("$G" -o 'requests=[0-9]*' "$tmp/capa/conns" | head -n 1 | sed 's/requests=//')
    if [ -n "$reqs" ] && [ "$reqs" -ge 2 ]; then
        t_ok "precondition: two model calls rode ONE connection (requests=$reqs)"
    else
        t_fail "precondition: the connection was NOT reused (conns requests=$reqs) -- the M1062 shape is not reproduced; check the mm script, never this assertion"
    fi
else
    t_fail "precondition: no capa/conns file -- the provider connection was not pooled, instrument vacuous"
fi

# The failed model_call row (ok=false). grep isolates it; jsonq -l reads the
# one remaining document per line.
$G '"event":"model_call"' "$tmp/telemetry.jsonl" 2>/dev/null \
    | "$JQ" -l '.ok' 2>/dev/null > "$tmp/oks" || true
$G '"event":"model_call"' "$tmp/telemetry.jsonl" 2>/dev/null \
    > "$tmp/calls" || true
nfail=0
while read -r o; do
    [ "$o" = "false" ] && nfail=$((nfail + 1))
done < "$tmp/oks"
# 2: a failed model_call exists (the dead stream was recorded as a failure).
if [ "$nfail" -ge 1 ]; then
    t_ok "a failed model_call is in telemetry (ok=false, $nfail)"
else
    t_fail "no failed model_call in telemetry (nfail=$nfail)"
fi

# The failed row is the LAST failed model_call (the tool-result call on the
# reused connection is the one that died; request 1 succeeded).
failed_row=$("$G" '"ok":false' "$tmp/calls" | tail -n 1)
[ -n "$failed_row" ] || failed_row=""

# 3: THE FIX -- the failed row's fail_class is the stream-death class, NOT
# timeout-connect. Red-first on the unfixed tree it IS timeout-connect.
fc=$(printf '%s' "$failed_row" | "$JQ" .fail_class 2>/dev/null)
if [ "$fc" = "timeout-stall" ]; then
    t_ok "failed reused-stream call is timeout-stall (not timeout-connect)"
elif [ "$fc" = "timeout-connect" ]; then
    t_fail "failed reused-stream call is timeout-connect (RED: the connection was reused, the request WAS sent; M1062 not fixed)"
else
    t_fail "failed reused-stream call is fail_class=$fc (expected timeout-stall)"
fi

# 4: that row's status is 0 (the dead-stream signature: nothing arrived).
st=$(printf '%s' "$failed_row" | "$JQ" .status 2>/dev/null)
if [ "$st" = "0" ]; then
    t_ok "the failed row has status 0 (dead stream, nothing arrived)"
else
    t_fail "the failed row has status=$st (expected 0)"
fi

# 5: the message names timeouts.deadStream (the fence that fired), not the
# connect knob. Red-first on the unfixed tree it says
# "could not connect within 20s (timeouts.connect) -- the request was never sent".
tr=$(printf '%s' "$failed_row" | "$JQ" -l .transport 2>/dev/null)
case "$tr" in
    *deadStream*|*went*dead*)
        t_ok "the message names timeouts.deadStream (the fence that fired)" ;;
    *)
        t_fail "the message does not name timeouts.deadStream (got: $tr)" ;;
esac

# 6: the message must NOT assert the request was never sent (the M1062 lie).
case "$tr" in
    *"never sent"*)
        t_fail "the message says 'the request was never sent' (RED: it was sent; M1062 not fixed)" ;;
    *)
        t_ok "the message does not claim the request was never sent" ;;
esac

t_done
