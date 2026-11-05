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
#   5. a fifth run: inject with `from:supervisor` lands as `[supervisor] `
#      (not `[supervisor: supervisor]`) in the model's history (H9).
#   6. a sixth run: the inject delivery state in status (M1045/M1045-b) --
#      the baseline pair is emitted as zeros before any inject,
#      injected=1 / queued_injects=1 after one, and a SECOND inject whose
#      text carries a real newline reads queued_injects=2 (notes, not
#      lines; the shipped ctl client cannot carry one, so sockq does).
#   7. a seventh run: the fold's reset of the note count (M1046) --
#      after two injects fold at the tool boundary, status read during the
#      next tool call says queued_injects=0 while injected stayed 2 (the X2 pin).
# (Port of tests/e2e/control.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 17
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
# M1045-b: the raw-socket note client (run 6's second inject needs it; a
# newline in the note is exactly what the single-line ctl client cannot send).
SOCKQ="$SMOKE_TOOLS/sockq"

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
    t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_done
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
# M752: a verb the codec does not know, sent raw (the shipped client refuses it
# before connecting). The server's reply lists the verbs it serves.
printf '%s\n' '{"v":1,"type":"steer"}' \
    | "$SMOKE_TOOLS/sockq" --deadline 60 "$tmp/c1.sock" > "$tmp/unk" 2>&1 &
_upid=$!
wait "$_spid"; wait "$_ipid"; wait "$_upid"
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

# M752: THE SERVER'S OWN REPLY NAMES EVERY VERB IT SERVES. It listed five by hand,
# "status|inject|pause|resume|abort", and omitted `mode` from M304 to M752. The
# list is the codec's now (jc_control_verb_list). The match is the list's tail
# `abort|mode`, not the word "mode", which the reply's advice also contains.
if grep -q 'unknown command' "$tmp/unk" && grep -q 'abort|mode' "$tmp/unk"; then
    t_ok "an unknown verb's reply lists all six verbs, mode included"
else
    t_fail "unknown-verb reply: $(head_bytes 200 "$tmp/unk")"
fi

# --- run 5: inject with from:supervisor lands as [supervisor] (H9) ---------
# The ctl client does not expose a `from` argument, so we use sockq to send
# raw JSON with `"from":"supervisor"`. The fix changes the prefix from
# `[supervisor: supervisor] ` to `[supervisor] `.
SOCKQ="$SMOKE_TOOLS/sockq"
mm_start "$tmp/replies.mm" "$tmp/cap5"
write_config "$tmp/config.json" "$MM_PORT"
start_run "$tmp/c5.sock" "$tmp/o5"
if ! wait_sock "$tmp/c5.sock"; then
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_fail "run5: no socket"
    t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_fail "-"; t_done
fi
sleep 1
# Send inject with from:supervisor via sockq (raw JSON)
printf '%s\n' '{"v":1,"type":"inject","text":"from supervisor","from":"supervisor"}' \
    | "$SOCKQ" --deadline 60 "$tmp/c5.sock" > "$tmp/inj5" 2>&1 &
_i5pid=$!
wait "$_i5pid"
wait "$RUN_PID"; run_rc=$?
mm_stop
if [ "$run_rc" -eq 0 ] && grep -q "CTL_DONE" "$tmp/o5"; then
    # Check that the injected text appears as [supervisor] (not [supervisor: supervisor])
    # Run 5 captures to cap5, not tmp
    if grep -q '"role":"tool"' "$tmp/cap5/req.2" 2>/dev/null \
       && grep -q "\[supervisor\] " "$tmp/cap5/req.2" 2>/dev/null; then
        t_ok "inject with from:supervisor lands as [supervisor] (H9)"
    else
        t_fail "inject with from:supervisor: expected [supervisor] in cap5/req.2"
        grep "\[supervisor" "$tmp/cap5/req.2" 2>/dev/null | sed 's/^/    | /'
    fi
else
    t_fail "run5 did not complete (rc=$run_rc): $(head_bytes 200 "$tmp/o5")"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_fail "-"
    t_fail "-"
fi

# --- run 6: the inject delivery state in status (M1045/M1045-b) -----------
# `injected` and `queued_injects` are ALWAYS emitted, zero included: a
# supervisor polling from cold must observe the baseline (0/0), and the
# pair is the observable correction for the paused-ack overclaim.
# M1045-b: the second leg injects a note whose text carries a REAL newline
# (a raw-socket note -- the shipped `ctl` client joins single-line args and
# cannot carry one) and re-reads status: `queued_injects` must count NOTES, not
# lines (2, not 3). On the unfixed line-count build the second status reads
# queued_injects=3 (1 line from the first note + 2 from the second) -- the red.
mm_start "$tmp/replies.mm" "$tmp/cap6"
write_config "$tmp/config.json" "$MM_PORT"
start_run "$tmp/c6.sock" "$tmp/o6"
if ! wait_sock "$tmp/c6.sock"; then
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_fail "run6: no socket"; t_done
fi
sleep 1
# Baseline: no inject yet -- the pair must be present and zero, not omitted.
ctl "$tmp/c6.sock" status > "$tmp/st6a" 2>&1
i0=$("$JQ" '.injected' "$tmp/st6a" 2>/dev/null)
q0=$("$JQ" '.queued_injects' "$tmp/st6a" 2>/dev/null)
if [ "$i0" = "0" ] && [ "$q0" = "0" ]; then
    t_ok "baseline status emits injected=0 and queued_injects=0"
