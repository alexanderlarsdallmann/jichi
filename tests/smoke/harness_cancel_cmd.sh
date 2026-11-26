#!/bin/sh
# smoke: the `jichi control <sock> cancel-cmd [task]` verb (M1192 B3, def-376) --
# the RED-FIRST driver (wave A). No fix, no code change: the verb is wave C's.
#
# THE UNIVERSE (what this driver pins) -- six cells, one headless --auto run
# with a --control socket each, a real `sh`, the mock model for the model:
#   1. LEAD target, no task: the lead's first tool call is `sleep 30` under
#      "runTimeout": 20 in the config (so the command takes the WATCHED path,
#      D7; no per-call timeout is set anywhere), and cancel-cmd with a reason
#      is sent while the sleep is in flight (status's last_tool ==
#      run_terminal_command).
#   2. WORKER target, with task: M1149's start_lead_ctl idiom -- the lead's
#      first reply is a spawn_parallel with ONE write task whose child runs
#      `sleep 30` (a read-only child fences run_terminal_command, so the child
#      must be write:true, in a git workspace; it inherits the lead's config,
#      "runTimeout": 20), and cancel-cmd task 0 is sent while the batch is
#      live (last_tool == spawn_parallel, served by the pool's ~200 ms wake).
#   3. REFUSAL BY NAME, no batch: a lead that never spawns (M1149 cell 12(b)'s
#      pool-ops-NULL shape: its only tool is a short sleep, served mid-tool),
#      cancel-cmd task 0 sent while that sleep is in flight.
#   4. TWO CANCELS: in cell 1's run, a second cancel-cmd right after the first
#      reply lands and the run is between calls.
#   5. (M1195 b) NOT-IN-A-COMMAND: a batch of ONE write child whose first
#      command is short (`sleep 1`) and whose second mock reply is HELD
#      (`delay 12000`) -- a live between-commands gap (the child's status:
#      in_cmd 0, tool still set). The cell polls the gap, sends cancel-cmd
#      task 0 in it, and asserts the EXACT fourth refusal 'task 0 is not in
#      a command (tool: run_terminal_command)'. RED before the in_cmd
#      serialization: the status carries no in_cmd, the poll reads null, the
#      gap is never observed, the cell fails.
#   6. (M1195 c) TELEMETRY ROW: reuses cell 1's landed cancel (exit 143 +
#      the note) and asserts the run's OWN telemetry file
#      (~/.jichi.d/telemetry/<ws>-<key>.jsonl, written by default at the
#      metrics level -- no --log flag) carries the tool_call line with
#      "stopped":"cancel" (the emitter, jc_agent.c), plus the offline
#      `jichi telemetry` rendering of the cancelled line.
#
# THE VERB IS RED BY CONSTRUCTION (this IS the red). `cancel-cmd` does not
# exist on this tree: the codec's verb list is the eight, and the shipped
# `jichi control` client refuses a ninth verb CLIENT-side (it checks
# jc_control_type_parse before connecting), so every cell sends the verb over
# the raw-socket `sockq` client (as control.sh and M1149's driver do) and gets
# the SERVER's codec refusal back: a reply carrying "unknown command" and the
# verb list. That refusal -- with the absence of every effect (no cancel note
# in any request, no exit 143, no "no batch is live" / "no command is running"
# refusal, no batch completing early) -- is the t_fail committed here. Wave C
# (the verb in src/) turns each effect green; the green assertions below are
# written to fire then and must not be edited into the red.
#
# THE 143-NOT-137 CONTRACT (D2, the notes/M1192.md addendum): on this tree
# EVERY kill of a watched command -- timeout, abort, byte cap, memory --
# funnels through one path, the SIGKILL escalation (jc_app.c:856) and
# `*exit_code = 137` (:916). B3's cancel must therefore carry its OWN flag
# (the M342 shape: several kills do not share a diagnosis) set when the cancel
# fires, and report the note plus exit status **143**, not 137. The green
# cells assert 143 and the exact note.
#
# THE NO-CAPS RULE: the jichi invocations inside carry no timeout wrapper and
# no per-call timeout. The `sleep 30` is INSIDE jichi -- it is cancelled in
# seconds (green) or ends at the config's own runTimeout (red), never at a
# driver cap. The with_deadline fence around each headless run is the
# DRIVER'S OWN guard (allowed), well above every cell's expected end.
#
# MEASURED SERVE-POINT DEVIATION (cell 3's shape). The control socket is
# served at the agent-loop boundaries and every ~300 ms of a tool call, but
# NOT during a model call's HTTP wait (probed on this tree: a connection made
# mid-model-call receives no reply and is reset at run end; only the
# pre-request boundary before the FIRST model call serves it). The brief's
# "plain text, no tool calls at all" mock can therefore never get the reply
# while its single model call is in flight, so cell 3 uses M1149 cell 12(b)'s
# exact no-batch shape instead -- a lead whose only tool is a `sleep` and who
# never spawns (pool_ops stays NULL), which self-terminates in seconds and is
# precisely the shape the green "no batch is live" refusal requires.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

