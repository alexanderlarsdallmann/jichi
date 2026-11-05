#!/bin/sh
# smoke: a run that never takes a pre-edit checkpoint must still run its
# completion verifier and be judged by it, not claimed green (M1048, G18).
#
# WHY THIS EXISTS. The completion verifier is gated on `snapshotted`
# (src/chat/jc_agent.c:2877): a run that takes no pre-edit checkpoint --
# `snapshots:false`, or a run whose tool calls changed nothing (a text-only
# answer, a read-only loop) -- never executes its verifier, yet still finishes
# as a clean run (JC_ENV_OK). The claim then comes from inference, not from a
# run: main.c:2479 reads `verifier_armed && outcome == JC_ENV_OK` as green,
# the reach footer prints "checked: verify green", and the envelope footer
# prints "[envelope] verified ok" -- while the exit code is 0. A false "verify
# green" is exactly the claim class G18 exists to stop.
#
# THIS DRIVER PROVES THE DEFECT RED ON THE UNFIXED HEAD. Cases 1-3 must be red
# now and green after the fix (the fix ungates the completion verify, records
# `verifier_ran`, and adds a `verified` field to the run journal's `end`
# record). Check 4 is a CONTROL: the existing budget fixture of
# run_outcome_agrees.sh must keep printing "the verifier never reached a
# verdict" (the run stopped first), and it is green on the head. The fix splits
# the not-run half of the reach footer, and a split that collapses the two
# not-run cases into one message would pass 1-3 and break 4.
#
#   1  snapshots OFF, model writes nothing, --verify "false"  -> rc 1, one
#      journal verify record, end.verified false, no false-green footer
#   2  snapshots ON,  model writes nothing, --verify "false"  -> rc 1, one
#      journal verify record, end.verified false, verify FAILED
#   3  snapshots ON,  model writes nothing, --verify "true"   -> rc 0, one
#      journal verify record, end.verified true, verified ok
#   4  CONTROL (budget fixture): a run that stopped first keeps its own
#      not-run message, "the verifier never reached a verdict"
#   5  PERIODIC, snapshots OFF, a FAILING --verify-every: the read-only loop
#      fires the mid-turn cadence on a round where NO mutating tool ran
#      (snapshotted 0) -- the exact state 82c197e0's un-gate made reachable.
#      The journal must now carry a "phase":"periodic" verify record (it was
#      silently skipped before), the run is judged RED by the completion gate
#      (outcome verify_failed), and -- the red did NOT roll back
#      (rolled_back false; there is no checkpoint to restore).
#   6  PERIODIC, snapshots OFF, a PASSING --verify-every: the periodic record
#      is present, green-checkpoint banking stays off (no snapshot), and the
#      run ends green because the check passed, not because it was skipped.
#
# Fixture shape copied from run_outcome_agrees.sh: its run() idiom, the inline
# config with lowResource pinned (smoke_lint requires it of every inline
# driver config), smoke_home, mm_start/mm_stop, and < /dev/null on every
# headless run. The workspace relies on smoke_home -- run_outcome_agrees does
# not `git init` it either; with snapshots on jichi keeps its checkpoint in a
# shadow repo under $HOME (src/snapshot/jc_snapshot.c), and its own vfail
# fixture (snapshots on, a plain tmp workspace, no git init) proves that path.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git is required for the snapshot machinery"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

cat > "$tmp/done.mm" <<'EOF'
wire openai
rule
  text FINISHED
EOF
cat > "$tmp/loop.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  tool read_file {"path":"a.txt"}
EOF
# p2.mm: a read-only loop that makes exactly TWO tool calls, then gives a final
# answer. `count` pins a rule to one 1-based request (mm_core.h), so request 1
# reads, request 2 reads again, and the last rule (no count) answers. The cadence
# --verify-every 2 fires at the end of round 2 (tool_calls 2 - last 0 >= 2), a
# round where NO mutating tool ran, so `snapshotted` stayed 0 -- the exact state
# that was silently skipped before 82c197e0 un-gated the periodic site. A final
# answer exists so the completion verify still runs (and decides the run),
# separating the periodic record from the completion one: the periodic record
# carries "phase":"periodic", the completion one does not.
cat > "$tmp/p2.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"a.txt"}
rule
  count 2
  tool read_file {"path":"a.txt"}
rule
  text FINISHED
EOF

# run_case <label> <script> <snapshots> <flags...>
# One headless run, capturing the exit code, the run journal (--journal),
# stdout (the answer) and stderr (the reach footer + the [envelope] line).
run_case() {
    _l="$1"; _s="$2"; _snap="$3"; shift 3
    _w=$(smoke_tmp)
    printf 'hello\n' > "$_w/a.txt"
    mm_start "$tmp/$_s" "$tmp/cap.$_l"
    # Written inline rather than via write_config, which pins "snapshots":false
    # -- cases 2 and 3 need it true. lowResource is pinned because smoke_lint
    # requires it of every inline driver config: on a low-RAM host auto-lite
    # reshapes the configuration and a reshaped config could change which
    # surface says what.
    cat > "$tmp/$_l.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false,
"snapshots":$_snap,"maxRetries":0}
EOF
    (cd "$_w" && with_deadline 120 "$BIN" --config "$tmp/$_l.json" --no-session \
        --journal "$tmp/$_l.jrnl" \
        "$@" -p "do the thing" < /dev/null > "$tmp/$_l.out" 2> "$tmp/$_l.err")
    echo $? > "$tmp/$_l.rc"
    mm_stop
}

