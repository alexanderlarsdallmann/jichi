#!/bin/sh
# M802: every on-disk sink that stores tool output must scrub a registered secret
# that a TOOL prints. The redactor scrubs registered values only (the model API
# keys, src/main.c jc_redact_register), so the canary is the mock model's apiKey,
# and a shell command echoes it into a tool result; then grep every sink. Before
# M802 the saved session and the --output jsonl stream carried the canary verbatim
# (checks 6-7 red) while telemetry already redacted; both now redact. This began as
# the drive experiment E-R3. (A secret split across two model-text deltas is a
# narrower gap, closed separately with its own driver.) Set E1_RES to keep the
# sink artifacts for diagnosis.
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
CANARY=CANARYSECRETzq7x0123456789
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo $CANARY"}
rule
  count 2
  text DONE
rule
  count 3
  tool run_terminal_command {"command":"echo $CANARY"}
rule
  count 4
  text DONE
rule
  status 401
  body {"error":{"message":"Incorrect API key provided: $CANARY","type":"invalid_request_error"}}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q --auto \
    --log "$tmp/telemetry.jsonl" --log-level full --journal "$tmp/journal.jsonl" \
    -p "print it" < /dev/null > "$tmp/out.txt" 2>&1 )
# Second invocation, the drive's output mode: the jsonl stream carries tool previews.
( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session --auto \
    --output jsonl -p "print it" < /dev/null > "$tmp/stream.jsonl" 2>/dev/null )
# Third invocation for C-03 red-first: provider error with canary in the error body.
# The config's apiKey is $CANARY and maxRetries 0; the mock rule returns status 401
# with a body containing the canary, which should be jc_redact_register'd.
cat > "$tmp/config_c03.json" <<EOF2
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF2
( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config_c03.json" -q --auto \
    --log "$tmp/telemetry_c03.jsonl" --log-level metrics -p "hi" < /dev/null > "$tmp/c03_out.txt" 2>&1 )
mm_stop
sess=$(find "$HOME" -path '*sessions*' -name '*.json' 2>/dev/null | head -1)
count() { if [ -n "$1" ] && [ -f "$1" ]; then grep -c "$CANARY" "$1"; else echo absent; fi; }
s=$(count "$sess"); t=$(count "$tmp/telemetry.jsonl"); j=$(count "$tmp/journal.jsonl"); o=$(count "$tmp/out.txt")
st=$(count "$tmp/stream.jsonl"); stp=$(grep -c 'tool_result' "$tmp/stream.jsonl" 2>/dev/null)
echo "# canary occurrences: session=$s telemetry=$t journal=$j stdout=$o stream(jsonl)=$st (stream tool_result events: $stp)"
echo "# session file: ${sess:-none found}"
# The redactor writes "***" in place of a secret (src/util/jc_log.c): a telemetry
# row with the echo command AND "***" proves redaction happened rather than the
# output simply not being logged.
red=$(grep 'run_terminal_command' "$tmp/telemetry.jsonl" 2>/dev/null | grep -c 'echo \*\*\*')
echo "# telemetry rows where the echo command reads 'echo ***': $red"
if [ -n "${E1_RES:-}" ]; then
    mkdir -p "$E1_RES"
    cp -p "$tmp/telemetry.jsonl" "$tmp/journal.jsonl" "$tmp/out.txt" "$tmp/stream.jsonl" "$E1_RES/" 2>/dev/null
    [ -n "$sess" ] && cp -p "$sess" "$E1_RES/session.json"
    cp -p "$tmp/telemetry_c03.jsonl" "$E1_RES/" 2>/dev/null
fi
[ -n "$sess" ] && t_ok "a session was saved (the instrument has something to read)" || t_fail "no session file found under the smoke HOME"
grep -q 'run_terminal_command' "$tmp/telemetry.jsonl" 2>/dev/null && t_ok "telemetry recorded the tool call" || t_fail "telemetry has no tool call row"
[ "$t" = 0 ] && t_ok "telemetry: the canary does not appear" || t_fail "telemetry: the canary appears $t time(s)"
[ "$red" -ge 1 ] && t_ok "telemetry: the tool row shows the redaction marker (redacted, not merely unlogged)" || t_fail "telemetry: no row with the redaction marker -- the 0 above may mean unlogged, not redacted"
[ "$stp" -ge 1 ] && t_ok "stream: the tool result was emitted (the instrument has something to read)" || t_fail "stream: no tool_result event"
[ "$st" = 0 ] && t_ok "stream (--output jsonl): the canary does not appear" || t_fail "stream (--output jsonl): the canary appears $st time(s)"
[ "$s" = 0 ] && t_ok "session: the canary is redacted" || t_fail "session: the canary appears $s time(s) -- sessions are not redacted"
# C-03: the model_call failure event should have the canary scrubbed from transport, stream_error, and error_body
c03_t=$(count "$tmp/telemetry_c03.jsonl")
if grep -q '"status":401' "$tmp/telemetry_c03.jsonl" 2>/dev/null; then
    c03_has_status=$(grep -c '"status":401' "$tmp/telemetry_c03.jsonl" 2>/dev/null)
else
    c03_has_status=0
fi
if grep -q '"event":"model_call"' "$tmp/telemetry_c03.jsonl" 2>/dev/null; then
    c03_has_model_call=$(grep -c '"event":"model_call"' "$tmp/telemetry_c03.jsonl" 2>/dev/null)
else
    c03_has_model_call=0
fi
echo "# C-03 canary occurrences in telemetry_c03: $c03_t; status 401 events: $c03_has_status; model_call events: $c03_has_model_call"
[ "$c03_has_status" -ge 1 ] && t_ok "C-03: telemetry has model_call with status 401" || t_fail "C-03: telemetry missing model_call with status 401"
[ "$c03_t" = 0 ] && t_ok "C-03: telemetry: the canary does not appear in error fields" || t_fail "C-03: telemetry: the canary appears $c03_t time(s) in error fields"
t_done
