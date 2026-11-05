#!/bin/sh
# smoke: a worker stopped from outside with SIGTERM is reported as that, not as the
# operator's interrupt, and the call the stop killed gets no limit advice (M885, the
# demos drive's N47, continued).
#
# THE DEFECT. Run 0102 (2026-10-03, 02:53): the drive's supervisor stopped one worker
# with SIGTERM. The lead's batch result said, about the worker's run, "the operator
# interrupted the run -- stop and report, do not restart it", and about the shell call
# the SIGTERM had killed, "a limit killed the call, so a larger version of the same
# subtask will be killed too -- split it". The zigodot lead copied the first into the
# team's LOG as the operator's interruption of the whole run. Nobody had interrupted
# it, and no limit had been met.
#
# HOW IT RUNS. A write child starts a long command that writes its parent's pid -- the
# jichi child -- and the driver SIGTERMs that pid, the supervisor's move. The child's
# command runner kills the command, the child's run ends, and the child reports. The
# config sets the drive's `runTimeout: 900`: without a timeout or a memory budget jichi
# runs a command on its popen path, which never stops the command on a SIGTERM -- the
# first version of this driver measured its `sleep 60` running to the end, 60,003 ms
# (DEFERRED def-315).
#
#   1  the child's report is in the batch result and does not say the operator
#      interrupted the run
#   2  it says the child was stopped from outside the run (SIGTERM)
#   3  the killed call's own note names the SIGTERM
#   4  the killed call gets no limit advice; it was killed because the run was being
#      stopped
#   5  CONTROL: a call killed by its own signal, in a child that then answered, keeps
#      the limit advice -- 4's absence is the stop's, not the advice's
#
# The markers come from jichi's output -- the lead's next request to the mock, which
# carries the batch result -- never from a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- a write child runs in a worktree"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

cfg() {
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "runTimeout":900}
EOF
}

# start_lead <mm-file> <prompt>: the lead runs in the background -> WS, CAP, LPID
start_lead() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@PIDF@#$PIDF#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg > "$CAP/cfg.json"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "$2" < /dev/null > "$CAP/out" 2>&1 ) &
    LPID=$!
}
finish_lead() { wait "$LPID" 2>/dev/null; mm_stop; }
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }

# --- 1-4: stopped from outside, mid-command -----------------------------------------
PIDF="$tmp/child.pid"
cat > "$tmp/term.mm" <<'EOF'
wire openai
rule
  match "LEADT"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADT"
  tool spawn_parallel {"tasks":[{"task":"CHILDT run the long check","write":true}]}
rule
  match "CHILDT"
  tool run_terminal_command {"command":"echo $PPID > @PIDF@; sleep 60"}
EOF
rm -f "$PIDF"
start_lead "$tmp/term.mm" "LEADT run the batch"
i=0
while [ ! -s "$PIDF" ] && [ "$i" -lt 300 ]; do
    sleep 0.1 2>/dev/null || sleep 1; i=$((i + 1))
done
[ -s "$PIDF" ] && kill -TERM "$(cat "$PIDF")" 2>/dev/null
finish_lead
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'stop=aborted' "$LR" &&
   ! grep -q 'operator interrupted' "$LR"; then
    t_ok "the stopped child's report does not say the operator interrupted the run"
else
    t_fail "report missing, or it blames the operator: $(grep -o '\[delegate\][^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 600)"
fi
if [ -n "$LR" ] && grep -q 'stopped from outside the run (SIGTERM)' "$LR"; then
    t_ok "it says the child was stopped from outside the run (SIGTERM)"
else
    t_fail "no SIGTERM stop in the report: $(grep -o '\[delegate\][^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 600)"
fi
if [ -n "$LR" ] && grep -q 'stopped: the run received SIGTERM' "$LR"; then
    t_ok "the killed call's own note names the SIGTERM"
else
    t_fail "the killed call's note does not name the SIGTERM: $(grep -o 'last failing call[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 400)"
fi
if [ -n "$LR" ] && grep -q 'killed because the run was being stopped' "$LR" &&
   ! grep -q 'a limit killed the call' "$LR"; then
    t_ok "the killed call gets no limit advice: the stop killed it"
else
    t_fail "the killed call is advised as a limit: $(grep -o '\[delegate\][^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 600)"
fi

# --- 5: CONTROL -- a call killed by its own signal keeps the limit advice -------------
PIDF="$tmp/unused.pid"
cat > "$tmp/kill.mm" <<'EOF'
wire openai
rule
  match "LEADK"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADK"
  tool spawn_parallel {"tasks":[{"task":"CHILDK run the crashing check","write":true}]}
rule
  match "CHILDK"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDK"
  tool run_terminal_command {"command":"sh -c 'kill -KILL $$'"}
EOF
start_lead "$tmp/kill.mm" "LEADK run the batch"
finish_lead
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'a limit killed the call' "$LR" &&
   ! grep -q 'was being stopped' "$LR"; then
    t_ok "CONTROL: a call killed by its own signal keeps the limit advice"
else
    t_fail "CONTROL: no limit advice for a call its own signal killed: $(grep -o '\[delegate\][^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 600)"
fi

t_done
