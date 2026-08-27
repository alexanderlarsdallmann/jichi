#!/bin/sh
# smoke: ACP session/cancel aborts an in-flight turn promptly (no hang).
# The mock model stalls (SSE headers then freeze) so the turn is stuck in
# the model stream; the ACP client sends session/prompt, waits, then a
# session/cancel notification. The server must answer the still-open
# prompt request with stopReason:"cancelled" well before the 120s stall
# timeout. Bidirectional stdio in sh: jichi --acp reads a FIFO we hold
# open read-write (so it never sees EOF), its stdout is polled for each
# id's result. (Port of tests/e2e/acp_cancel.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
fifo="$tmp/in.fifo"
outf="$tmp/out.jsonl"
mkfifo "$fifo"
: > "$outf"

# wait for a line in the growing $1 whose .id == $2 and has result/error,
# within $3 seconds; print it on success.
acp_wait_id() {
    _end=$(( $(date +%s) + $3 ))
    while [ "$(date +%s)" -le "$_end" ]; do
        while IFS= read -r _l; do
            [ "$(printf '%s' "$_l" | "$JQ" .id 2>/dev/null)" = "$2" ] || continue
            if printf '%s' "$_l" | "$JQ" -q .result 2>/dev/null \
               || printf '%s' "$_l" | "$JQ" -q .error 2>/dev/null; then
                printf '%s\n' "$_l"
                return 0
            fi
        done < "$1"
        sleep 1
    done
    return 1
}

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  stall header
EOF
mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,
 "maxRetries":0,"timeouts":{"stall":120}}
EOF

# jichi's read-open of the FIFO blocks until a writer; backgrounding it and
# then opening fd 3 read-write both unblocks it AND keeps stdin open.
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --acp \
    < "$fifo" > "$outf" 2>/dev/null) &
ACP_PID=$!
exec 3<>"$fifo"

send() { printf '%s\n' "$1" >&3; }

send '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{}}}'
if acp_wait_id "$outf" 1 15 > /dev/null; then
    t_ok "initialize answered"
else
    t_fail "no initialize result"
    exec 3>&-; kill "$ACP_PID" 2>/dev/null; mm_stop
    t_fail -; t_fail -; t_fail -; t_done
fi

send '{"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"'"$ws"'","mcpServers":[]}}'
ns=$(acp_wait_id "$outf" 2 15)
sid=$(printf '%s' "$ns" | "$JQ" .result.sessionId 2>/dev/null)
if [ -n "$sid" ] && [ "$sid" != "null" ]; then
    t_ok "session/new returned a sessionId"
else
    t_fail "no sessionId"
    exec 3>&-; kill "$ACP_PID" 2>/dev/null; mm_stop
    t_fail -; t_fail -; t_done
fi

send '{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"'"$sid"'","prompt":[{"type":"text","text":"stall please"}]}}'
sleep 2                              # let the turn reach the stalled stream
t0=$(date +%s)
send '{"jsonrpc":"2.0","method":"session/cancel","params":{"sessionId":"'"$sid"'"}}'
res=$(acp_wait_id "$outf" 3 20)
t1=$(date +%s)
exec 3>&-
kill "$ACP_PID" 2>/dev/null
wait "$ACP_PID" 2>/dev/null
mm_stop

if [ -n "$res" ]; then
    t_ok "session/prompt returned after cancel (no hang)"
else
    t_fail "session/prompt never returned -- hung"
fi
sr=$(printf '%s' "$res" | "$JQ" .result.stopReason 2>/dev/null)
elapsed=$((t1 - t0))
if [ "$sr" = "cancelled" ] && [ "$elapsed" -le 15 ]; then
    t_ok "stopReason=cancelled, promptly (${elapsed}s, not the 120s stall)"
else
    t_fail "stopReason=$sr elapsed=${elapsed}s"
fi

t_done