run_case c1 done.mm false --edit-scope '**' --verify "false"
run_case c2 done.mm true  --edit-scope '**' --verify "false"
run_case c3 done.mm true  --edit-scope '**' --verify "true"
run_case c4 loop.mm true  --edit-scope '**' --verify "true" --max-tool-calls 4
# Periodic (--verify-every 2), snapshots OFF, read-only two-call loop (p2.mm):
# p5 = a FAILING gate, p6 = a PASSING gate.
run_case p5 p2.mm false --edit-scope '**' --verify-every 2 --verify "false"
run_case p6 p2.mm false --edit-scope '**' --verify-every 2 --verify "true"

rc() { cat "$tmp/$1.rc" 2>/dev/null; }
# Count the journal's "event":"verify" records (the completion/budget verdict,
# not the "verify" config field or the verify_declined/verify_stuck siblings).
nv() { sed -n 's/.*"event":"verify".*//p' "$tmp/$1.jrnl" 2>/dev/null | wc -l | tr -d ' '; }
# The run journal end record's `verified` fact (true/false/"not_run" after the
# fix; absent on the head). Captured up to the next comma or brace.
ev() { sed -n 's/.*"event":"end".*"verified":\([^,}]*\).*/\1/p' "$tmp/$1.jrnl" 2>/dev/null | head -1; }
# The two footer lines a claim of green lives on, for the diagnostic.
foot() { $G -E '^\[jichi\] checked:|^\[envelope\]' "$tmp/$1.err" 2>/dev/null | tr '\n' ' ' | head_bytes 240; }
# Count the journal's PERIODIC verify records -- the "event":"verify" lines that
# carry "phase":"periodic" (src/chat/jc_agent.c, the --verify-every site). The
# completion site writes "event":"verify" with NO "phase" field, so this pattern
# isolates the mid-turn ones exactly; nv above would count both.
nper() { $G '"event":"verify".*"phase":"periodic"' "$tmp/$1.jrnl" 2>/dev/null | wc -l | tr -d ' '; }
# The run journal end record's outcome (ok | verify_failed | budget_exhausted |
# max_iters | ...). Captured up to the next comma.
eo() { sed -n 's/.*"event":"end".*"outcome":"\([a-z_]*\)".*/\1/p' "$tmp/$1.jrnl" 2>/dev/null | head -1; }
# The end record's rolled_back fact (true|false). The periodic red feeds forward
# and does NOT roll back (there is no green checkpoint to restore), so this must
# be false even when the run ends verify_failed.
erb() { sed -n 's/.*"event":"end".*"rolled_back":\([^,}]*\).*/\1/p' "$tmp/$1.jrnl" 2>/dev/null | head -1; }

# ---- 1: snapshots OFF, model writes nothing, the gate fails ------------------
# A run that takes no checkpoint because snapshots are off must still run its
# verifier and be judged by it: rc 1, one verify record, end.verified false,
# and -- the red part -- no false "verified ok". On the head this case is rc 0
# with zero verify records and a footer that says "checked: verify green".
if [ "$(rc c1)" = 1 ] && [ "$(nv c1)" = 1 ] && [ "$(ev c1)" = false ] &&
   ! $G -q 'checked: verify green' "$tmp/c1.err" &&
   ! $G -q 'verified ok' "$tmp/c1.err"; then
    t_ok "snapshots off + a failing gate ends red, records the run, and does not claim green"
else
    t_fail "snapshots off + failing gate still claimed green: rc=$(rc c1) \
verify-records=$(nv c1) end-verified=[$(ev c1)] footer: $(foot c1) -- a run that \
never took a checkpoint must run its verifier, not infer green from outcome==ok"
fi

# ---- 2: snapshots ON, model writes nothing, the gate fails -------------------
# The other half of the same defect: snapshots are available, but the model
# made no change so no pre-edit checkpoint was taken, snapshotted stayed 0, and
# the completion verify never ran -- while the footer still said green. Must
# end rc 1, one verify record, end.verified false, and report verify FAILED.
if [ "$(rc c2)" = 1 ] && [ "$(nv c2)" = 1 ] && [ "$(ev c2)" = false ] &&
   { $G -q 'verify FAILED' "$tmp/c2.err" || $G -q '\[envelope\] verify_failed' "$tmp/c2.err"; }; then
    t_ok "snapshots on but nothing written + a failing gate ends red and says so"
else
    t_fail "snapshots on, no change, failing gate still claimed green: rc=$(rc c2) \
verify-records=$(nv c2) end-verified=[$(ev c2)] footer: $(foot c2) -- a model's \
'I made no changes, all fine' is the claim class G18 exists to stop"
fi

