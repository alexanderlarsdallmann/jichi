#!/bin/sh
# smoke: the autonomous task-loop reference example
# (examples/autonomous-loop). Drives the shipped loop.sh against the real
# jichi binary and a mock chat model, proving the supervisor contract:
# a task is claimed and run as a bounded --auto turn, the model calls the
# shipped report_status user tool (report.sh writes the operator-fixed
# $JICHI_REPORT_FILE), and on exit 0 the task moves pending/ -> done/.
# The SHIPPED config.autonomous.json is used with only its models block
# swapped for the mock (an awk surgery on that one array), and separately
# asserted to be valid JSON wiring report_status.
# (Port of tests/e2e/supervisor.py, M217.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
exdir="$SMOKE_ROOT/examples/autonomous-loop"
shipped="$exdir/config.autonomous.json"

# 1. the shipped config is valid JSON and wires report_status as a tool
if "$JQ" -q '.' "$shipped" 2>/dev/null \
   && "$JQ" '.tools[0].name' "$shipped" 2>/dev/null | grep -q report_status; then
    t_ok "the shipped config.autonomous.json parses and wires report_status"
else
    t_fail "shipped config invalid or report_status not wired"
fi

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text LOOP_DONE
rule
  tool report_status {"message":"task complete"}
EOF
mm_start "$tmp/replies.mm" "$tmp"

# swap ONLY the models array of the shipped config for the mock
awk -v port="$MM_PORT" '
  /^  "models": \[/ {
    print "  \"toolProfile\": \"full\","
    print "  \"lowResource\": false,"
    print "  \"models\": [{\"name\":\"worker\",\"provider\":\"openai\",\"model\":\"mock\",\"apiBase\":\"http://127.0.0.1:" port "/v1\",\"apiKey\":\"x\",\"roles\":[\"chat\"]}],"
    skip = 1; next
  }
  skip && /^  \],/ { skip = 0; next }
  !skip { print }
' "$shipped" > "$tmp/cfg.json"
if "$JQ" -q '.' "$tmp/cfg.json" 2>/dev/null; then
    t_ok "the model-swapped run config is still valid JSON"
else
    t_fail "the model-swapped config is not valid JSON"
fi

ws=$(smoke_tmp)
queue=$(smoke_tmp)
report="$tmp/status.log"
mkdir -p "$queue/pending"
printf 'Do the task and call report_status.\n' > "$queue/pending/t1.task"
chmod +x "$exdir/report.sh" "$exdir/loop.sh" 2>/dev/null

# Via `env`, not prefix assignments: with_deadline is a shell FUNCTION, and
# whether assignments prefixed to a function call are exported to the
# function's children is a POSIX gray zone -- dash 0.5.8 (stretch, V2f) does
# not export them, so loop.sh's ${JICHI_CONFIG:?} guard fired. `env` is
# specified unambiguously everywhere.
with_deadline 90 env \
    JICHI_BIN="$BIN" JICHI_CONFIG="$tmp/cfg.json" QUEUE="$queue" \
    WORKSPACE="$ws" JICHI_REPORT_FILE="$report" RUN_ONCE=1 \
    BUDGET_TOKENS=100k DEADLINE=2m MAX_TOOL_CALLS=10 VERIFY="" POLL=1 \
    PATH="$exdir:$PATH" \
    sh "$exdir/loop.sh" < /dev/null > "$tmp/loop.out" 2>&1
rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "loop.sh completed the run (exit 0)"
else
    t_fail "loop.sh rc=$rc: $(tail -c 300 "$tmp/loop.out")"
fi
if [ -f "$report" ] && grep -q "task complete" "$report"; then
    t_ok "report_status wrote the operator-fixed report file"
else
    t_fail "report file missing/empty: $(cat "$report" 2>/dev/null)"
fi
if [ -f "$queue/done/t1.task" ] \
   && [ -z "$(ls -A "$queue/pending" 2>/dev/null)" ]; then
    t_ok "the completed task was routed pending/ -> done/"
else
    t_fail "task not routed to done/ (done=$(ls "$queue/done" 2>/dev/null))"
fi

t_done
