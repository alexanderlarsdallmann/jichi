#!/bin/sh
# smoke: two runs started together on one workspace cannot both take its lease
# (M760).
#
# THE DEFECT, from the operator's twelve questions (plan section 10, "what is
# broken, first"): the lease was taken read-then-write. Two runs started together
# both read "free" and both took it quietly -- so `--lease fail`, which is how a
# supervisor serialises runs, let both through, and `--lease warn` warned neither.
# The window is microseconds wide and never seen by chance, so a FAULT=1 build
# holds it open: JICHI_FAULT_LEASE_PAUSE_MS sleeps between the decision and the
# write. Before the fix that pause is the whole race; after it, the second run
# waits on the take's lock and reads the first one's record.
#
# Each run's first tool is `sleep 2`, so the first run is still alive when the
# second one reads its lease.
. "$(dirname "$0")/_smoke.sh"

"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JICHI_FAULT_LEASE_PAUSE_MS=1500
export JICHI_FAULT_LEASE_PAUSE_MS

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text RACE_DONE
rule
  tool run_terminal_command {"command":"sleep 2"}
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# Two runs on one workspace, started together. exec, so $! is jichi itself (on
# OpenBSD ksh it would otherwise be the subshell) and the pid a refusal names can
# be compared with it.
start_pair() {   # $1 = the lease mode, $2 = a tag for the output files
    (cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto --lease "$1" \
        --budget-tokens 100k --deadline 2m --no-session -p "run the task" \
        < /dev/null > "$tmp/$2-a.out" 2> "$tmp/$2-a.err") &
    pa=$!
    (cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto --lease "$1" \
        --budget-tokens 100k --deadline 2m --no-session -p "run the task" \
        < /dev/null > "$tmp/$2-b.out" 2> "$tmp/$2-b.err") &
    pb=$!
    wait "$pa"; ra=$?
    wait "$pb"; rb=$?
}

# --- --lease fail: one runs, one is refused ------------------------------------------
start_pair fail fail
# 1: exactly one refused (exit 2), the other completed (exit 0)
if { [ "$ra" -eq 2 ] && [ "$rb" -eq 0 ]; } || { [ "$ra" -eq 0 ] && [ "$rb" -eq 2 ]; }; then
    t_ok "under --lease fail, one of two runs started together is refused (exit $ra / $rb)"
else
    t_fail "under --lease fail, two runs started together exited $ra and $rb -- both took the lease"
fi
# 2: ...and the refusal names the run that got in
if [ "$ra" -eq 2 ]; then
    grep -q "pid $pb" "$tmp/fail-a.err"; c2=$?
else
    grep -q "pid $pa" "$tmp/fail-b.err"; c2=$?
fi
if [ "$c2" -eq 0 ]; then
    t_ok "the refusal names the other run's pid"
else
    t_fail "no refusal names the other run (pids $pa $pb): $(tail -c 200 "$tmp/fail-a.err") | $(tail -c 200 "$tmp/fail-b.err")"
fi

# --- --lease warn: both run, one says the tree was already held ------------------------
start_pair warn warn
nwarn=$(grep -l 'ALREADY held by a live run' "$tmp/warn-a.err" "$tmp/warn-b.err" 2>/dev/null | wc -l | tr -d ' ')
# 3: exactly one warned, and both completed
if [ "$nwarn" -eq 1 ] && [ "$ra" -eq 0 ] && [ "$rb" -eq 0 ]; then
    t_ok "under --lease warn, both runs complete and exactly one says the tree is held"
else
    t_fail "under --lease warn: $nwarn warning(s), exits $ra / $rb"
fi
# 4: ...naming the other run
if grep -q "ALREADY held by a live run (pid $pb" "$tmp/warn-a.err" 2>/dev/null \
   || grep -q "ALREADY held by a live run (pid $pa" "$tmp/warn-b.err" 2>/dev/null; then
    t_ok "the warning names the other run's pid"
else
    t_fail "no warning names the other run (pids $pa $pb)"
fi

# --- a take held past the wait: the second run says so and goes on ---------------------
# The lock is polled, not waited on without end, so a holder stopped inside a take
# (SIGSTOP, a debugger) costs the next run 5 s, not the run. Here the first take
# is held 7 s. The second gives up at 5 s, says so, and proceeds as it did before
# the lock existed -- the degradation is the old race, announced.
JICHI_FAULT_LEASE_PAUSE_MS=7000
start_pair warn slow
nslow=$(grep -l 'for more than 5 s' "$tmp/slow-a.err" "$tmp/slow-b.err" 2>/dev/null | wc -l | tr -d ' ')
# 5: exactly one run waited out the lock and said so, and both completed
if [ "$nslow" -eq 1 ] && [ "$ra" -eq 0 ] && [ "$rb" -eq 0 ]; then
    t_ok "a take held past the 5 s wait is announced, and the waiting run goes on"
else
    t_fail "take held past the wait: $nslow announcement(s), exits $ra / $rb"
fi

mm_stop

# 6: nothing is held after all of them have gone, and the take's lock stays beside
# it. The lock file is never removed: removing it would reopen the race it closes.
nlease=$(ls "$HOME/.jichi.d/leases/" 2>/dev/null | grep -c '\.json$')
nlock=$(ls "$HOME/.jichi.d/leases/" 2>/dev/null | grep -c '\.lock$')
if [ "$nlease" -eq 0 ] && [ "$nlock" -eq 1 ]; then
    t_ok "after both runs, no lease is left, and the take's lock file stays"
else
    t_fail "after both runs: $nlease lease(s), $nlock lock file(s): $(ls "$HOME/.jichi.d/leases/" 2>/dev/null | tr '\n' ' ')"
fi

t_done