# ---- 3: snapshots ON, model writes nothing, the gate PASSES ------------------
# A passing gate on a no-change run stays green, but only because the verifier
# actually ran. Red on the head at the record count (0 verify records today).
if [ "$(rc c3)" = 0 ] && [ "$(nv c3)" = 1 ] && [ "$(ev c3)" = true ] &&
   $G -q 'verified ok' "$tmp/c3.err"; then
    t_ok "snapshots on, nothing written, a passing gate ends green and records the run"
else
    t_fail "a passing gate on a no-change run was not recorded as run: rc=$(rc c3) \
verify-records=$(nv c3) end-verified=[$(ev c3)] footer: $(foot c3) -- green must \
come from a check that ran, not from armed-and-ok"
fi

# ---- 4: CONTROL -- a run that stopped first keeps its own not-run message ----
# The budget fixture of run_outcome_agrees.sh: a read-only loop that exhausts
# --max-tool-calls. The verifier is armed but the run stopped first (outcome is
# budget_exhausted, not ok), so the reach footer says "the verifier never
# reached a verdict". This is green on the head and must STAY green after the
# fix splits the not-run half: a split that collapses "stopped first" into the
# new "did not run" message passes 1-3 and breaks this.
if [ "$(rc c4)" = 1 ] && $G -q 'verifier never reached a verdict' "$tmp/c4.err"; then
    t_ok "a run that stopped first keeps its own not-run message (control)"
else
    t_fail "the budget-stopped run lost its stopped-first message: rc=$(rc c4) \
footer: $(foot c4) -- 'the run stopped first' and 'the run completed without \
running the verifier' are two different facts and must not share a sentence"
fi

# ---- 5: PERIODIC verify with snapshots OFF, the gate FAILS (M1048, 82c197e0) --
# The read-only two-call loop (p2.mm) makes tool_calls reach 2 while `snapshotted`
# stays 0 (no mutating tool ran), and --verify-every 2 fires on that round. Before
# 82c197e0 un-gated the periodic site this round was silently skipped (no "phase":
# "periodic" record). Now the check runs without a checkpoint and journaled it:
# measured on this build, p5's journal held, in order,
#   "event":"verify","phase":"periodic","exit":1     (the periodic, red)
#   "event":"verify","exit":1,"retries_left":3       (the completion, no "phase")
#   "event":"end",...,"outcome":"verify_failed","verified":false,"rolled_back":false
# So the observable facts are: at least ONE periodic record was journaled, the run
# ended RED (the completion gate decided it, not an inference), and -- the periodic
# red feeds forward and does NOT roll back (there is no green checkpoint to restore,
# so rolled_back stays false even though the run ended verify_failed).
if [ "$(nper p5)" -ge 1 ] && [ "$(eo p5)" = verify_failed ] &&
   [ "$(ev p5)" = false ] && [ "$(erb p5)" = false ] &&
   ! $G -q 'verified ok' "$tmp/p5.err"; then
    t_ok "a failing periodic verify with snapshots off runs, is journaled, ends red, and does not roll back"
else
    t_fail "the periodic verify without a checkpoint was not proven: \
periodic-records=$(nper p5) outcome=$(eo p5) end-verified=[$(ev p5)] \
rolled_back=[$(erb p5)] footer: $(foot p5) -- the un-gate at 82c197e0 makes the \
mid-turn cadence fire on a snapshotted=0 round; a check that silently skipped it \
(the pre-un-gate state) would show periodic-records=0. The red must feed forward, \
not roll back (no checkpoint to restore), so rolled_back must stay false"
fi

# ---- 6: PERIODIC verify with snapshots OFF, the gate PASSES ------------------
# The other half: a passing periodic check on a no-checkpoint run. The periodic
# record is journaled (the cadence fired), and green-checkpoint BANKING stays off --
# it sits behind jc_snapshot_available, and with snapshots:false there is no
# checkpoint to bank, so rolled_back is trivially false. The run ends green
# (outcome ok) because the check PASSED, not because it was skipped. Measured on
# this build, p6's journal held
#   "event":"verify","phase":"periodic","exit":0,"sanity":"no_count"
#   "event":"verify","exit":0,"retries_left":3,"sanity":"no_count"
#   "event":"end",...,"outcome":"ok","verified":true,"rolled_back":false
# and the envelope footer said "verified ok".
if [ "$(nper p6)" -ge 1 ] && [ "$(eo p6)" = ok ] &&
   [ "$(ev p6)" = true ] && [ "$(erb p6)" = false ] &&
   $G -q 'verified ok' "$tmp/p6.err"; then
    t_ok "a passing periodic verify with snapshots off runs, is journaled, and ends green (no checkpoint banked)"
else
    t_fail "the passing periodic verify without a checkpoint was not proven: \
periodic-records=$(nper p6) outcome=$(eo p6) end-verified=[$(ev p6)] \
rolled_back=[$(erb p6)] footer: $(foot p6) -- green here must come from a check \
that RAN (the periodic record exists), with no checkpoint banked behind it"
fi

t_done
