#!/bin/sh
# smoke: a delegate child (spawn_parallel / spawn_subagent) keeps its ASSIGNED
# model on its LATER requests, not the lead's (M1207, the harness-port drive's
# DM batch, card [105] / SV:1409).
#
# THE DEFECT. A child that is given a different model sends its FIRST request to
# that model, but every LATER request in its loop goes to the LEAD's model. Cause:
# the fork (jc_tool_parallel.c:1121 for spawn_parallel, jc_tool_subagent.c for
# spawn_subagent) resolves the child's model into a LOCAL provider and passes it
# to the child's loop as opts->provider, while app->provider in the child's COW
# copy stays the LEAD's (the intended design). The loop's UNCONDITIONAL
# `prov = app->provider;` after every model call (jc_agent.c:2777, from M1038)
# then re-syncs the child's prov off its assigned model onto the lead's provider
# from request 2 on. The child's telemetry rows keep the assigned model's name
# (M800), so the rows look right while the wire is wrong -- which is exactly why
# this driver asserts on the mock CAPTURES (the raw requests each mock received),
# never on telemetry.
#
# INSTRUMENT. Two mocks on distinct ports: `lead` (mock A, model id lead-model)
# and `worker` (mock B, model id worker-model). Each capture dir holds one raw
# file per request (req.1, req.2, ... in arrival order). One model call = one
# captured request. Config has NO routing (so turn-start never touches
# app->provider in the lead run), shellFence off, snapshots/repoMap off,
# maxRetries 0. The child makes three model calls (two tool rounds + a final
# text); the lead makes two (the spawn call + the follow-up on the batch result).
#
#   DM1  spawn_parallel WRITE child on the worker -- GREEN: A=2, B=3.
#        RED pre-fix: A=3, B=1 (the child's rounds 2 and 3 land on the lead's
#        wire; the worker serves only the child's first call).
#   DM1p DM1 with the run pinned `--model lead` (the harness's form; the
#        operator's probe showed both shapes reproduce) -- same GREEN/RED.
#   DM2  spawn_parallel READ-ONLY child on the worker (a task with `model` but no
#        `write`, so it is offered read-only tools; its first tool call is
#        list_files) -- same count shape as DM1.
#   DM3  spawn_subagent child on the worker -- same count shape as DM1.
#   DM4  positive control: a spawn_subagent child given the LEAD's own model.
#        mc == active, so the child runs on app->provider by design and makes ONE
#        call, served by A. GREEN pre AND post: A=3 (lead turn1 + the child round
#        + lead final), B=0. Guards a fix that makes a same-model child lose its
#        round entirely.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# common config: two models, distinct ports, no routing, fence/snapshots off
cfg() {  # $1 = A_PORT $2 = B_PORT $3 = out path
    cat > "$3" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"lead","provider":"openai","model":"lead-model","apiBase":"http://127.0.0.1:$1/v1","apiKey":"x","roles":["chat"],"contextLength":65536,"inputCostPer1M":0,"outputCostPer1M":0},
 {"name":"worker","provider":"openai","model":"worker-model","apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","contextLength":65536,"inputCostPer1M":0,"outputCostPer1M":0}],
 "shellFence":false,"snapshots":false,"repoMap":false,"maxRetries":0}
EOF
}

# run_cell A_MM B_MM [EXTRA-JICHI-ARGS...]
# Start both mocks, run jichi against the shared workspace with the lead on mock A
# and the child's model on mock B, stop both, and record the per-mock request
# counts in $C_A / $C_B (plus the lead's stdout in $C_OUT for diagnosis).
run_cell() {
    C_A=$(smoke_tmp)
    C_B=$(smoke_tmp)
    mm_start "$1" "$C_A"; PA_PID=$MM_PID; PA_PORT=$MM_PORT
    mm_start "$2" "$C_B"; PB_PID=$MM_PID; PB_PORT=$MM_PORT
    cfg "$PA_PORT" "$PB_PORT" "$C_A/cfg.json"
    ( cd "$ws" && with_deadline 90 "$BIN" --config "$C_A/cfg.json" \
        -q --auto --no-session $3 -p "LEADX run" < /dev/null \
        > "$C_A/out" 2>&1 )
    kill "$PB_PID" "$PA_PID" 2>/dev/null
    wait "$PB_PID" "$PA_PID" 2>/dev/null
    MM_PID=""
    C_OUT="$C_A/out"
    C_A_N=$(ls "$C_A"/req.* 2>/dev/null | wc -l | tr -d ' ')
    C_B_N=$(ls "$C_B"/req.* 2>/dev/null | wc -l | tr -d ' ')
}

