#!/bin/sh
# smoke: a delegate's tool calls spend the RUN's budget (M431).
#
# The envelope metered tokens at every depth and enforced only at depth 0
# (jc_env_over_budget sat behind env_active(), which requires agent_depth == 0),
# so a subagent's tool calls were free: M422 measured `--max-tool-calls 3`
# letting a run execute 9. env_budget_applies() fixed it.
#
# Shape: the top-level model spawns one subagent whose own model never finishes
# (every reply carries text AND another tool call), so it would run to
# maxSubagentIters=8 if nothing stopped it. The run is given --max-tool-calls 3.
#
# Two-sided by construction. BEFORE the fix the subagent's 8 calls cost the run
# nothing, the parent's own count reached 1, the pool never tripped, and the run
# finished TOPDONE with rc=0 and no `budget` journal event -- so all three
# assertions below fail. AFTER it, the third call trips the cap inside the
# subagent, which stops and hands its partial answer back, and the top level
# settles the run as budget_exhausted.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# the subagent's endless reply: assistant text + another read_file call
cat > "$tmp/progress.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"PROGRESS"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"read_file","arguments":"{\"path\": \"README\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "TOP run"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP run"
  tool spawn_subagent {"task":"SUBTASK: investigate"}
rule
  sse-file $tmp/progress.sse
EOF

echo "hello" > "$ws/README"

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"maxSubagentIters":8'

(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --max-tool-calls 3 --journal "$tmp/j.jsonl" \
    -p "TOP run a subagent" \
    < /dev/null > "$tmp/out.txt" 2>&1); rc=$?
mm_stop

# --- 1: the run stopped on budget, it did not complete -------------------------
# Before the fix this was rc=0 with TOPDONE on stdout.
if [ $rc -eq 1 ]; then
    t_ok "the run exited 1 (budget exhausted), not 0"
else
    t_fail "rc=$rc (expected 1); stdout: $(head_bytes 200 "$tmp/out.txt" 2>/dev/null)"
fi

# --- 2: the envelope recorded a budget stop -----------------------------------
if grep -q '"event":"budget"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "journal carries the budget event"
else
    t_fail "no budget journal event: $(head -5 "$tmp/j.jsonl" 2>/dev/null)"
fi

# --- 3: the calls that spent it were the DELEGATE's ---------------------------
# The parent itself made exactly one call (spawn_subagent), so a tool_calls
# figure of 3 or more on `end` can only include the subagent's.
grep '"event":"end"' "$tmp/j.jsonl" 2>/dev/null | tail -1 > "$tmp/end.json"
tc=$("$SMOKE_TOOLS/jsonq" '.tool_calls' "$tmp/end.json" 2>/dev/null)
tc=${tc%%.*}    # cJSON may render the number with a fractional part
if [ -n "$tc" ] && [ "$tc" -ge 3 ] 2>/dev/null; then
    t_ok "the run's tool_calls ($tc) includes the subagent's (>= 3)"
else
    t_fail "end event did not charge the delegate's calls: $(cat "$tmp/end.json" 2>/dev/null)"
fi

t_done
