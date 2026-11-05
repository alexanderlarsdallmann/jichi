#!/bin/sh
# M800: a reader of the telemetry must be able to tell which CHILD of a
# spawn_parallel batch a record came from, and which MODEL it used. The lead runs
# on model "lead" (id mock-lead) and spawns two children on model "worker" (id
# mock-worker); each child makes one tool call. The mock's capture records the
# model id each request really named -- the ground truth the telemetry is compared
# with. Before M800, every depth-1 record named the LEAD's model (a child builds
# its own provider but does not switch app->config.model), carried no task field,
# and both children reused the same seq values (they fork with a copy of the
# parent's counter). This driver is the red half of that fix; it began life as the
# drive experiment E1-S7. Set E1_RES to keep the artifacts for diagnosis.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
RES="${E1_RES:-$tmp}"
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_A: run echo A1, then answer ADONE","model":"worker"},{"task":"TASK_B: run echo B1, then answer BDONE","model":"worker"}]}
rule
  match "TASK_A"
  match "\"role\":\"tool\""
  text ADONE
rule
  match "TASK_A"
  tool run_terminal_command {"command":"echo A1"}
rule
  match "TASK_B"
  match "\"role\":\"tool\""
  text BDONE
rule
  match "TASK_B"
  tool run_terminal_command {"command":"echo B1"}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"lead","provider":"openai","model":"mock-lead","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
 {"name":"worker","provider":"openai","model":"mock-worker","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    --model lead --log "$tmp/telemetry.jsonl" --log-level full \
    -p "LEADPROMPT run two things" < /dev/null > "$tmp/out.txt" 2>&1 )
mm_stop
mkdir -p "$RES"; cp -p "$tmp/telemetry.jsonl" "$tmp/out.txt" "$RES/" 2>/dev/null
# Ground truth from the mock: which model ids were requested.
worker_reqs=$(grep -l '"model": *"mock-worker"' "$tmp/cap"/* 2>/dev/null | wc -l)
lead_reqs=$(grep -l '"model": *"mock-lead"' "$tmp/cap"/* 2>/dev/null | wc -l)
# The telemetry's view, without a scripting language: one field per line.
T="$tmp/telemetry.jsonl"
d1_model_calls=$(grep '"event": *"model_call"' "$T" | grep -c '"depth": *1')
d1_labeled_worker=$(grep '"event": *"model_call"' "$T" | grep '"depth": *1' | grep -c '"model_id": *"mock-worker"')
d1_labeled_lead=$(grep '"event": *"model_call"' "$T" | grep '"depth": *1' | grep -c '"model": *"lead"')
d1_rows=$(grep -c '"depth": *1' "$T")
d1_task_field=$(grep '"depth": *1' "$T" | grep -c '"task"')
d1_seq_dups=$(grep '"depth": *1' "$T" | grep -o '"seq": *[0-9]*' | sort | uniq -d | wc -l)
echo "# mock ground truth: requests naming mock-worker=$worker_reqs, mock-lead=$lead_reqs"
echo "# telemetry depth-1: model_call rows=$d1_model_calls, labeled model_id mock-worker=$d1_labeled_worker, labeled model \"lead\"=$d1_labeled_lead"
echo "# telemetry depth-1: rows=$d1_rows, rows with a \"task\" field=$d1_task_field, duplicated seq values=$d1_seq_dups"
echo "# final: $(tr '\n' ' ' < "$tmp/out.txt" | head_bytes 120)"
[ "$worker_reqs" -ge 2 ] && grep -q LEADDONE "$tmp/out.txt" && t_ok "instrument: both children requested mock-worker ($worker_reqs requests) and the lead finished" \
    || t_fail "instrument: worker requests=$worker_reqs, lead done=$(grep -c LEADDONE "$tmp/out.txt") -- the scenario did not run as scripted"
[ "$d1_model_calls" -ge 2 ] && t_ok "instrument: telemetry has $d1_model_calls depth-1 model_call rows" || t_fail "instrument: no depth-1 model_call rows"
[ "$d1_labeled_worker" = "$d1_model_calls" ] && t_ok "every depth-1 model_call names the model the child used (mock-worker)" \
    || t_fail "depth-1 model_call rows naming mock-worker: $d1_labeled_worker of $d1_model_calls ($d1_labeled_lead name the lead's model)"
[ "$d1_rows" -gt 0 ] && [ "$d1_task_field" = "$d1_rows" ] && t_ok "every depth-1 row names its child (a task field)" \
    || t_fail "depth-1 rows with a task field: $d1_task_field of $d1_rows -- two children cannot be told apart"
[ "$d1_seq_dups" = 0 ] && t_ok "depth-1 seq values are unique" || t_fail "$d1_seq_dups seq value(s) appear more than once at depth 1"
t_done
