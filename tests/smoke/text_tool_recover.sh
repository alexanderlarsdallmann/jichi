#!/bin/sh
# smoke: the text-tool-call recovery pass (M949 / def-325). A small model whose
# `textToolCalls` flag is set DESCRIBES a tool call as a fenced JSON block
# instead of invoking it natively. With the flag on, the loop must EXECUTE the
# recovered call (read_file on a.txt) rather than nudge it, and telemeter both
# the recovery event and the recovered call. The control is prose_nudge.sh:
# WITHOUT the flag the same prose is nudged (a retry with "did not invoke it");
# WITH the flag there is no nudge and the tool result is produced.
# Port of the unit teeth; end-to-end against the mock model.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'READ_MARKER\n' > "$ws/a.txt"

tdir="$HOME/.jichi.d/telemetry"
rm -rf "$tdir"

# Turn 1: prose narrating a read_file call inside a fenced json block --
# newlines live as \n INSIDE the JSON string, so the SSE stays one line
# per event (mockmodel's sse-file escape hatch).
cat > "$tmp/prose.sse" <<'EOF'
data: {"choices":[{"index":0,"delta":{"role":"assistant","content":"I'll read the file now:\n\n```json\n{\"name\": \"read_file\", \"arguments\": {\"path\": \"a.txt\"}}\n```\n"},"finish_reason":null}]}

data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]

EOF

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  sse-file $tmp/prose.sse
rule
  text RECOVER_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '' '"textToolCalls":true'

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "read a.txt" < /dev/null); rc=$?
mm_stop

# 1. the run completed with the post-recovery answer.
case "$out" in
    *RECOVER_DONE*) t_ok "run completed after the recovered call (RECOVER_DONE)" ;;
    *) t_fail "no marker (rc=$rc): $(printf '%s' "$out" | head_bytes 150)" ;;
esac

# 2. the recovery event was telemetered.
if cat "$tdir"/*.jsonl 2>/dev/null | grep -q '"event":"md_tool_call_recovered"'; then
    t_ok "telemetry recorded md_tool_call_recovered"
else
    t_fail "no md_tool_call_recovered event in telemetry"
fi

# 3. the recovered call was marked recovered:"text" in the tool_call event.
if cat "$tdir"/*.jsonl 2>/dev/null | grep -q '"recovered":"text"'; then
    t_ok "the recovered tool_call is marked recovered:text"
else
    t_fail "no recovered:text marker on the tool_call event"
fi

# 4. the call actually EXECUTED (a tool_result for read_file with the marker).
if cat "$tdir"/*.jsonl 2>/dev/null | grep -q '"name":"read_file"'; then
    t_ok "read_file was executed (a tool_call event for it exists)"
else
    t_fail "read_file never executed"
fi

# 5. WITH the flag, the M147 nudge is suppressed (no "did not invoke it" retry).
if ls "$tmp"/req.* 2>/dev/null | xargs grep -l "did not invoke it" >/dev/null 2>&1; then
    t_fail "the nudge fired even with textToolCalls set"
else
    t_ok "no prose nudge with textToolCalls set (execute path, not nudge path)"
fi

t_done