JQ="$SMOKE_TOOLS/jsonq"
SOCKQ="$SMOKE_TOOLS/sockq"
# B3.3's note (distinct from M885's SIGTERM note and the timeout note).
NOTE='cancelled by the supervisor (control cancel-cmd)'
# D7: the watched path for every model-issued command (no per-call timeout).
EXTRA=',"runTimeout":20,"shellFence":false'

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}
# The telemetry files that EXISTED before this driver ran, so a cell that reads
# the run's own telemetry (~/.jichi.d/telemetry/<ws>-<key>.jsonl -- written by
# default at the metrics level, no --log flag) can ignore files from earlier
# drivers in the same suite run. One file per line.
TEL_PRE="$tmp/tel_pre"
ls "$HOME/.jichi.d/telemetry" 2>/dev/null | sort > "$TEL_PRE"

# cs <sock> <json-line> -> reply on stdout (raw socket: one line in, one out).
cs() { printf '%s\n' "$2" | "$SOCKQ" --deadline 30 "$1" 2>&1; }
# cs_status <sock> -> the current status reply (ok:true, or an error).
cs_status() { cs "$1" '{"v":1,"type":"status"}'; }
# cs_abort <sock> -> abort the whole run (a known verb; on the POOL it is
# applied at the next ~200 ms wake, so it unblocks a batch the mock still
# holds).
cs_abort() { cs "$1" '{"v":1,"type":"abort"}' > /dev/null 2>&1; }