# A child's second request is its first tool round: its body carries a
# "role":"tool" message. Diagnostic only (the count shape is the red/green signal).
child_tool_round() {  # $1 = capture dir -> "yes" if req.2 has a tool round
    if [ -f "$1/req.2" ] && grep -q '"role":"tool"' "$1/req.2" 2>/dev/null; then
        printf 'yes'
    else
        printf 'no'
    fi
}

# --- DM1: spawn_parallel WRITE child on the worker ----------------------------
cat > "$tmp/dm1.a.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_parallel {"tasks":[{"model":"worker","write":true,"task":"run two shell commands then answer done"}]}
rule
  text LEAD1
EOF
cat > "$tmp/dm1.b.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"true"}
rule
  count 2
  tool run_terminal_command {"command":"true"}
rule
  text WORKER1
EOF
run_cell "$tmp/dm1.a.mm" "$tmp/dm1.b.mm"
if [ "$C_A_N" = "2" ] && [ "$C_B_N" = "3" ]; then
    t_ok "DM1 (write child): the child's later requests stay on its model (lead A=2, worker B=3; B req.2 tool round=$(child_tool_round "$C_B"))"
else
    t_fail "DM1 (write child): the child's later requests reached the LEAD's model -- lead A=$C_A_N (want 2), worker B=$C_B_N (want 3); B req.2 tool round=$(child_tool_round "$C_B"); out: $(head_bytes 120 "$C_OUT" 2>/dev/null)"
fi

# --- DM1p: the pinned variant (--model lead, the harness's form) --------------
run_cell "$tmp/dm1.a.mm" "$tmp/dm1.b.mm" --model lead
if [ "$C_A_N" = "2" ] && [ "$C_B_N" = "3" ]; then
    t_ok "DM1 pinned (--model lead): the child's later requests stay on its model (lead A=2, worker B=3)"
else
    t_fail "DM1 pinned (--model lead): the child's later requests reached the LEAD's model -- lead A=$C_A_N (want 2), worker B=$C_B_N (want 3); out: $(head_bytes 120 "$C_OUT" 2>/dev/null)"
fi

# --- DM2: spawn_parallel READ-ONLY child on the worker ------------------------
# A read-only child is a task with `model` but NO `write`: it is offered only
# read-only tools, so its first tool call is a read-only tool that needs no
# arguments (list_files). A failed tool call still produces a tool round.
cat > "$tmp/dm2.a.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_parallel {"tasks":[{"model":"worker","task":"list the directory then answer done"}]}
rule
  text LEAD1
EOF
cat > "$tmp/dm2.b.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {}
rule
  count 2
  tool list_files {}
rule
  text WORKER1
EOF
run_cell "$tmp/dm2.a.mm" "$tmp/dm2.b.mm"
if [ "$C_A_N" = "2" ] && [ "$C_B_N" = "3" ]; then
    t_ok "DM2 (read-only child): the child's later requests stay on its model (lead A=2, worker B=3)"
else
    t_fail "DM2 (read-only child): the child's later requests reached the LEAD's model -- lead A=$C_A_N (want 2), worker B=$C_B_N (want 3); out: $(head_bytes 120 "$C_OUT" 2>/dev/null)"
fi

# --- DM3: spawn_subagent child on the worker ----------------------------------
cat > "$tmp/dm3.a.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"run two shell commands then answer done","model":"worker"}
rule
  text LEAD1
EOF
run_cell "$tmp/dm3.a.mm" "$tmp/dm1.b.mm"
if [ "$C_A_N" = "2" ] && [ "$C_B_N" = "3" ]; then
    t_ok "DM3 (spawn_subagent): the child's later requests stay on its model (lead A=2, worker B=3)"
else
    t_fail "DM3 (spawn_subagent): the child's later requests reached the LEAD's model -- lead A=$C_A_N (want 2), worker B=$C_B_N (want 3); out: $(head_bytes 120 "$C_OUT" 2>/dev/null)"
fi

# --- DM4: positive control -- a child on the LEAD's own model -----------------
# mc == active: the child runs on app->provider by design and makes ONE call,
# served by A's catch-all. GREEN pre AND post fix. B must receive 0 requests.
cat > "$tmp/dm4.a.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"run one shell command then answer done","model":"lead"}
rule
  text LEAD1
