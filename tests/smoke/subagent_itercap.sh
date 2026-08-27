#!/bin/sh
# smoke: the subagent iteration-cap truncation note (M62 #5). The top-level
# model spawns a subagent whose model never finishes (every reply carries
# text AND another tool call), so it stops at maxSubagentIters=2; the
# partial answer returned to the parent must carry the "[stopped at its
# iteration limit]" note -- asserted in the captured request bodies.
# (Port of tests/e2e/subagent_itercap.py, M211; the combined text+tool
# reply uses the sse-file escape hatch.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
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

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"maxSubagentIters":2'

(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto -p "TOP run a subagent" \
    < /dev/null > /dev/null 2>&1); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "the run completed (rc=0)"
else
    t_fail "run rc=$rc"
fi

# M437 replaced M62's free-text note ("stopped at its iteration limit") with the
# structured delegation report, which carries the SAME guarantee in a form shared with
# spawn_parallel: a stop reason plus the remedy. The pinned string moved with it
# deliberately -- keeping the old sentence alongside would be two ways to say one thing,
# which is the drift M296 forbids. What must not change is the guarantee itself, so both
# halves are pinned: the machine-readable reason AND the statement that the answer may be
# incomplete.
if grep -l "\[delegate\] stop=max_iters" "$tmp"/req.* >/dev/null 2>&1 &&
   grep -l "may be partial" "$tmp"/req.* >/dev/null 2>&1; then
    t_ok "the iteration cap reaches the parent as stop=max_iters with its remedy"
else
    t_fail "truncation note missing from all $(ls "$tmp"/req.* 2>/dev/null | wc -l) captured requests"
fi

# M431: PROMPT_SUB told EVERY subagent "You cannot delegate further (no
# sub-agents of your own)". False at the default maxSubagentDepth of 2, where a
# depth-1 subagent IS advertised spawn_subagent -- a prompt that denies a tool the
# model can see invites it to distrust the rest of the prompt. The sentence now
# comes from jc_subagent_can_spawn, the same predicate that builds the tool array.
# This config leaves maxSubagentDepth at its default, so the nesting branch is the
# expected one; the leaf wording must NOT appear.
if grep -l "may delegate one further sub-agent" "$tmp"/req.* >/dev/null 2>&1 &&
   ! grep -l "cannot delegate further" "$tmp"/req.* >/dev/null 2>&1; then
    t_ok "the subagent prompt states the delegation depth it actually has"
else
    t_fail "delegation sentence wrong: $(grep -hoE 'cannot delegate further[^"]{0,40}|may delegate one further[^"]{0,40}' "$tmp"/req.* 2>/dev/null | sort -u | head -2)"
fi

# M434: ENFORCED IMPLIES STATED. The subagent prompt was PROMPT_SUB + env + extra, while
# three gates hold at any depth -- so a delegate was fenced by rules it had never seen.
# The most serious omission was not efficiency: without the untrusted-content rule a
# subagent that fetches a URL has no statement that fetched content is data, not
# instructions. Asserted on the captured request, the only ground truth for what the
# model actually received.
if grep -l "UNTRUSTED" "$tmp"/req.* >/dev/null 2>&1; then
    t_ok "the subagent is told the untrusted-content convention"
else
    t_fail "no untrusted-content rule in any of $(ls "$tmp"/req.* 2>/dev/null | wc -l) requests -- a delegate that fetches a URL is unguarded"
fi

t_done