# wait_sock <path> -> 0 once the unix socket exists (control.sh's idiom).
wait_sock() {
    _ws_i=0
    while [ ! -S "$1" ] && [ "$_ws_i" -lt 80 ]; do sleep 0.5; _ws_i=$((_ws_i + 1)); done
    [ -S "$1" ]
}
# num_gt A B / num_lt A B -- compare possibly-FRACTIONAL numbers (awk). The
# telemetry's duration_ms is written with a decimal point, so the shell's
# integer -lt/-gt would reject it outright (a 901.62 string is not an integer).
num_gt() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a+0 > b+0)}'; }
num_lt() { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a+0 < b+0)}'; }
# wait_last_tool <sock> <tool> -> block until a status reply says last_tool is
# the tool (the cell's in-flight window is open: last_tool is set at the tool's
# start, jc_agent.c:3521, and status is served mid-tool every ~300 ms).
# Echoes the live status. The 40 x 0.5 s bound is a FIXTURE margin, not a cap:
# the run's own with_deadline fence ends it first.
wait_last_tool() {
    _wlt_r=""
    _wlt_i=0
    while [ "$_wlt_i" -lt 40 ]; do
        _wlt_r=$(cs_status "$1")
        _wlt_t=$(printf '%s' "$_wlt_r" | "$JQ" '.last_tool' 2>/dev/null)
        [ "$_wlt_t" = "$2" ] && break
        _wlt_i=$((_wlt_i + 1))
        sleep 0.5
    done
    printf '%s' "$_wlt_r"
}
# ctl_wait_batch <sock> -> block until a status reply says last_tool is
# spawn_parallel (the batch is LIVE, M1149's idiom). Echoes the live status.
ctl_wait_batch() {
    _wl_r=""
    _wl_i=0
    while [ "$_wl_i" -lt 40 ]; do
        _wl_r=$(cs_status "$1")
        _wl_lt=$(printf '%s' "$_wl_r" | "$JQ" '.last_tool' 2>/dev/null)
        [ "$_wl_lt" = "spawn_parallel" ] && break
        _wl_i=$((_wl_i + 1))
        sleep 0.5
    done
    printf '%s' "$_wl_r"
}
# start_lead_ctl <mm-file> <extra-config> <sock> [git]: M1149's idiom. The
# with_deadline fence (30 s) is the driver's own guard around the run, NOT a
# cap on the measurement (the cell's assertion is the bound). Sets WS, CAP,
# OUT, LPID, SOCK; finish_lead_ctl sets RUN_RC. [git] makes the workspace a
# repo with one committed seed: cell 2's write child runs in a git worktree
# (the parallel drivers' idiom) -- and a write:true child, not write:false:
# read-only children fence run_terminal_command, so they could never run the
# sleep that the cell cancels.
start_lead_ctl() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    if [ "${4:-}" = "git" ]; then
        ( cd "$WS" && git init -q && git config user.email t@t \
            && git config user.name t \
            && git add -A && git commit -qm init )
    fi
    CAP=$(smoke_tmp)
    cp "$1" "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"; SOCK="$3"
    rm -f "$SOCK"
    ( cd "$WS" && with_deadline 30 "$BIN" --config "$CAP/cfg.json" \
        --control "$SOCK" -q --auto \
        -p "LEADX run the task" < /dev/null > "$OUT" 2>&1 ) &
    LPID=$!
}
# finish_lead_ctl: wait the lead, stop the mock, unlink the socket.
finish_lead_ctl() {
    wait "$LPID" 2>/dev/null
    RUN_RC=$?
    mm_stop
    rm -f "$SOCK" 2>/dev/null
}

# ============================================================================
# --- cells 1 + 4: the LEAD's own command (one run, two cancels) --------------
# req.1 -> the `sleep 30` tool call; req.2 (carries the tool result) -> the
# closing text. GREEN: req.2's result carries the cancel note with exit 143.
# RED: req.2's result carries the runTimeout note with exit 124 (the un-
# cancelled sleep ends at the config's own 20 s limit -- the driver's
# measurement of the defect the verb exists to avoid), and the run still
# completes cleanly, so the cell never hangs on a broken build either.
cat > "$tmp/c1.mm" <<'EOF'
wire openai
rule
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"sleep 30"}
rule
  match "\"role\":\"tool\""
  text CC-DONE
EOF
start_lead_ctl "$tmp/c1.mm" "$EXTRA" "$tmp/c1.sock"
WS1=$WS
if ! wait_sock "$tmp/c1.sock"; then
    finish_lead_ctl
    t_fail "cell 1 (lead target, no task): the control socket never appeared"
    t_fail "cell 2 (worker target, task 0): not run"
    t_fail "cell 3 (no batch, refusal by name): not run"
    t_fail "cell 4 (two cancels): not run"
    t_fail "cell 5 (not in a command, refusal by effect): not run"
    t_fail "cell 6 (telemetry stopped:cancel row): not run"
    t_done
