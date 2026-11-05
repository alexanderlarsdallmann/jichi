#!/bin/sh
# M922 (F-J8): a reader of the telemetry must be able to tell which model a
# spawn_subagent child called. The lead runs on model "lead" (id mock-lead) and
# delegates to a subagent on model "worker" (id mock-worker); the child makes one
# tool call. The mock's capture records the model id each request really named --
# the ground truth the telemetry is compared with. Before M922, every depth-1
# record named the LEAD's model: spawn_parallel set the M800 override fields in
# its COW fork, but spawn_subagent runs in-process and never set them, so the
# model_call rows fell through to app->config.model, which mirrors the lead.
# Measured live by the 2026-10-08 zigodot drive with --dump-requests (F-J8): the
# child's request said mock-worker's id while its telemetry row said the lead's.
# This driver is the red half of that fix; it mirrors parallel_child_attribution
# (M800) for the in-process child. Set SUB_ATTR_RES to keep the artifacts.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
RES="${SUB_ATTR_RES:-$tmp}"
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_subagent {"task":"SUBTASK: run echo S1, then answer SDONE","model":"worker"}
rule
  match "SUBTASK"
  match "\"role\":\"tool\""
  text SDONE
rule
  match "SUBTASK"
  tool run_terminal_command {"command":"echo S1"}
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
    -p "LEADPROMPT delegate the subtask" < /dev/null > "$tmp/out.txt" 2>&1 )
mm_stop
mkdir -p "$RES"; cp -p "$tmp/telemetry.jsonl" "$tmp/out.txt" "$RES/" 2>/dev/null
# Ground truth from the mock: which model ids were requested.
worker_reqs=$(grep -l '"model": *"mock-worker"' "$tmp/cap"/* 2>/dev/null | wc -l)
lead_reqs=$(grep -l '"model": *"mock-lead"' "$tmp/cap"/* 2>/dev/null | wc -l)
# The telemetry's view, without a scripting language: one field per line.
T="$tmp/telemetry.jsonl"
d1_model_calls=$(grep '"event": *"model_call"' "$T" | grep -c '"depth": *1')
d1_labeled_worker=$(grep '"event": *"model_call"' "$T" | grep '"depth": *1' | grep -c '"model_id": *"mock-worker"')
d1_labeled_lead=$(grep '"event": *"model_call"' "$T" | grep '"depth": *1' | grep -c '"model_id": *"mock-lead"')
d0_model_calls=$(grep '"event": *"model_call"' "$T" | grep -c '"depth": *0')
d0_labeled_lead=$(grep '"event": *"model_call"' "$T" | grep '"depth": *0' | grep -c '"model_id": *"mock-lead"')
echo "# mock ground truth: requests naming mock-worker=$worker_reqs, mock-lead=$lead_reqs"
echo "# telemetry depth-1: model_call rows=$d1_model_calls, labeled mock-worker=$d1_labeled_worker, labeled mock-lead=$d1_labeled_lead"
echo "# telemetry depth-0: model_call rows=$d0_model_calls, labeled mock-lead=$d0_labeled_lead"
echo "# final: $(tr '\n' ' ' < "$tmp/out.txt" | head_bytes 120)"
[ "$worker_reqs" -ge 1 ] && [ "$lead_reqs" -ge 1 ] && grep -q LEADDONE "$tmp/out.txt" \
    && t_ok "instrument: the child requested mock-worker ($worker_reqs requests), the lead requested mock-lead ($lead_reqs), and the lead finished" \
    || t_fail "instrument: worker requests=$worker_reqs, lead requests=$lead_reqs, lead done=$(grep -c LEADDONE "$tmp/out.txt") -- the scenario did not run as scripted"
[ "$d1_model_calls" -ge 1 ] && [ "$d0_model_calls" -ge 1 ] \
    && t_ok "instrument: telemetry has $d1_model_calls depth-1 and $d0_model_calls depth-0 model_call rows" \
    || t_fail "instrument: model_call rows depth-1=$d1_model_calls depth-0=$d0_model_calls -- the nested run is missing"
[ "$d1_labeled_worker" = "$d1_model_calls" ] \
    && t_ok "every depth-1 model_call names the model the child used (mock-worker)" \
    || t_fail "depth-1 model_call rows naming mock-worker: $d1_labeled_worker of $d1_model_calls ($d1_labeled_lead name the lead's model)"
[ "$d0_labeled_lead" = "$d0_model_calls" ] \
    && t_ok "every depth-0 model_call still names the lead's model (mock-lead)" \
    || t_fail "depth-0 model_call rows naming mock-lead: $d0_labeled_lead of $d0_model_calls -- the override leaked to the lead"
t_done
