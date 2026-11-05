#!/bin/sh
# smoke: with no model configured, jichi refuses a prompt and EXITS -- it does not
# crash on the way out (M758).
#
# THE DEFECT. Since M718 the program may run without a provider: the TUI's local
# commands need none, and a turn refuses a prompt it cannot send. Teardown did
# not know that, and called the provider's free through a NULL pointer. So the
# first thing a fresh install did -- `jichi`, then /exit, or `jichi -p hi` -- ended
# in SIGSEGV (exit 139), after the correct refusal had been printed. Found by
# the driver for `jichi ps`, written next, whose first red run exited 139
# instead of 1.
#
# WHY NOTHING SAW IT. The smoke tier runs the TUI on an empty config all the time,
# but ptydrive's `waitexit` recorded the child's status and nothing read it unless
# an `assertexit` followed -- and 27 of the 36 drivers that wait for an exit
# assert none. ptydrive now fails a script whose child died by a signal the script
# did not assert (exit 7); check 3 holds that rule to its word.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# --- 1: a prompt with no config at all is refused, and the run EXITS ---------------
(cd "$ws" && with_deadline 60 "$BIN" --no-session -p "hi" < /dev/null \
    > "$tmp/p.out" 2>&1); rc1=$?
if [ "$rc1" -ne 0 ] && [ "$rc1" -lt 128 ] \
   && grep -q 'no model is configured' "$tmp/p.out"; then
    t_ok "with no model configured, -p is refused and exits $rc1 (no signal)"
else
    t_fail "-p with no model configured (rc=$rc1, 128+ is a signal): $(tail -c 200 "$tmp/p.out")"
fi

# --- 2: the TUI starts, and /exit leaves with status 0 -------------------------------
cat > "$tmp/t.pd" <<'EOF'
expect "for commands" 15
delay 500
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 --log "$tmp/t.log" \
    "$tmp/t.pd" -- "$BIN" --no-session --no-lite) > "$tmp/t.out" 2>&1; rc2=$?
if [ "$rc2" -eq 0 ]; then
    t_ok "with no model configured, the TUI's /exit exits 0"
else
    t_fail "the TUI with no model configured (ptydrive rc=$rc2): $(tail -c 200 "$tmp/t.out")"
fi

# --- 3: ptydrive fails a child's death by a signal that no assertexit expected --------
# The instrument's own tooth: a child that kills itself with SIGSEGV. The script
# waits for the exit and asserts nothing, the shape of 27 drivers.
cat > "$tmp/s.pd" <<'EOF'
expect "ready" 10
waitexit 10
EOF
"$SMOKE_TOOLS/ptydrive" --deadline 20 "$tmp/s.pd" -- \
    sh -c 'echo ready; sleep 1; kill -SEGV $$' > "$tmp/s.out" 2>&1; rc3=$?
if [ "$rc3" -eq 7 ] && grep -q 'killed by signal' "$tmp/s.out"; then
    t_ok "ptydrive fails a script whose child died by a signal it did not assert (exit 7)"
else
    t_fail "ptydrive let an unasserted SIGSEGV pass (rc=$rc3): $(head_bytes 160 "$tmp/s.out")"
fi

# --- 4: ...and passes one that asserted it ----------------------------------------------
# A driver that MEANS a signal death says so, and is not failed for it.
printf 'expect "ready" 10\nwaitexit 10\nassertexit 139\n' > "$tmp/s2.pd"
"$SMOKE_TOOLS/ptydrive" --deadline 20 "$tmp/s2.pd" -- \
    sh -c 'echo ready; sleep 1; kill -SEGV $$' > "$tmp/s2.out" 2>&1; rc4=$?
if [ "$rc4" -eq 0 ]; then
    t_ok "a script that asserts the signal's status passes"
else
    t_fail "an asserted signal death failed the script (rc=$rc4): $(head_bytes 160 "$tmp/s2.out")"
fi

t_done