else
    t_fail "baseline status wrong (injected=$i0 queued_injects=$q0): $(cat "$tmp/st6a")"
fi
ctl "$tmp/c6.sock" inject "delivery-state probe text" > "$tmp/inj6" 2>&1
if grep -q "applied at next model call" "$tmp/inj6"; then
    t_ok "the inject ack still says applied at next model call"
else
    t_fail "inject ack changed: $(cat "$tmp/inj6")"
fi
ctl "$tmp/c6.sock" status > "$tmp/st6b" 2>&1
i1=$("$JQ" '.injected' "$tmp/st6b" 2>/dev/null)
q1=$("$JQ" '.queued_injects' "$tmp/st6b" 2>/dev/null)
if [ "$i1" = "1" ] && [ "$q1" = "1" ]; then
    t_ok "status after the inject: injected=1 and queued_injects=1"
else
    t_fail "status after inject wrong (injected=$i1 queued_injects=$q1): $(cat "$tmp/st6b")"
fi
# M1045-b: the second leg -- a note with a real newline (raw socket: the
# \n here is the JSON escape; cJSON decodes it to a newline byte, and that
# is precisely the input the old line-counting helper miscounted). Wait for
# the job and check the reply carries ok, as run 5 does with _i5pid.
printf '%s\n' '{"v":1,"type":"inject","text":"second note, first line\nsecond note, second line"}' \
    | "$SOCKQ" --deadline 60 "$tmp/c6.sock" > "$tmp/inj6b" 2>&1 &
_i6bpid=$!
wait "$_i6bpid"
# Gate, not a check: the leg cannot proceed without an accepted second inject.
if ! grep -q '"ok":true' "$tmp/inj6b"; then
    t_fail "second inject not acked: $(cat "$tmp/inj6b")"
    t_done
fi
ctl "$tmp/c6.sock" status > "$tmp/st6c" 2>&1
i2=$("$JQ" '.injected' "$tmp/st6c" 2>/dev/null)
q2=$("$JQ" '.queued_injects' "$tmp/st6c" 2>/dev/null)
if [ "$i2" = "2" ] && [ "$q2" = "2" ]; then
    t_ok "a second two-line note: injected=2 and queued_injects=2 (notes, not lines)"
else
    t_fail "status after the second inject wrong (injected=$i2 queued_injects=$q2): $(cat "$tmp/st6c")"
fi
# abort (the run was never resumed, so the queued note stays queued -- a
# folded-state check would be a false green from the next run's mock).
ctl "$tmp/c6.sock" abort > /dev/null 2>&1
wait "$RUN_PID" 2>/dev/null
mm_stop

# --- run 7: the fold's reset of the note count (M1046) ---------------------
# A dedicated post-fold leg with its own capture (cap7) AND its own reply
# table (replies7.mm), so a resumed leg cannot take the false green from the
# next run's mock (the warning just above the c6 teardown).
#
# WHY A SEPARATE TABLE: the fold lands in REQUEST 2 -- the first post-tool
# request (roles system/user/assistant/tool/[operator]) -- and request 2 must
# be a `sleep 3` so status can be read DURING it. A `count 3` rule would never
# be reached (request 2 matches `match "\"role\":\"tool\""` -> CTL_DONE and
# ends the run, so no request 3 exists), and a `count 2` rule in the SHARED
# replies.mm would turn runs 1-6's request 2 into a sleep (breaking their
# CTL_DONE checks). So run 7 owns its table: req.1 sleep3 (catch-all),
# req.2 sleep3 (count 2), req.3 CTL_DONE (match tool-role). Verified by hand:
# 3 requests, the fold in req.2, status during sleep3#2 -> queued_injects=0.
#
# PLAN ARITHMETIC: runs 1-6 emit 16 checks (T_N=16) before this leg, and this
# leg plans exactly ONE -- the post-fold status (check 17). So every early exit
# that emits a descriptive t_fail lands on check #17 (16+1=17=plan, a clean
# single-fail t_done). The fold gate emits NO t_fail: on gate failure t_done
# reports the plan mismatch (16 ran of 17); a t_fail there would miscount.
# No padding is needed on any path in this leg.
cat > "$tmp/replies7.mm" <<'EOF'
wire openai
rule
  count 2
  tool run_terminal_command {"command":"sleep 3"}
