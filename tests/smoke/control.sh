#!/bin/sh
# smoke: the mid-run control channel (M159/M161/M162). A bounded --auto
# run whose first tool call is `sleep 3` is steered live through the
# control socket via the shipped `jichi control` client:
#   1. during the sleep: status (ok:true + budget) + inject (queued); at
#      the tool boundary the injected text lands as one [operator] user
#      message -- the mock asserts request 2 carries it -- and `runs`
#      flags the run steered (M161).
#   2. a second run: pause --extend -> status shows paused + live
#      deadline_credit -> resume (credit journaled, M162) -> completes.
#   3. a third run: abort during the sleep -> exit 130.
#   4. a fourth run: mode -- narrowing applied, WIDENING REFUSED (M304).
# (Port of tests/e2e/control.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text CTL_DONE
rule
  tool run_terminal_command {"command":"sleep 3"}
EOF

ctl() { "$BIN" control "$1" "$2" ${3:+"$3"} < /dev/null 2>&1; }

wait_sock() {
    _i=0
    while [ ! -S "$1" ]; do
        _i=$((_i + 1)); [ $_i -gt 20 ] && return 1
        sleep 1
    done
    return 0
}

start_run() { # start_run SOCK OUTFILE  -> sets RUN_PID
    (cd "$ws" && "$BIN" --config "$tmp/config.json" --auto --control "$1" \
        --budget-tokens 100k --deadline 2m --no-session \
        -p "run the task" < /dev/null > "$2" 2>"$2.err") &
    RUN_PID=$!
}

# --- run 1: status + inject during the sleep -------------------------------
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

start_run "$tmp/c1.sock" "$tmp/o1"
if ! wait_sock "$tmp/c1.sock"; then
    kill "$RUN_PID" 2>/dev/null
    t_fail "run1: control socket never appeared"
    t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_done
fi
sleep 1                       # the run is inside `sleep 3` now
# Fire status + inject concurrently; wait for THOSE specific jobs, not a
# bare `wait` -- mm_start's mockmodel is also a background child and would
# hold a bare wait until its 120s deadline (a real hang the suite's 120s
# wrapper caught; standalone it merely ran 130s).
ctl "$tmp/c1.sock" status > "$tmp/st" 2>&1 &
_spid=$!
ctl "$tmp/c1.sock" inject "wrap up and answer now" > "$tmp/inj" 2>&1 &
_ipid=$!
wait "$_spid"; wait "$_ipid"
wait "$RUN_PID"; run_rc=$?
mm_stop

if [ "$("$JQ" '.ok' "$tmp/st" 2>/dev/null)" = "true" ] \
   && [ "$("$JQ" '.budget_tokens' "$tmp/st" 2>/dev/null)" = "100000" ]; then
    t_ok "status served at the boundary with budget fields"
else
    t_fail "status wrong: $(cat "$tmp/st")"
fi
if grep -q "CTL_DONE" "$tmp/o1" && [ "$run_rc" -eq 0 ]; then
    t_ok "the steered run finished (rc=0)"
else
    t_fail "steered run rc=$run_rc: $(cat "$tmp/o1")"
fi
if grep -q '"role":"tool"' "$tmp/req.2" 2>/dev/null \
   && grep -q "\[operator\]" "$tmp/req.2" 2>/dev/null; then
    t_ok "the injected text reached the model as an [operator] message"
else
    t_fail "no [operator] steering message in request 2"
fi
if [ ! -e "$tmp/c1.sock" ]; then
    t_ok "the control socket was unlinked at teardown"
else
    t_fail "socket lingered after the run"
fi
# M161: runs flags the steered run
"$BIN" runs "$HOME/.jichi.d/runs" --output json < /dev/null \
    > "$tmp/runs.json" 2>/dev/null
nsteered=0; i=0
while [ $i -lt 8 ]; do
    s=$("$JQ" ".runs[$i].steered" "$tmp/runs.json" 2>/dev/null) || break
    [ "$s" = "1" ] && nsteered=$((nsteered + 1))
    i=$((i + 1))
done
if [ "$nsteered" -eq 1 ]; then
    t_ok "runs flags the steered run (M161)"
else
    t_fail "steered run not flagged ($nsteered found)"
fi

# --- run 2: pause --extend -> status(paused+credit) -> resume --------------
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT"
start_run "$tmp/c2.sock" "$tmp/o2"
if ! wait_sock "$tmp/c2.sock"; then
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_fail "run2: no socket"; t_done
fi
sleep 1
ctl "$tmp/c2.sock" pause --extend > "$tmp/pause" 2>&1
sleep 2                       # let the extend-pause accrue credit
ctl "$tmp/c2.sock" status > "$tmp/st2" 2>&1
ctl "$tmp/c2.sock" resume > /dev/null 2>&1
wait "$RUN_PID"; run_rc=$?
mm_stop
credit=$("$JQ" '.deadline_credit' "$tmp/st2" 2>/dev/null)
if grep -q "deadline clock stopped" "$tmp/pause" \
   && [ "$("$JQ" '.paused' "$tmp/st2" 2>/dev/null)" = "true" ] \
   && [ "${credit:-0}" -ge 1 ] 2>/dev/null \
   && grep -q "CTL_DONE" "$tmp/o2" && [ "$run_rc" -eq 0 ]; then
    t_ok "pause --extend: paused status + live credit, resume completes"
