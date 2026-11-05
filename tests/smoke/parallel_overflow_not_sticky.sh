#!/bin/sh
# smoke: a context-window 400 that flags the LEAD process must not leak into a
# LATER parallel write child that runs cleanly (M917, the review's R04-F5).
#
# THE DEFECT. app->ctx_overflow is set the moment a model call is refused as
# over-window (src/chat/jc_agent.c), and -- until this milestone -- never
# cleared anywhere. It is a per-PROCESS flag, but run_child reports it in the
# child's own done line (src/tools/jc_tool_parallel.c:964), and the parent's
# merge decision reads the child's `overflow` (jc_parallel_ending checks it
# before the stop). So one 400 anywhere in the process poisons every later
# parallel write child in that process, whatever the child itself did.
#
# THE TRIGGER, drivable in one headless turn. Both a spawn_subagent delegate and
# a spawn_parallel child build the SAME "focused sub-agent" system prompt, so the
# mock cannot tell them apart by system text -- it tells them apart by TASK:
# the subagent's task carries RESEARCH, the parallel child's carries CHILDP, and
# the two never appear in the other's history. spawn_subagent runs IN-PROCESS
# (it shares the lead's app pointer and calls run_agent_loop directly), so the
# subagent's context-window 400 sets the LEAD process's flag. The lead then forks
# a spawn_parallel WRITE child (the review's scenario 1b: a delegate overflows in
# the child's process, the child finishes normally, and is still reported as
# having run out of context -- because the flag is per process, not per run).
# The parallel child is forked AFTER the flag is set, so it inherits the stale
# value.
#
# THE FIX. Clear app->ctx_overflow at run_agent_loop entry, beside the other
# per-run last_* resets -- so it means "a context-window refusal ended THIS run"
# and no later run in the process (or forked child, which starts its own loop)
# inherits one. The subagent's own 400 still flags ITS run (that is correct);
# only the leak across run boundaries is removed.
#
#   1  the subagent's call was refused with a real context-window 400, whose body
#      reached the lead (a request captured with the over-window error) -- the
#      denominator: the flag was actually set in this process
#   2  CONTROL: a clean parallel write child, with the lead NOT flagged, still
#      merges with no verifier
#   3  a parallel write child that answered on its own merges when the lead's
#      process carries a stale overflow flag from an earlier in-process 400
#
# Markers are printed by jichi's output (the merge line) and by the mock's
# capture, never spelled in a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}

OVF_BODY='{"error":{"message":"litellm.ContextWindowExceededError: the request exceeds the model'\''s maximum context length","type":"invalid_request_error","code":400}}'

run_case() {  # $1 = mm file, $2 = extra config, $3 = prompt -> WS, CAP, OUT, LR
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@CAP@#$CAP#g" -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "$3" < /dev/null > "$OUT" 2>&1 )
    mm_stop
    LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1)
}

# --- the poisoned case: subagent 400, then a clean write child -------------
# Lead's system prompt says "command-line AI coding agent"; both the subagent
# and the parallel child say "focused sub-agent". Rules are ordered so the first
# matching one wins and no request is handled by the wrong rule.
cat > "$tmp/poison.mm" <<EOF
wire openai
rule
  match "You are a focused sub-agent"
  match "LEADSUB"
  status 400
  body $OVF_BODY
rule
  match "You are a focused sub-agent"
  match "CHILDP"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "You are a focused sub-agent"
  match "CHILDP"
  tool write_file {"path":"child.txt","content":"child\n"}
rule
  match "You are jichi, a command-line"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "You are jichi, a command-line"
  match "sub-agent run failed"
  tool spawn_parallel {"tasks":[{"task":"CHILDP write the file and answer","write":true}]}
rule
  match "You are jichi, a command-line"
  match "delegate the research"
  tool spawn_subagent {"task":"LEADSUB do the research"}
EOF

run_case "$tmp/poison.mm" ',"shellFence":false' 'LEAD delegate the research, then run the batch'

# ---- 1: the trigger really happened (the flag was set in this process) ----
# The subagent's 400 body is a RESPONSE, not in the request it caused -- but it
# propagates to the lead through the delegate's error, so SOME request carries
# it. Nothing below tests anything if that never happened.
n400=0
for req in "$CAP"/cap/req.*; do
    [ -f "$req" ] || continue
    if grep -q 'ContextWindowExceeded' "$req"; then n400=$((n400 + 1)); fi
done
if [ "$n400" -ge 1 ]; then
    t_ok "the subagent's context-window 400 reached the lead (the flag was set)"
else
    t_fail "no request carries the over-window error -- the trigger never fired,
so check 3 tests nothing: $(ls "$CAP"/cap/ 2>/dev/null | head)"
fi

# keep the poisoned child's workspace + result for check 3
POISON_WS="$WS"
POISON_LR="$LR"

# ---- 2: CONTROL -- a clean child, lead NOT flagged, merges as always ------
cat > "$tmp/clean.mm" <<EOF
wire openai
rule
  match "You are a focused sub-agent"
  match "CHILDP"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "You are a focused sub-agent"
  match "CHILDP"
  tool write_file {"path":"child.txt","content":"child\n"}
rule
  match "You are jichi, a command-line"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "You are jichi, a command-line"
  match "run the batch"
  tool spawn_parallel {"tasks":[{"task":"CHILDP write the file and answer","write":true}]}
EOF
run_case "$tmp/clean.mm" ',"shellFence":false' 'LEAD run the batch'
if [ -f "$WS/child.txt" ]; then
    t_ok "CONTROL: a clean parallel write child merges with no verifier"
else
    t_fail "the control child did not merge -- the rig is broken, not the fix: \
$(head_bytes 400 "$OUT")"
fi

# ---- 3: the poisoned child merges, not kept ------------------------------
if [ -f "$POISON_WS/child.txt" ]; then
    t_ok "a clean parallel write child merges when the lead carries a stale overflow flag"
else
    t_fail "the poisoned child's file was KEPT, not merged (red before M917: the
stale ctx_overflow labelled a clean child 'ran out of context'):
$(head_bytes 500 "$POISON_LR" 2>/dev/null)"
fi

t_done
