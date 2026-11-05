#!/bin/sh
# M804 (L1b): a `control abort` sent WHILE a spawn_parallel batch runs must interrupt
# the batch now (before M804 it was deferred to the boundary, so the batch ran on),
# AND -- because P1 preserves at teardown -- the killed children's work must survive.
# Each child writes a file then sleeps; abort arrives once the socket is up; the run
# must end well before the sleep, and the child's file must be recoverable from a
# refs/jichi/discarded/parallel-interrupted state.
. "$(dirname "$0")/_smoke.sh"

# M827 also checks two things about what this same interrupted batch REPORTS:
#   N14 -- the mid-batch abort reply says the batch stops now (not "queued to the
#          next tool boundary", which is true only in a shell call, not the pool);
#   N16 -- an interrupted worker's end record says "tool calls unknown", not "0"
#          (it never sent its final report, and a fabricated 0 makes a successor
#          discard a state that holds work).
t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
sock="$tmp/ctl.sock"
marker="KIDWORK_ab9_kept"
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_A: write then wait","write":true},{"task":"TASK_B: write then wait","write":true}]}
rule
  match "TASK_A"
  match "\"role\":\"tool\""
  text ADONE
rule
  match "TASK_A"
  tool run_terminal_command {"command":"printf '$marker\\n' > kid_a.txt && sleep 12"}
rule
  match "TASK_B"
  match "\"role\":\"tool\""
  text BDONE
rule
  match "TASK_B"
  tool run_terminal_command {"command":"printf '$marker\\n' > kid_b.txt && sleep 12"}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF

t0=$(date +%s)
# exec so $! is jichi itself, not the subshell (OpenBSD ksh); < /dev/null so the
# headless -p run does not block on the harness's held-open stdin.
( cd "$ws" && exec "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    --control "$sock" -p "LEADPROMPT run two writers" < /dev/null > "$tmp/out.txt" 2>&1 ) &
lead_pid=$!
# wait for the socket, then abort mid-batch (children are sleeping ~12 s)
n=0; while [ ! -S "$sock" ] && [ "$n" -lt 150 ]; do n=$((n+1)); sleep 0.1; done
sleep 2
with_deadline 20 "$BIN" control "$sock" abort < /dev/null > "$tmp/abort.out" 2>&1
wait "$lead_pid" 2>/dev/null
t1=$(date +%s); elapsed=$((t1 - t0))
mm_stop 2>/dev/null

shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
kept=0; refs=0; unkwn=0
if [ -n "$shadow" ]; then
    refs=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-interrupted/**' --format='%(refname)' 2>/dev/null | grep -c .)
    for r in $(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-interrupted/**' --format='%(refname)' 2>/dev/null); do
        if git --git-dir "$shadow" show "$r:kid_a.txt" 2>/dev/null | grep -q "$marker" \
           || git --git-dir "$shadow" show "$r:kid_b.txt" 2>/dev/null | grep -q "$marker"; then
            kept=1
        fi
        # M827 (N16): the end record (the commit body) counts tool calls "unknown".
        if git --git-dir "$shadow" log -1 --format=%B "$r" 2>/dev/null \
             | grep -qi 'tool calls unknown'; then
            unkwn=1
        fi
    done
fi
echo "# elapsed=${elapsed}s; abort reply: $(tr '\n' ' ' < "$tmp/abort.out" 2>/dev/null | head_bytes 80); parallel-interrupted refs=$refs; child file recovered=$kept; socket seen after ${n}x0.1s"

grep -qi 'abort' "$tmp/abort.out" 2>/dev/null && t_ok "instrument: the abort command was accepted mid-batch" \
    || t_fail "instrument: abort not acknowledged -- $(cat "$tmp/abort.out" 2>/dev/null)"
[ -n "$shadow" ] && t_ok "instrument: the shadow repo exists" || t_fail "instrument: no shadow repo"
[ "$elapsed" -lt 10 ] && t_ok "the batch was interrupted early (${elapsed}s, under the 12 s sleep)" \
    || t_fail "the batch ran ${elapsed}s -- the mid-batch abort did NOT interrupt it"
[ "$kept" = 1 ] && t_ok "a killed child's file is recoverable from a parallel-interrupted state" \
    || t_fail "no killed child's work was preserved (refs=$refs) -- interrupted work was lost"
# M827 (N14): the abort was served from the pool, so its reply says the batch stops
# now and work is kept -- not "queued to the next tool boundary" (a shell-call reply).
grep -qi 'stops now' "$tmp/abort.out" 2>/dev/null \
    && t_ok "the mid-batch abort reply says the batch stops now (not 'queued to a boundary')" \
    || t_fail "abort reply is not the pool wording: $(tr '\n' ' ' < "$tmp/abort.out" 2>/dev/null | head_bytes 100)"
# M827 (N16): the interrupted worker's end record counts tool calls as unknown.
[ "$unkwn" = 1 ] \
    && t_ok "an interrupted worker's end record counts tool calls 'unknown', not 0" \
    || t_fail "no interrupted end record said 'tool calls unknown' (refs=$refs)"