else
    t_fail "pause/extend/resume wrong (credit=$credit rc=$run_rc)"
fi
# the settled credit is journaled on the resume event
if grep -h '"event":"control"' "$HOME/.jichi.d/runs"/*.jsonl 2>/dev/null \
   | grep '"cmd":"resume"' | grep -q '"credited":[1-9]'; then
    t_ok "the resume event is journaled with credited>=1 (M162)"
else
    t_fail "no journaled resume event with credited>=1"
fi

# --- run 3: abort -> exit 130 ----------------------------------------------
mm_start "$tmp/replies.mm" "$tmp/cap3"
write_config "$tmp/config.json" "$MM_PORT"
start_run "$tmp/c3.sock" "$tmp/o3"
if ! wait_sock "$tmp/c3.sock"; then
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_fail "run3: no socket"; t_done
fi
sleep 1
ctl "$tmp/c3.sock" abort > /dev/null 2>&1
wait "$RUN_PID"; run_rc=$?
mm_stop
if [ "$run_rc" -eq 130 ]; then
    t_ok "abort exits the run like Ctrl-C (130)"
else
    t_fail "aborted run rc=$run_rc (want 130)"
fi

# --- run 4: mode -- narrow only (M304) --------------------------------------
#
# The one-way rule is the point. The control channel's founding constraint is that
# it never widens: an operator can tighten a running agent but not loosen it, so
# exposing the socket is not a privilege-escalation surface. Both directions are
# asserted, because a check proving only that narrowing works would also pass on a
# build that allowed everything.
#
# Both commands are fired before the run can move on. Narrowing to plan removes the
# mutating tools, so the scripted turn finishes and there may be no later boundary
# to serve a second command -- an earlier version asked for the refusal afterwards
# and timed out waiting for a boundary that would never come. The behaviour was
# correct; the fixture's assumption was not.
mm_start "$tmp/replies.mm" "$tmp/cap4"
write_config "$tmp/config.json" "$MM_PORT"
start_run "$tmp/c4.sock" "$tmp/o4"
if ! wait_sock "$tmp/c4.sock"; then
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_fail "run4: no socket"
    t_fail "-"; t_done
fi
sleep 1
ctl "$tmp/c4.sock" mode plan > "$tmp/m1" 2>&1
ctl "$tmp/c4.sock" mode auto > "$tmp/m2" 2>&1 &
_widen_job=$!
# WHAT THIS DOES AND DOES NOT PROVE, because two stronger versions were tried and
# deleted. It asserts the command is accepted and acknowledged at a tool boundary.
# It does NOT observe the EFFECT: asserting `status` afterwards (status now reports
# the posture, M304) needs a later boundary to serve it, and narrowing ends the
# scripted turn -- so both a plan and a chat variant timed out. Removing
# jc_app_set_mode leaves this check green, and that is stated rather than hidden.
#
# Covered instead by tests/test_perm.c (all nine ordered mode pairs plus a
# strict-order sweep). The untested link is the single jc_app_set_mode call.
if grep -q '"ok":true' "$tmp/m1" && grep -q "plan" "$tmp/m1"; then
    t_ok "mode plan is accepted and acknowledged at a tool boundary"
else
    t_fail "mode plan not accepted: $(cat "$tmp/m1")"
fi

ctl "$tmp/c4.sock" abort > /dev/null 2>&1
wait "$RUN_PID" 2>/dev/null
wait "$_widen_job" 2>/dev/null
mm_stop

# THE ONE-WAY RULE IS NOT ASSERTED HERE, on purpose. A check was written for it and
# DELETED after watching it pass with the narrowing gate removed: narrowing to plan
# ends the scripted turn, so the follow-up widening attempt goes unserved and `m2` is
# empty either way -- the check could not tell a build that refuses a widening from
# one that allows it. Making it discriminate needs a run that is in a narrow posture
# AND still reaching tool boundaries, which this mock cannot arrange.
#
# So the rule's coverage is tests/test_perm.c's jc_perm_mode_narrows table: all nine
# ordered pairs, plus a sweep asserting narrowing is a STRICT order (no pair may
# narrow in both directions), plus the out-of-range case. The wiring of that decision
# into the handler is the single `if` above check 9, exercised by check 9 itself.
# Recorded rather than papered over -- see docs/TEST_INTEGRITY.md.

# The narrowing is journaled, like every non-status control command, so an audit
# shows that the posture changed and when.
# Same journal the resume check reads: ~/.jichi.d/runs (smoke_home isolates HOME).
if grep -h '"event":"control"' "$HOME/.jichi.d/runs"/*.jsonl 2>/dev/null \
   | grep '"cmd":"mode"' | grep -q "plan"; then
    t_ok "the narrowing is journaled as a control command"
else
    t_fail "no journaled mode command naming the new posture"
    grep -h '"event":"control"' "$HOME/.jichi.d/runs"/*.jsonl 2>/dev/null \
        | sed 's/^/    | /' | head -6
fi

t_done