rule
  match "\"role\":\"tool\""
  text CTL_DONE
rule
  tool run_terminal_command {"command":"sleep 3"}
EOF
mm_start "$tmp/replies7.mm" "$tmp/cap7"
write_config "$tmp/config.json" "$MM_PORT"
start_run "$tmp/c7.sock" "$tmp/o7"
if ! wait_sock "$tmp/c7.sock"; then
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_fail "run7: no socket"; t_done
fi
sleep 1                       # the run is inside the first `sleep 3` now
# Inject two one-line notes, each GATED: the fold happens at the boundary
# after the first sleep 3; the gate below asserts BOTH note texts in
# cap7/req.2 (the fold folds the whole buffer into ONE [operator] message), so
# a note acked after the boundary cannot pass it. (A retry can accept a
# duplicate on a re-inject, which is why check 17 allows injected>=2 rather
# than hard-requiring 2.)
_tries=0; _ok=""
while [ "$_tries" -lt 8 ]; do
    ctl "$tmp/c7.sock" inject "x2 pin note A" > "$tmp/inj7a" 2>&1
    if grep -q '"ok":true' "$tmp/inj7a"; then _ok=1; break; fi
    _tries=$((_tries + 1)); sleep 1
done
if [ -z "$_ok" ]; then
    # T_N is 16; this one fail is check #17 -> clean exit (no padding).
    t_fail "run7: inject A not acked: $(cat "$tmp/inj7a")"; t_done
fi
_tries=0; _ok=""
while [ "$_tries" -lt 8 ]; do
    ctl "$tmp/c7.sock" inject "x2 pin note B" > "$tmp/inj7b" 2>&1
    if grep -q '"ok":true' "$tmp/inj7b"; then _ok=1; break; fi
    _tries=$((_tries + 1)); sleep 1
done
if [ -z "$_ok" ]; then
    # T_N is 16; this one fail is check #17 -> clean exit (no padding).
    t_fail "run7: inject B not acked: $(cat "$tmp/inj7b")"; t_done
fi
# GATE the fold (NOT a planned check): request 2 must carry the first note as
# an [operator] message. Retry up to 5 x 1s for cap7/req.2 to exist. On gate
# failure: t_done reports the plan mismatch; a t_fail would miscount.
_i=0
[ -e "$tmp/cap7/req.2" ] || {
    while [ $_i -lt 5 ] && [ ! -e "$tmp/cap7/req.2" ]; do
        _i=$((_i + 1)); sleep 1
    done
}
if [ -e "$tmp/cap7/req.2" ] \
   && grep -q '"role":"tool"' "$tmp/cap7/req.2" \
   && grep -q 'x2 pin note A' "$tmp/cap7/req.2" \
   && grep -q 'x2 pin note B' "$tmp/cap7/req.2" \
   && grep -q '\[operator\]' "$tmp/cap7/req.2"; then
    :
else
    echo "run7 gate failed: fold not observed in cap7/req.2" >> "$tmp/o7.err"
    kill "$RUN_PID" 2>/dev/null; mm_stop; t_done
fi
# The run is inside the SECOND `sleep 3` (request 2's tool call); the fold
# already zeroed pending_notes at the boundary. Read status (sleep 3 is long
# enough; a cold reply missing the fields is re-read once after sleep 1).
ctl "$tmp/c7.sock" status > "$tmp/st7" 2>&1
if [ -z "$("$JQ" '.queued_injects' "$tmp/st7" 2>/dev/null)" ] \
   || [ -z "$("$JQ" '.injected' "$tmp/st7" 2>/dev/null)" ]; then
    sleep 1
    ctl "$tmp/c7.sock" status > "$tmp/st7" 2>&1
fi
i7=$("$JQ" '.injected' "$tmp/st7" 2>/dev/null)
q7=$("$JQ" '.queued_injects' "$tmp/st7" 2>/dev/null)
if [ "$q7" = "0" ] && [ "$i7" -ge 2 ] 2>/dev/null; then
    t_ok "post-fold status: queued_injects=0 while injected=$i7 (the fold reset the note count, M1046)"
else
    t_fail "post-fold status wrong (injected=$i7 queued_injects=$q7, want 0; reset line jc_control.c pending_notes=0): $(cat "$tmp/st7")"
fi
# Teardown (run 6's shape): the run is at its scripted end (req.3 answers
# CTL_DONE); abort is harmless on a closing socket, errors to /dev/null.
ctl "$tmp/c7.sock" abort > /dev/null 2>&1
wait "$RUN_PID" 2>/dev/null
mm_stop

t_done