fi
wait_last_tool "$tmp/c1.sock" run_terminal_command > /dev/null
# The first cancel (cell 1) and the immediate second one (cell 4), both over
# the raw socket: the shipped client refuses the verb before connecting.
cs "$tmp/c1.sock" '{"v":1,"type":"cancel-cmd","reason":"fixture"}' > "$tmp/c1.r1" 2>&1
cs "$tmp/c1.sock" '{"v":1,"type":"cancel-cmd","reason":"fixture"}' > "$tmp/c1.r2" 2>&1
finish_lead_ctl
CAP1=$CAP; OUT1=$OUT; RC1=$RUN_RC

# --- cell 1 -----------------------------------------------------------------
R1N=$(grep -l "$NOTE" "$CAP1"/cap/req.* 2>/dev/null | head -1)
R1N=${R1N:-}
if grep -q 'unknown command' "$tmp/c1.r1"; then
    t_fail "cell 1 (lead target, no task): RED by construction -- the cancel-cmd verb does not exist, and the server's reply is the codec's 'unknown command' refusal: $(head_bytes 200 "$tmp/c1.r1")"
else
    # GREEN (wave C): the reply is ok; req.2's tool result carries the cancel
    # note with exit status 143 (D2: 143, NOT 137); the run completes cleanly
    # and the call's elapsed time is a few seconds, not the config's 20 s
    # runTimeout.
    # (M1195 a) MEASURED, not defaulted: a non-enveloped headless -q --auto -p
    # run never writes ~/.jichi.d/runs/, so the old grep there came up empty
    # and DUR defaulted to 0 -- GREEN BY ABSENCE. The telemetry JSONL
    # (~/.jichi.d/telemetry/<ws>-<key>.jsonl) is what this run ACTUALLY writes
    # (metrics level is the default), and its tool_call line carries the
    # call's duration_ms. We require the value to be PRESENT and a real,
    # positive measurement (< 10000), so a defaulted 0 can no longer read
    # as a pass.
    TELF=$(ls "$HOME/.jichi.d/telemetry"/*-*.jsonl 2>/dev/null \
        | grep -v -F -x -f "$TEL_PRE" | head -1)
    DUR=""
    if [ -n "$TELF" ] && [ -f "$TELF" ]; then
        DUR=$(grep '"event":"tool_call"' "$TELF" 2>/dev/null \
            | grep '"name":"run_terminal_command"' \
            | grep -o '"duration_ms":[0-9.]*' | head -1)
        DUR=${DUR#*:}
    fi
    DUR_MEASURED=0
    [ -n "$DUR" ] && num_gt "$DUR" 0 && DUR_MEASURED=1
    if [ -n "$R1N" ] && grep -q 'exit status: 143' "$R1N" \
       && [ "$RC1" -eq 0 ] && grep -q 'CC-DONE' "$OUT1" \
       && [ "$DUR_MEASURED" -eq 1 ] && num_lt "$DUR" 10000; then
        t_ok "cell 1 (lead target, no task): the reply is ok, the next request carries the cancel note with exit status 143 (not 137), the run completes cleanly and the call took ${DUR} ms MEASURED from the telemetry tool_call row (a few seconds, not the 20 s runTimeout)"
    else
        t_fail "cell 1 green assertions: note in ${R1N:-no captured request} (want the cancel note + 'exit status: 143'), rc=$RC1 (want 0), telemetry file ${TELF:-none} (want the run's ~/.jichi.d/telemetry row), call duration '${DUR:-ABSENT}' ms (want a MEASURED value > 0 and < 10000, NOT a defaulted 0 / absent): $(head_bytes 200 "$tmp/c1.r1")"
    fi
fi

# ============================================================================
# --- cell 2: the WORKER's command (a batch of ONE write task) -----------------
# The child must be write:true (a read-only child fences run_terminal_command,
# "tool disabled in read-only mode", and could never run the sleep the cell
# cancels); it runs in a git worktree, so the lead's workspace is a repo with
# one committed seed (the parallel drivers' idiom). The lead's first reply
# spawns the batch; the child's first request runs `sleep 30` (the child
# inherits the config's "runTimeout": 20, the watched path); the child's next
# request (carries the tool result) -> its closing text. The mock's GLOBAL
# capture ($CAP/cap/req.N) witnesses every request of both processes,
# attributable by the child's task prompt CCW2 (M1149's dump-requests note:
# the mock capture, not --dump-requests, for a fork pool).
cat > "$tmp/c2.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CCW2 zero","write":true}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CCW2"
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"sleep 30"}
rule
  match "CCW2"
  match "\"role\":\"tool\""
  text CHILD-DONE
EOF
start_lead_ctl "$tmp/c2.mm" "$EXTRA" "$tmp/c2.sock" git
if ! wait_sock "$tmp/c2.sock"; then
    finish_lead_ctl
    t_fail "cell 2 (worker target, task 0): the control socket never appeared"
else
    ctl_wait_batch "$tmp/c2.sock" > /dev/null
    cs "$tmp/c2.sock" '{"v":1,"type":"cancel-cmd","task":0,"reason":"fixture"}' > "$tmp/c2.r" 2>&1
    if grep -q 'unknown command' "$tmp/c2.r"; then
        # RED teardown: the un-cancelled child's sleep would hold the batch to
        # the child's own 20 s runTimeout -- an abort (a known verb) stops the
        # pool at its next ~200 ms wake, so the assertion above stands and the
        # cell ends in seconds.
        cs_abort "$tmp/c2.sock"
    fi
    finish_lead_ctl
    # The child's request that carries the cancel note (green only).
    R2N=""
    for f in "$CAP"/cap/req.*; do
        [ -f "$f" ] || continue
        grep -q 'CCW2' "$f" 2>/dev/null || continue
        grep -q "$NOTE" "$f" 2>/dev/null && R2N=$f
    done
    if grep -q 'unknown command' "$tmp/c2.r"; then
        t_fail "cell 2 (worker target, task 0): RED by construction -- the cancel-cmd verb does not exist, and the server's reply (served by the pool's wake while the batch is live) is the codec's 'unknown command' refusal: $(head_bytes 200 "$tmp/c2.r")"
    else
        # GREEN (wave C): the reply is ok naming task 0; the child's next
        # request carries the cancel note with exit status 143 (D2); the batch
        # completes and the lead answers (LEAD-DONE).
        if printf '%s' "$(cat "$tmp/c2.r")" | grep -q '"ok":true' \
           && grep -q 'task 0' "$tmp/c2.r" \
           && [ -n "$R2N" ] && grep -q 'exit status: 143' "$R2N" \
           && [ "$RUN_RC" -eq 0 ] && grep -q 'LEAD-DONE' "$OUT"; then
            t_ok "cell 2 (worker target, task 0): the reply is ok naming task 0, the child's next request carries the cancel note with exit status 143 (not 137), and the batch completes (the lead answers LEAD-DONE)"
        else
            t_fail "cell 2 green assertions: reply ok+task0=$(head_bytes 160 "$tmp/c2.r"); note in ${R2N:-no CCW2 request} (want the cancel note + 'exit status: 143'); rc=$RUN_RC (want 0); LEAD-DONE present=$([ -f "$OUT" ] && grep -c 'LEAD-DONE' "$OUT" 2>/dev/null)"
        fi
    fi
fi

# ============================================================================
# --- cell 3: REFUSAL BY NAME, no batch (M1149 cell 12(b)'s pool-ops-NULL -----
# --- shape -- see the header's measured serve-point deviation) ---------------
# A lead that never spawns; its only tool is a `sleep 5` (served mid-tool
# every ~300 ms), so the run self-terminates in seconds on any build. The
# task-form cancel must be refused with the EXACT "no batch is live" (the
# pool-ops-NULL shape of M1149's stop-task/inject-task) once the verb exists;
# today the codec refuses it as unknown before any handler runs.
cat > "$tmp/c3.mm" <<'EOF'
wire openai
rule
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"sleep 5"}
rule
  match "\"role\":\"tool\""
  text CC3-DONE
EOF
start_lead_ctl "$tmp/c3.mm" "$EXTRA" "$tmp/c3.sock"
if ! wait_sock "$tmp/c3.sock"; then
    finish_lead_ctl
    t_fail "cell 3 (no batch, refusal by name): the control socket never appeared"
else
    wait_last_tool "$tmp/c3.sock" run_terminal_command > /dev/null
    cs "$tmp/c3.sock" '{"v":1,"type":"cancel-cmd","task":0}' > "$tmp/c3.r" 2>&1
    finish_lead_ctl
    if grep -q 'unknown command' "$tmp/c3.r"; then
        t_fail "cell 3 (no batch, refusal by name): RED by construction -- the cancel-cmd verb does not exist, and the server's reply is the codec's 'unknown command' refusal (not the handler's 'no batch is live'): $(head_bytes 200 "$tmp/c3.r")"
    elif grep -q 'no batch is live' "$tmp/c3.r"; then
        t_ok "cell 3 (no batch, refusal by name): the server refuses with the exact 'no batch is live' (the pool-ops-NULL shape, like M1149's stop-task/inject-task) and signals nothing"
    else
        t_fail "cell 3: neither the red 'unknown command' nor the green 'no batch is live' (reply: $(head_bytes 200 "$tmp/c3.r"))"
    fi
fi

# ============================================================================
# --- cell 4: TWO CANCELS (cell 1's run, the data already gathered) ------------
# GREEN: the first cancel lands (the sleep is killed in seconds) and the run
# is between calls -- the second cancel is refused as no-command-running by
# EFFECT (B3.7's lead-form refusal), the next request is the closing text,
# and there is exactly ONE cancel note in the captured requests (no second
# 143). RED: the first reply is already 'unknown command' (the verb does not
# exist), so the second is refused by the codec too -- both replies named.
if grep -q 'unknown command' "$tmp/c1.r1" || grep -q 'unknown command' "$tmp/c1.r2"; then
    t_fail "cell 4 (two cancels): RED by construction -- the cancel-cmd verb does not exist, and both cancels get the codec's 'unknown command' (r1: $(head_bytes 160 "$tmp/c1.r1") | r2: $(head_bytes 160 "$tmp/c1.r2"))"
else
    NC=0
    for f in "$CAP1"/cap/req.*; do
        [ -f "$f" ] || continue
        grep -q "$NOTE" "$f" 2>/dev/null && NC=$((NC + 1))
    done
    if grep -q 'no command is running' "$tmp/c1.r2" && [ "$NC" -eq 1 ] \
       && [ "$RC1" -eq 0 ] && grep -q 'CC-DONE' "$OUT1"; then
        t_ok "cell 4 (two cancels): the second cancel is refused by EFFECT as no-command-running (the run was between calls), the next request is the closing text, and exactly one request carries the cancel note (no second 143)"
    else
        t_fail "cell 4 green assertions: r2 (want 'no command is running'): $(head_bytes 160 "$tmp/c1.r2"); cancel notes in $NC captured requests (want exactly 1); rc=$RC1 (want 0); closing text present=$([ -f "$OUT1" ] && grep -c 'CC-DONE' "$OUT1" 2>/dev/null)"
    fi
fi

# ============================================================================
# --- cell 5 (M1195 b): the fourth refusal -- 'not in a command' ---------------
# A batch of ONE write child (cell 2's idiom: git workspace, spawn_parallel).
# The child's first command is SHORT (`sleep 1`) and its second mock reply is
# HELD (`delay 12000`) -- so after the sleep returns, the child is live and
# BETWEEN commands (waiting on its next model reply) for ~12 s: a real gap to
# poll. In that gap the child's per-task status shows in_cmd == 0 with the
# tool still set (cur_tool survives the in_cmd:0 line; the parent stores it at
# every tool line, jc_tool_parallel.c). The cell then sends cancel-cmd task 0
# and asserts the EXACT fourth refusal of M1192's note B3.7:
#   'task 0 is not in a command (tool: run_terminal_command)'.
# RED BEFORE the in_cmd serialization (change 1): the per-task object has no
# in_cmd key, the poll reads null, the gap is never observed, the cancel is
# never sent in the window, and the cell fails. The cell NEVER waits for
# in_cmd == 1 (the child's first command is 1 s; waiting for it would race
# the command start, and the refusal arm is precisely in_cmd == 0).
cat > "$tmp/c5.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CCW5 zero","write":true}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CCW5"
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"sleep 1"}
rule
  match "CCW5"
  match "\"role\":\"tool\""
  delay 12000
  text CHILD-DONE
EOF
start_lead_ctl "$tmp/c5.mm" "$EXTRA" "$tmp/c5.sock" git
if ! wait_sock "$tmp/c5.sock"; then
    finish_lead_ctl
    t_fail "cell 5 (not in a command, refusal by effect): the control socket never appeared"
else
    ctl_wait_batch "$tmp/c5.sock" > /dev/null
    # Poll for the between-commands gap: in_cmd == 0 AND the tool set.
    C5_GAP=0; C5_TOOL=""; C5_IC=""; C5_i=0
    while [ "$C5_i" -lt 40 ]; do
        C5_S=$(cs_status "$tmp/c5.sock")
        C5_IC=$(printf '%s' "$C5_S" | "$JQ" '.batch.tasks[0].in_cmd' 2>/dev/null)
        C5_TOOL=$(printf '%s' "$C5_S" | "$JQ" '.batch.tasks[0].tool' 2>/dev/null)
        if [ "$C5_IC" = "0" ] && [ -n "$C5_TOOL" ] && [ "$C5_TOOL" != "null" ]; then
            C5_GAP=1
            break
        fi
        C5_i=$((C5_i + 1))
        sleep 0.5
    done
    if [ "$C5_GAP" -eq 1 ]; then
        cs "$tmp/c5.sock" '{"v":1,"type":"cancel-cmd","task":0,"reason":"fixture"}' > "$tmp/c5.r" 2>&1
    fi
    cs_abort "$tmp/c5.sock"
    finish_lead_ctl
    if [ "$C5_GAP" -eq 0 ]; then
        t_fail "cell 5 (not in a command, refusal by effect): the between-commands gap was never observed polling .batch.tasks[0].in_cmd == 0 + tool set -- in_cmd read '${C5_IC:-null}' tool '${C5_TOOL:-null}' (RED without the in_cmd serialization: the status carries no in_cmd key, so the poll reads null and the window is unobservable): $(head_bytes 200 "$tmp/c5.r")"
    elif grep -q 'unknown command' "$tmp/c5.r"; then
        t_fail "cell 5 (not in a command, refusal by effect): the gap WAS observed (in_cmd 0, tool ${C5_TOOL}) but the reply is the codec's 'unknown command' refusal, not the handler's: $(head_bytes 200 "$tmp/c5.r")"
    elif grep -q 'task 0 is not in a command (tool: run_terminal_command)' "$tmp/c5.r"; then
        t_ok "cell 5 (not in a command, refusal by effect): the gap was observed (in_cmd 0, tool ${C5_TOOL}) and cancel-cmd task 0 in that window is refused with the EXACT fourth refusal 'task 0 is not in a command (tool: run_terminal_command)' (no signal, no note): $(head_bytes 160 "$tmp/c5.r")"
    else
        t_fail "cell 5 (not in a command, refusal by effect): the gap was observed (in_cmd 0, tool ${C5_TOOL}) but the reply is neither the named refusal nor unknown: $(head_bytes 200 "$tmp/c5.r")"
    fi
fi

# ============================================================================
# --- cell 6 (M1195 c): the landed cancel produced the telemetry row ----------
# Reuses cell 1's run (the lead's sleep 30 cancelled in seconds: the note in
# the next request + exit 143, rc 0, CC-DONE). The run's OWN telemetry file
# (~/.jichi.d/telemetry/<ws>-<key>.jsonl, written by default at the metrics
# level -- no --log flag) must carry the tool_call line with
# "stopped":"cancel" (the emitter, jc_agent.c, which fires for a shell tool at
# exit_status == 143) -- the emitter -> file path end to end. The offline
# `jichi telemetry <file>` summarizer (run in the run's workspace so the
# default-path pick agrees) must then render the reader's conditional line:
# 'of which 1 were cancelled by the supervisor (control cancel-cmd, exit 143)'
# (jc_telemetry.c cancelled++). This cell's teeth are the raw JSONL assertion;
# the rendered line is the cheap reader check beside it.
# Which telemetry file this driver created? The run's own file is named
# <ws-base>-<key>.jsonl under ~/.jichi.d/telemetry (written by default at the
# metrics level -- no --log flag). We do NOT glob by the workspace's basename
# (the key in the name is a 64-hex digest we cannot recompute from the shell,
# and the base is derived from the CANONICAL path, which need not equal the
# fixture's basename on every host); instead we take the files created since
# TEL_PRE and find the one carrying the landed cancel's tool_call row.
TELF6=""
for _f in "$HOME/.jichi.d/telemetry"/*-*.jsonl; do
    [ -f "$_f" ] || continue
    _b=${_f##*/}
    grep -Fxq "$_b" "$TEL_PRE" && continue   # existed before this driver
    # The landed cancel's file is the one carrying the stopped:cancel row
    # (cell 1 ran first and created its file first, so a plain first-new-file
    # would also do; requiring the row keeps the cell honest if the order
    # ever changes).
    if grep '"event":"tool_call"' "$_f" 2>/dev/null \
       | grep '"name":"run_terminal_command"' \
       | grep -q '"stopped":"cancel"'; then
        TELF6=$_f
        break
    fi
