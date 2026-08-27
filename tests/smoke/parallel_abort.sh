#!/bin/sh
# smoke: abort propagation + reaping in the spawn_parallel fork pool
# (M62 #2). The top-level agent launches two read-only subtasks whose
# model streams stall (headers, then nothing), so both children are live;
# we SIGINT the parent ~1.5s in. The parent must propagate the abort,
# SIGTERM/escalate+reap both children, and exit promptly -- not deadlock
# in waitpid on a stalled child. The per-child watchdog is set far above
# the test window, so it is the abort -- not the watchdog -- that ends
# the run. A sequential mock suffices (the abort closes the stalled
# connections in bounded time; M216 found concurrent accept unneeded).
# (Port of tests/e2e/parallel_abort.py, M216.)
#
# M540 ADDED CHECK 1, AND THE REASON IS THAT THE OTHER TWO PROVED NOTHING. This
# driver was the ONE genuinely vacuous driver a mutant sweep found in 202: run it
# with JC_SMOKE_BIN pointing at a shell script that does nothing and exits 0, and
# both original checks PASSED -- "SIGINT-ed parent exited" and "exit was prompt
# (0s)". Of course they did. A binary that does nothing exits promptly too.
#
# The header above claims the parent "must propagate the abort, SIGTERM/escalate+
# reap both children". Neither check observed a child at all. What was actually
# asserted was "something exited within 15 seconds", which is a property of almost
# any program. Check 1 supplies the missing denominator: the mock must have served
# TWO stalled TASK_ requests, so there really were two children to reap, and the
# promptness measured below is promptness at reaping them rather than promptness at
# doing nothing.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TASK_"
  stall header
rule
  tool spawn_parallel {"tasks":[{"task":"TASK_A: stall"},{"task":"TASK_B: stall"}]}
EOF

mm_start_unbounded "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "parallelTaskTimeout":120,"timeouts":{"stall":120}}
EOF

(cd "$ws" && exec "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    -p "run two stalling things" < /dev/null > /dev/null 2>&1) &
RUN_PID=$!
sleep 3                         # let the pool launch both children
t0=$(date +%s)
kill -INT "$RUN_PID" 2>/dev/null
# wait for the parent to exit, bounded well under the 120s watchdog/stall
i=0
while kill -0 "$RUN_PID" 2>/dev/null; do
    i=$((i + 1)); [ $i -gt 15 ] && break
    sleep 1
done
t1=$(date +%s)
elapsed=$((t1 - t0))
mm_stop

# --- 1: there were two children to reap (the denominator) -------------------
# Without this the checks below pass against a binary that never forked anything.
# Two TASK_ requests reached the mock => spawn_parallel launched both subtasks and
# both stalled, which is the state whose abort this driver exists to measure.
ntask=$(grep -l 'TASK_' "$tmp"/req.* 2>/dev/null | wc -l | tr -d ' ')
if [ "$ntask" -ge 2 ]; then
    t_ok "the fork pool launched both children ($ntask stalled TASK_ requests)"
else
    t_fail "only $ntask TASK_ request(s) reached the mock (want >= 2) -- no \
children were stalled, so nothing below measures abort or reaping"
fi

if kill -0 "$RUN_PID" 2>/dev/null; then
    kill -KILL "$RUN_PID" 2>/dev/null
    t_fail "parent did not exit within 15s of SIGINT -- abort/reaping deadlocked"
    t_fail -
    t_done
fi
wait "$RUN_PID" 2>/dev/null
t_ok "SIGINT-ed parent exited (reaped both stalled children)"
if [ "$elapsed" -le 12 ]; then
    t_ok "exit was prompt (${elapsed}s) -- the abort path, not the 120s watchdog"
else
    t_fail "took ${elapsed}s -- the abort path hung (watchdog window reached)"
fi

t_done
