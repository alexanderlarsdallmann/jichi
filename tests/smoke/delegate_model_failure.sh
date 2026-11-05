#!/bin/sh
# smoke: a delegate whose model calls failed is reported as such, not by its last
# failing tool call (M876: the demos drive's N47 (a), (b) and (c)).
#
# THE CASE (run 0094, 2026-10-02). The HRZ gateway went down mid-task. A worker's
# model calls failed with connect timeouts, its run ended `stop=error` after 49
# calls, and the batch result said "a command it ran kept hitting its own time
# limit ... narrow or restructure it" -- its last failing TOOL call, three
# `timeout N` runs of an 84 s suite. Nothing named the model calls. The lead's
# handoff blamed a command timeout and planned around it.
#
# THE CHECKS. Two write children, two models on two mock endpoints: the worker's
# answers ONCE and is gone (--max-requests 1), so its next call is refused.
#   1  the batch result names the model-call failure as the reason the run ended
#   2  ...before its last failing call, which is marked an earlier event, with no
#      time-limit remedy
#   3  a child that hit its own time limit once, and then answered, is told it HIT
#      the limit -- not that it "kept hitting" it (N47 (c))
#   4  spawn_subagent's report names its model failure the same way
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
WS=$(smoke_tmp)

# the worker's endpoint: one answer -- a command cut off at its own bound -- then gone
cat > "$tmp/w.mm" <<'EOF'
wire openai
rule
  match "CHILDX"
  tool run_terminal_command {"command":"timeout 1 sleep 5"}
EOF
mm_start "$tmp/w.mm" "$tmp/capW" 1
WPORT=$MM_PORT

# the lead's endpoint, which also serves the healthy child
cat > "$tmp/l.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "sub-agent run failed"
  text LEAD-DONE
rule
  match "LEADX"
  match "## Parallel agents"
  tool spawn_subagent {"task":"SUBX look around","model":"worker"}
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDX run the suite","model":"worker","write":true},{"task":"CHILDY run the suite","write":true}]}
rule
  match "CHILDY"
  match "\"role\":\"tool\""
  text CHILDY-DONE
rule
  match "CHILDY"
  tool run_terminal_command {"command":"timeout 1 sleep 5"}
EOF
mm_start_unbounded "$tmp/l.mm" "$tmp/capL"
LPORT=$MM_PORT

cat > "$tmp/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"lead","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$LPORT/v1","apiKey":"x","roles":["chat"]},
 {"name":"worker","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$WPORT/v1","apiKey":"x"}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false}
EOF
( cd "$WS" && with_deadline 120 "$BIN" --config "$tmp/cfg.json" -q --auto \
    -p "LEADX run the batch" < /dev/null > "$tmp/out" 2>&1 )
mm_stop
BR=$(grep -l '## Parallel agents' "$tmp"/capL/req.* 2>/dev/null | head -1)
SR=$(grep -l 'sub-agent run failed' "$tmp"/capL/req.* 2>/dev/null | tail -1)

# 1: the run's own failure is named.
if [ -n "$BR" ] &&
   grep -q 'the run ended because its model calls failed: ' "$BR"; then
    t_ok "the batch result names the model-call failure as the reason the run ended"
else
    t_fail "batch result: $(grep -o 'Task 1[^#]*' "$BR" 2>/dev/null | head_bytes 400)"
fi

# 2: ...before the last failing call, which is an earlier event, with no remedy for it.
x=$(grep -o 'Task 1: CHILDX[^#]*' "$BR" 2>/dev/null)
if printf '%s' "$x" | grep -q 'model calls failed.*last failing call: run_terminal_command (at_bound).*it is not why the run ended' &&
   ! printf '%s' "$x" | grep -q 'time limit'; then
    t_ok "the last failing call follows, marked an earlier event, with no time-limit remedy"
else
    t_fail "task 1: $(printf '%s' "$x" | head_bytes 500)"
fi

# 3: one hit is a hit (N47 (c)).
y=$(grep -o 'Task 2: CHILDY[^#]*' "$BR" 2>/dev/null)
if printf '%s' "$y" | grep -q 'hit its own time limit and was cut off' &&
   ! printf '%s' "$y" | grep -q 'kept hitting'; then
    t_ok "a single cut-off is reported as a hit, not as \"kept hitting\""
else
    t_fail "task 2: $(printf '%s' "$y" | head_bytes 400)"
fi

# 4: spawn_subagent, the same report -- read from ITS tool result alone: the same
# request also carries the batch result above, which says the same sentence about
# CHILDX (a first version of this check read the whole request and passed with
# spawn_subagent's line removed).
sub=$(grep -o 'error: sub-agent run failed[^"]*' "$SR" 2>/dev/null | head -1)
if [ -n "$sub" ] &&
   printf '%s' "$sub" | grep -q 'the run ended because its model calls failed: '; then
    t_ok "spawn_subagent's report names its model failure the same way"
else
    t_fail "subagent result: $(printf '%s' "$sub" | head_bytes 400)"
fi

t_done
