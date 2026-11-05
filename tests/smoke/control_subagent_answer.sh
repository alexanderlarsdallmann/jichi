#!/bin/sh
# smoke: the control channel answers while an IN-PROCESS spawn_subagent runs
# (SV:1848 / M1103).
#
# THE DEFECT. Both service points (jc_control_boundary and
# jc_control_poll_during_call) returned early on app->agent_depth != 0. The
# guard's stated purpose -- "a forked pool child inherits the listening fd
# and must not answer it" -- is right for a FORKED child, wrong for
# spawn_subagent, which raises agent_depth IN-PROCESS in the very process
# that owns the socket. So the whole in-process child run was dark for
# status/pause/abort: the supervisor's 300 s wait expired (exit 3, measured
# 300.35 s dark in run 0107).
#
# THE FIX (the supervisor's shape): the serving guard becomes "am I the
# process that opened the socket" (jc_control.serve_pid, recorded at
# jc_control_open), not agent_depth. The inject FOLD keeps its depth guard:
# steering folds at the top-level boundary only, never into a subagent's
# history.
#
# WHICH WINDOW IS SERVICEABLE. A model call is one blocking curl, with no
# control serve point (on_progress only drives the heartbeat). So the
# serviceable in-child window is the child's TOOL call: its shell wait loop
# (jc_app.c) ticks jc_control_poll_during_call every ~200 ms -- but only when
# on_progress is installed, which `--output jsonl --heartbeat 1` gives the
# child (hb_forward_subagents, jc_tool_subagent.c). The child runs
# `sleep 25`; the four clients fire during that call. On the unfixed
# binary the depth guard darkens it, so every client's 12 s wait expires
# first -> exit 3 "no reply within the wait" (RED). On the fixed binary the
# in-process owner serves each in <1 s (pid matches) -> exit 0 (GREEN).
#
#   1  status fired during the in-process subagent's tool call is answered
#      (ok:true + budget_tokens 100000) instead of the 12 s wait expiring
#   2  pause and resume are both accepted during the same window (control is
#      live mid-subagent; the pause flag is set then cleared, so the child
#      is not left blocked at its boundary)
#   3  an inject accepted in the window folded into the LEAD's next request
#      (INJMARK in the post-child request) and NOT into the child's request
#      (the surviving depth guard on the fold: the child's boundary serves but
#      does not fold, so the note holds for the lead's top-level boundary)
#   4  the run completes rc=0 and the lead's final answer carries
#      CA27-LEAD-END
#
# Mock layout: the lead (prompt "CA27 delegate now") requests
# spawn_subagent {"task":"CA27-SUB sit"} (in-process child, which has the
# shell tool). The child's first request (CA27-SUB) gets
# run_terminal_command {"command":"sleep 25"} -- the dark window. The child's
# second request (CA27-SUB + the tool result) gets text CA27-CONT (the child's
# final answer). The lead's next request contains the child's answer
# CA27-CONT (the spawn_subagent result carries it) and gets CA27-LEAD-END.
# Capture numbering: req.1 lead, req.2 child (sleep), req.3 child (answer),
# req.4 lead (fold lands here).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "CA27-CONT"
  text CA27-LEAD-END
rule
  match "CA27-SUB"
  match "\"role\":\"tool\""
  text CA27-CONT
rule
  match "CA27-SUB"
  tool run_terminal_command {"command":"sleep 25"}
rule
  match "CA27"
  tool spawn_subagent {"task":"CA27-SUB sit"}
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"

# 12 s client cap: the red check. Unfixed -> nothing serves at depth != 0,
# the wait expires, exit 3. Fixed -> the in-process owner serves in <1 s.
ctl12() {
    JC_CONTROL_CLIENT_SECS=12 "$BIN" control "$1" "$2" ${3:+"$3"} < /dev/null 2>&1
}

# The control.sh start_run idiom: a backgrounded subshell jichi, bounded by
# its own --deadline (which the mock's 120 s mm_start --deadline also
# backstops by breaking the lead's stream) and by the suite's per-driver
# deadline (run_driver 120); every gate below kills RUN_PID and mm_stop on
# failure, and the happy path reaches an explicit wait.
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto \
    --output jsonl --heartbeat 1 --control "$tmp/s.sock" \
    --budget-tokens 100k --deadline 2m --no-session \
    -p "CA27 delegate now" < /dev/null > "$tmp/o" 2>"$tmp/o.err") &
RUN_PID=$!

# Gate (not a check): the child's FIRST request (CA27-SUB, no tool result
# yet) has landed -- the lead delegated and the child is about to run
# `sleep 25`. Written on receipt.
_i=0
[ -e "$tmp/cap/req.2" ] || while [ $_i -lt 15 ] && [ ! -e "$tmp/cap/req.2" ]; do
    _i=$((_i + 1)); sleep 0.5
