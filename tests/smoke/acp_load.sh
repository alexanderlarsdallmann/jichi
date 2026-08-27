#!/bin/sh
# smoke: ACP session/load replays a persisted session (offline, no model).
# Seeds a session JSON in the private HOME, runs `--acp` with a fixed
# stdin (initialize, then session/load), and asserts loadSession is
# advertised and the stored transcript is replayed as session/update
# notifications before the load result. (Port of tests/e2e/acp_load.py,
# M210 -- the ACP exchange here is one-shot, so no bidirectional pipe is
# needed; interactive ACP cases stay in the Python tier until B3.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
SID="11111111-2222-3333-4444-555555555555"

sdir="$HOME/.jichi.d/sessions"
mkdir -p "$sdir"
cat > "$sdir/$SID.json" <<'EOF'
{"sessionId":"11111111-2222-3333-4444-555555555555",
 "title":"seeded session","workspaceDirectory":".","mode":"chat",
 "history":[
  {"role":"user","content":"hello there"},
  {"role":"assistant","content":"hi! let me look",
   "toolCalls":[{"id":"tc-1","name":"read_file",
                 "arguments":"{\"path\":\"notes.txt\"}"}]},
  {"role":"tool","toolCallId":"tc-1","content":"file body"},
  {"role":"assistant","content":"done"}]}
EOF

write_config "$tmp/config.json" 9

printf '%s\n%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1}}' \
  '{"jsonrpc":"2.0","id":2,"method":"session/load","params":{"sessionId":"'"$SID"'"}}' \
  | with_deadline 30 "$BIN" --config "$tmp/config.json" --acp \
  > "$tmp/out.jsonl" 2>/dev/null; rc=$?

if [ -s "$tmp/out.jsonl" ] && "$JQ" -l -q '.jsonrpc' "$tmp/out.jsonl"; then
    t_ok "every output line is JSON-RPC (rc=$rc)"
else
    t_fail "non-JSON output or empty (rc=$rc)"
fi

# the initialize result (id 1) advertises loadSession:true
init_caps=""
while IFS= read -r line; do
    id=$(printf '%s\n' "$line" | "$JQ" '.id' 2>/dev/null) || id=""
    if [ "$id" = "1" ]; then
        init_caps=$(printf '%s\n' "$line" \
            | "$JQ" '.result.agentCapabilities.loadSession' 2>/dev/null)
        break
    fi
done < "$tmp/out.jsonl"
if [ "$init_caps" = "true" ]; then
    t_ok "initialize advertises loadSession:true"
else
    t_fail "loadSession not advertised: '$init_caps'"
fi

# collect replay notification kinds
grep '"method":"session/update"' "$tmp/out.jsonl" > "$tmp/updates" || :
"$JQ" -l '.params.update.sessionUpdate' "$tmp/updates" \
    > "$tmp/kinds" 2>/dev/null || :
missing=""
for need in user_message_chunk agent_message_chunk tool_call \
            tool_call_update; do
    grep -q "^$need\$" "$tmp/kinds" || missing="$missing $need"
done
if [ -z "$missing" ]; then
    t_ok "replay covers user/agent chunks + tool_call(+update)"
else
    t_fail "missing replay updates:$missing"
fi

if grep -q "hello there" "$tmp/updates" \
   && grep -q "file body" "$tmp/updates" \
   && grep -q "tc-1" "$tmp/updates"; then
    t_ok "user text, tool result and tool-call id all round-trip"
else
    t_fail "replayed content incomplete"
fi

# session/load (id 2) returns a non-error result
load_ok=0
while IFS= read -r line; do
    id=$(printf '%s\n' "$line" | "$JQ" '.id' 2>/dev/null) || id=""
    if [ "$id" = "2" ]; then
        printf '%s\n' "$line" | "$JQ" -q '.result' 2>/dev/null && load_ok=1
        break
    fi
done < "$tmp/out.jsonl"
if [ $load_ok -eq 1 ]; then
    t_ok "session/load answers with a result, not an error"
else
    t_fail "no session/load result"
fi

t_done
