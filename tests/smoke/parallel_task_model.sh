#!/bin/sh
# smoke: the batch result names each task's model, and says when it was the lead's own
# (M891, the demos drive's §15).
#
# THE CASE. A task that names no model -- and whose profile names none -- runs on the
# lead's model. Run 0096's two probe batches named none, so they ran on the lead's 27B:
# 7 to 23 s a call, where the workers' coder model took 1.3 s. The batch result named no
# task's model, so the cost was visible only in the telemetry.
#
# THE FIX (the operator's decision, 2026-10-03: "Name each task's model"). Each task's
# section names the model it ran on, and says when that was the lead's own because
# neither the task nor its profile named one.
#
#   1  a task that names no model is shown on the lead's own model, and the line says so
#   2  a task that names a model is shown on it, without that note
#   3  CONTROL: that task's requests really went to that model -- the line names what
#      ran, not what was asked for
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
WS=$(smoke_tmp)
CAP=$(smoke_tmp)

cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADQ"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADQ"
  tool spawn_parallel {"tasks":[{"task":"CHILDP no model named"},{"task":"CHILDR on the second model","model":"m2"}]}
rule
  match "CHILDP"
  text DONE-P
rule
  match "CHILDR"
  text DONE-R
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
 {"name":"m2","provider":"openai","model":"mock-two","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
    -p "LEADQ run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop
LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | tail -1)

if [ -n "$LR" ] && grep -q "model: m -- the lead's own: neither the task nor its profile named one" "$LR"; then
    t_ok "a task that names no model is shown on the lead's own model, and the line says so"
else
    t_fail "no lead's-own model line: $(grep -o '### Task 1[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 200)"
fi
if [ -n "$LR" ] && grep -q '\[jichi\] model: m2\\n' "$LR"; then
    t_ok "a task that names a model is shown on it, without that note"
else
    t_fail "no m2 model line: $(grep -o '### Task 2[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 200)"
fi
two=$(grep -l 'CHILDR' "$CAP"/cap/req.* 2>/dev/null | xargs grep -l '"model":"mock-two"' 2>/dev/null | head -n 1)
if [ -n "$two" ]; then
    t_ok "CONTROL: that task's requests went to the model the line names (mock-two)"
else
    t_fail "CONTROL: no request of the m2 task asked for mock-two"
fi

t_done