EOF
cat > "$tmp/dm4.b.mm" <<'EOF'
wire openai
rule
  text UNUSED
EOF
run_cell "$tmp/dm4.a.mm" "$tmp/dm4.b.mm"
if [ "$C_A_N" = "3" ] && [ "$C_B_N" = "0" ]; then
    t_ok "DM4 (control, child on the lead's model): all three rounds on A, none on B (lead A=3, worker B=0)"
else
    t_fail "DM4 (control): a same-model child lost (or misplaced) a round -- lead A=$C_A_N (want 3), worker B=$C_B_N (want 0); out: $(head_bytes 120 "$C_OUT" 2>/dev/null)"
fi

# --- DM5: cut-continue nudge (b1, :2995) --------------------------------------
# A child whose second round is cut at the ceiling (finish_reason 'length')
# triggers the cut-continue nudge. The child's third round (the continuation)
# must stay on the child's model (worker-model) and reach mock B, NOT the lead.
#
# The child's second round: sse-file cut at ceiling (content delta, finish 'length').
# The child's third round: nudge text 'cut off at the output ceiling' -> text DONE5.
cat > "$tmp/dm5.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"Some content that is cut"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"length"}],"usage":{"prompt_tokens":12,"completion_tokens":8192}}

data: [DONE]

EOF
cat > "$tmp/dm5.a.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILDD do work","model":"worker"}
rule
  text LEAD1
EOF
cat > "$tmp/dm5.b.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"true"}
rule
  count 2
  sse-file @TMP@/dm5.sse
rule
  match "cut off at the output ceiling"
  text DONE5
EOF
# Write the sse file to the capture dir before the run (the @TMP@ idiom)
C5_A=$(smoke_tmp)
C5_B=$(smoke_tmp)
sed -e "s#@TMP@#$tmp#g" "$tmp/dm5.a.mm" > "$C5_A/r.mm"
sed -e "s#@TMP@#$tmp#g" "$tmp/dm5.b.mm" > "$C5_B/r.mm"
mm_start "$C5_A/r.mm" "$C5_A"; PA5_PID=$MM_PID; PA5_PORT=$MM_PORT
mm_start "$C5_B/r.mm" "$C5_B"; PB5_PID=$MM_PID; PB5_PORT=$MM_PORT
cfg "$PA5_PORT" "$PB5_PORT" "$C5_A/cfg.json"
( cd "$ws" && with_deadline 90 "$BIN" --config "$C5_A/cfg.json" \
    -q --auto --no-session -p "LEADX run" < /dev/null \
    > "$C5_A/out" 2>&1 )
kill "$PB5_PID" "$PA5_PID" 2>/dev/null
wait "$PB5_PID" "$PA5_PID" 2>/dev/null
MM_PID=""
C5_A_N=$(ls "$C5_A"/req.* 2>/dev/null | wc -l | tr -d ' ')
C5_B_N=$(ls "$C5_B"/req.* 2>/dev/null | wc -l | tr -d ' ')
# Count nudge requests in each capture dir
A_WITH_NUDGE=$(grep -l 'cut off at the output ceiling' "$C5_A"/req.* 2>/dev/null | wc -l | tr -d ' ')
B_WITH_NUDGE=$(grep -l 'cut off at the output ceiling' "$C5_B"/req.* 2>/dev/null | wc -l | tr -d ' ')
# Get the model id from the nudge-carrying A file (if any)
A_MODEL=""
A_MODEL_ID=""
if [ "$A_WITH_NUDGE" -ge 1 ]; then
    A_MODEL=$(grep -l 'cut off at the output ceiling' "$C5_A"/req.* 2>/dev/null | head -1 | xargs grep -o '"model":"[^"]*"' 2>/dev/null | head -1)
    # Extract just the model name for display
    A_MODEL_ID=$(printf '%s' "$A_MODEL" | sed 's/.*"model":"\([^"]*\)".*/\1/')
fi
if [ "$B_WITH_NUDGE" -ge 1 ] && [ "$A_WITH_NUDGE" -eq 0 ]; then
    t_ok "DM5 (cut-continue): the nudge round reached the child's worker-model, not the lead (C5_A_N=$C5_A_N, C5_B_N=$C5_B_N, B nudge=$B_WITH_NUDGE, A nudge=$A_WITH_NUDGE)"