done
if [ ! -e "$tmp/cap/req.2" ] || ! grep -q "CA27-SUB" "$tmp/cap/req.2"; then
    kill "$RUN_PID" 2>/dev/null
    mm_stop
    t_fail "the child's request (CA27-SUB) never reached the mock: $(ls "$tmp/cap" 2>/dev/null)"
    t_fail -; t_fail -; t_fail -
    t_done
fi
sleep 1                       # settle INSIDE the child's `sleep 25` window

# --- the window: four clients, fired in order while the child sleeps -------
# All four are in-window clients (12 s cap). Fired as background jobs with a
# 1 s spacing so the shell loop serves them in order -- pause is served
# (and its flag set) before resume connects, so the child is not left blocked
# at its boundary on the fixed leg. On the unfixed leg each 12 s wait expires
# inside the window (rc 3).
ctl12 "$tmp/s.sock" status > "$tmp/st" 2>&1 &
spid=$!
sleep 1
ctl12 "$tmp/s.sock" inject "INJMARK fold probe" > "$tmp/inj" 2>&1 &
ipid=$!
sleep 1
ctl12 "$tmp/s.sock" pause > "$tmp/pause" 2>&1 &
ppid=$!
sleep 1
ctl12 "$tmp/s.sock" resume > "$tmp/resume" 2>&1 &
rpid=$!
wait "$spid"; st_rc=$?
wait "$ipid"; inj_rc=$?
wait "$ppid"; p_rc=$?
wait "$rpid"; r_rc=$?

wait "$RUN_PID" 2>/dev/null
run_rc=$?
mm_stop

# --- 1: status was answered in-window --------------------------------------
if [ "$st_rc" -eq 0 ] && [ "$("$JQ" '.ok' "$tmp/st" 2>/dev/null)" = "true" ] \
   && [ "$("$JQ" '.budget_tokens' "$tmp/st" 2>/dev/null)" = "100000" ]; then
    t_ok "status fired during the in-process subagent's tool call is answered (ok + budget 100000)"
else
    t_fail "status not served in-window (rc=$st_rc; unfixed: the depth guard darkens the whole child run): $(cat "$tmp/st")"
fi

# --- 2: pause and resume were both accepted in-window ----------------------
if [ "$p_rc" -eq 0 ] && grep -q "paused at tool boundary" "$tmp/pause" \
   && [ "$r_rc" -eq 0 ] && grep -q '"ok":true' "$tmp/resume"; then
    t_ok "pause and resume are accepted during the same window (control is live mid-subagent)"
else
    t_fail "pause/resume not served in-window (p_rc=$p_rc r_rc=$r_rc): pause=$(cat "$tmp/pause") resume=$(cat "$tmp/resume")"
fi

# --- 3: the inject folds at top level, never into the child's history ------
# The inject fired in-window (during the child's `sleep 25`). LEADREQ = the
# lead's post-child request (the only one carrying the child's answer
# CA27-CONT). CHILDRQ = the child's request(s) carrying CA27-SUB. The fold
# lands in LEADREQ at the lead's depth-0 boundary; the child's depth-1
# boundary serves but does NOT fold (the surviving depth guard), so CHILDRQ
# never carries INJMARK. inj_rc is the red discriminator: the in-window
# inject is acked (rc 0) only when the owner serves mid-child.
LEADREQ=$(grep -l "CA27-CONT" "$tmp/cap"/req.* 2>/dev/null | head -1)
CHILDRQ=$(grep -l "CA27-SUB" "$tmp/cap"/req.* 2>/dev/null | head -1)
if [ "$inj_rc" -eq 0 ] && grep -q '"ok":true' "$tmp/inj" \
   && [ -n "$LEADREQ" ] && grep -q "INJMARK" "$LEADREQ" \
   && [ -n "$CHILDRQ" ] && ! grep -q "INJMARK" "$CHILDRQ"; then
    t_ok "the in-window inject folded into the lead's next request, not the child's (the fold stays depth-guarded)"
else
    t_fail "inject fold wrong (inj_rc=$inj_rc lead=${LEADREQ:-none} child=${CHILDRQ:-none}): inject=$(cat "$tmp/inj") lead_has=$(grep -c "INJMARK" "$LEADREQ" 2>/dev/null) child_has=$(grep -c "INJMARK" "$CHILDRQ" 2>/dev/null)"
fi

# --- 4: the run completes and the lead's output carries the final text ----
if [ "$run_rc" -eq 0 ] && grep -q "CA27-LEAD-END" "$tmp/o"; then
    t_ok "the run completed (rc=0) with the lead's final answer"
else
    t_fail "run rc=$run_rc, expected CA27-LEAD-END: $(head_bytes 300 "$tmp/o")"
fi

t_done