done
if [ -z "$TELF6" ]; then
    t_fail "cell 6 (telemetry stopped:cancel row): no telemetry file this driver created carries the landed cancel's tool_call row with \"stopped\":\"cancel\" (want one under ~/.jichi.d/telemetry, written by default at the metrics level). dir=$(ls "$HOME/.jichi.d/telemetry" 2>&1)"
else
    RAW6=$(grep '"event":"tool_call"' "$TELF6" 2>/dev/null \
        | grep '"name":"run_terminal_command"' \
        | grep '"stopped":"cancel"')
    ( cd "$WS1" && "$BIN" telemetry "$TELF6" > "$tmp/c6.sum" 2>&1 )
    if printf '%s' "$RAW6" | grep -q '"stopped":"cancel"' \
       && grep -q 'of which 1 were cancelled by the supervisor' "$tmp/c6.sum" \
       && grep -q '(control cancel-cmd, exit 143)' "$tmp/c6.sum"; then
        t_ok "cell 6 (telemetry stopped:cancel row): the landed cancel produced the telemetry row -- $(printf '%s\n' "$RAW6" | grep -o '"stopped":"cancel"' | head -1) on the run_terminal_command tool_call line of the run's own file, and the offline summary renders 'of which 1 were cancelled by the supervisor (control cancel-cmd, exit 143)'"
    else
        t_fail "cell 6 (telemetry stopped:cancel row): file $TELF6; raw stopped:cancel line present=$([ -n "$RAW6" ] && printf yes || printf NO) (want a tool_call row with \"stopped\":\"cancel\"); rendered line present=$([ -f "$tmp/c6.sum" ] && grep -c 'of which 1 were cancelled by the supervisor' "$tmp/c6.sum" 2>/dev/null): $(head_bytes 300 "$tmp/c6.sum")"
    fi
fi

t_done