else
    t_fail "DM5 (cut-continue): the nudge round reached the LEAD's model (C5_A_N=$C5_A_N, C5_B_N=$C5_B_N, B nudge=$B_WITH_NUDGE, A nudge=$A_WITH_NUDGE (model: $A_MODEL_ID); want B>=1, A=0)"
fi

# --- DM6: empty-reasoning nudge (b1, :3034) -----------------------------------
# A child whose second round ends on reasoning with no answer text triggers
# the empty-reasoning nudge. The child's third round (the continuation) must
# stay on the child's model (worker-model) and reach mock B, NOT the lead.
#
# The child's second round: sse-file reasoning-only (one reasoning delta, finish 'stop').
# The child's third round: nudge text 'ended your last reply with reasoning' -> text DONE6.
cat > "$tmp/dm6.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"Let me think this through..."},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":12,"completion_tokens":9}}

data: [DONE]

EOF
cat > "$tmp/dm6.a.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"CHILDD review notes","model":"worker"}
rule
  text LEAD1
EOF
cat > "$tmp/dm6.b.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"true"}
rule
  count 2
  sse-file @TMP@/dm6.sse
rule
  match "ended your last reply with reasoning"
  text DONE6
EOF
# Write the sse file to the capture dir before the run (the @TMP@ idiom)
C6_A=$(smoke_tmp)
C6_B=$(smoke_tmp)
sed -e "s#@TMP@#$tmp#g" "$tmp/dm6.a.mm" > "$C6_A/r.mm"
sed -e "s#@TMP@#$tmp#g" "$tmp/dm6.b.mm" > "$C6_B/r.mm"
mm_start "$C6_A/r.mm" "$C6_A"; PA6_PID=$MM_PID; PA6_PORT=$MM_PORT
mm_start "$C6_B/r.mm" "$C6_B"; PB6_PID=$MM_PID; PB6_PORT=$MM_PORT
cfg "$PA6_PORT" "$PB6_PORT" "$C6_A/cfg.json"
( cd "$ws" && with_deadline 90 "$BIN" --config "$C6_A/cfg.json" \
    -q --auto --no-session -p "LEADX run" < /dev/null \
    > "$C6_A/out" 2>&1 )
kill "$PB6_PID" "$PA6_PID" 2>/dev/null
wait "$PB6_PID" "$PA6_PID" 2>/dev/null
MM_PID=""
C6_A_N=$(ls "$C6_A"/req.* 2>/dev/null | wc -l | tr -d ' ')
C6_B_N=$(ls "$C6_B"/req.* 2>/dev/null | wc -l | tr -d ' ')
# Count nudge requests in each capture dir
A_WITH_NUDGE=$(grep -l 'ended your last reply with reasoning' "$C6_A"/req.* 2>/dev/null | wc -l | tr -d ' ')
B_WITH_NUDGE=$(grep -l 'ended your last reply with reasoning' "$C6_B"/req.* 2>/dev/null | wc -l | tr -d ' ')
# Get the model id from the nudge-carrying A file (if any)
A_MODEL=""
A_MODEL_ID=""
if [ "$A_WITH_NUDGE" -ge 1 ]; then
    A_MODEL=$(grep -l 'ended your last reply with reasoning' "$C6_A"/req.* 2>/dev/null | head -1 | xargs grep -o '"model":"[^"]*"' 2>/dev/null | head -1)
    # Extract just the model name for display
    A_MODEL_ID=$(printf '%s' "$A_MODEL" | sed 's/.*"model":"\([^"]*\)".*/\1/')
fi
if [ "$B_WITH_NUDGE" -ge 1 ] && [ "$A_WITH_NUDGE" -eq 0 ]; then
    t_ok "DM6 (empty-reasoning): the nudge round reached the child's worker-model, not the lead (C6_A_N=$C6_A_N, C6_B_N=$C6_B_N, B nudge=$B_WITH_NUDGE, A nudge=$A_WITH_NUDGE)"
else
    t_fail "DM6 (empty-reasoning): the nudge round reached the LEAD's model (C6_A_N=$C6_A_N, C6_B_N=$C6_B_N, B nudge=$B_WITH_NUDGE, A nudge=$A_WITH_NUDGE (model: $A_MODEL_ID); want B>=1, A=0)"
fi

t_done
