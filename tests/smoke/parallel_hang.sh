#!/bin/sh
# smoke: the per-child watchdog in the spawn_parallel fork pool (M62 #1).
# The top-level agent launches two read-only subtasks; TASK_GOOD gets a
# normal answer, TASK_HANG gets SSE headers then a frozen stream. With a
# small parallelTaskTimeout (and a larger model stall timeout, so the
# watchdog -- not the HTTP layer -- fires), the swarm must NOT hang: the
# stalled child is killed, the good child answers, the run exits. A single
# sequential mock suffices: the watchdog closes the stalled child in
# bounded time (M216 verified concurrent accept was NOT needed).
# (Port of tests/e2e/parallel_hang.py, M216.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# role:tool (top-level 2nd call) checked FIRST -- the echoed spawn_parallel
# args also contain the TASK_* markers, but only the top level has a tool role.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TASK_HANG"
  stall header
rule
  match "TASK_GOOD"
  text GOODDONE
rule
  tool spawn_parallel {"tasks":[{"task":"TASK_GOOD: say GOODDONE"},{"task":"TASK_HANG: stall forever"}]}
EOF

mm_start_unbounded "$tmp/replies.mm" "$tmp"

cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "parallelTaskTimeout":3,"timeouts":{"stall":30}}
EOF

t0=$(date +%s)
out=$(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "run two things in parallel" \
      < /dev/null 2>&1); rc=$?
t1=$(date +%s)
mm_stop
elapsed=$((t1 - t0))

if [ "$elapsed" -le 30 ]; then
    t_ok "the watchdog bounded the swarm (${elapsed}s, no hang)"
else
    t_fail "run took ${elapsed}s -- watchdog too slow or did not fire"
fi
case "$out" in
    *GOODDONE*|*TOPDONE*) t_ok "the swarm completed with a final answer" ;;
    *) t_fail "no final answer (rc=$rc): $(printf '%s' "$out" | head_bytes 150)" ;;
esac

t_done
