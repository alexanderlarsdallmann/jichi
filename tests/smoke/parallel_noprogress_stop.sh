#!/bin/sh
# M829 (N4b, threadwork's weekend drive): a spawn_parallel/subagent CHILD's whole
# task is ONE turn, so a no-progress loop there never gets a next turn to break out
# of -- on the drive a worker made one passing call 1,126 times in its single turn.
# The success-twin detector now ENDS a depth>=1 child when the same successful call
# reaches JC_NOPROGRESS_STOP_AT (9), after the warnings at 3 and 6, keeping its work.
# The top level (depth 0) still only warns.
#
# The child's mock returns the SAME successful command every call (same args, same
# output), so it would loop to maxSubagentIters (set high, 30) without the stop. With
# it, the child makes ~9-10 model calls and stops. We count the child's captured
# requests (all carry TASK_L): stopped is well under the cap. Before M829 it runs to
# the cap -- the red half.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_L loop the same call","write":true}]}
rule
  match "TASK_L"
  tool run_terminal_command {"command":"echo same_output_every_time"}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
 "maxSubagentIters":30,"maxToolIters":40,"parallelTaskTimeout":120,"preserveDiscarded":true}
EOF
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" -q --no-session \
    --auto --no-stdin -p "LEADPROMPT run one looping writer" > "$tmp/out.txt" 2>&1 )
mm_stop

# Every child model call carries the TASK_L seed; count them.
child_reqs=$(grep -l 'TASK_L' "$tmp"/cap/req.* 2>/dev/null | grep -c .)
echo "# child requests (TASK_L): $child_reqs (cap maxSubagentIters=30, stop threshold 9)"

[ "$child_reqs" -gt 0 ] \
    && t_ok "instrument: the child ran ($child_reqs model calls captured)" \
    || t_fail "instrument: no child requests captured (out: $(head_bytes 160 "$tmp/out.txt"))"

# With the stop the child ends ~9-10 calls in; without it, it runs to the 30 cap.
[ "$child_reqs" -lt 15 ] \
    && t_ok "a looping child is stopped near the threshold, not run to maxSubagentIters" \
    || t_fail "the child made $child_reqs calls -- it ran to the cap, no no-progress stop"
t_done
