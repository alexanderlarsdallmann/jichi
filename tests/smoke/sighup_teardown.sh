#!/bin/sh
# smoke: E-03 -- SIGHUP is now handled like SIGTERM (graceful teardown, M988).
#
# THE DEFECT. SIGHUP had no handler; its default action ends jichi without
# running the graceful abort path (the one SIGTERM takes: jc_bg_mgr_free,
# which stops the run's background jobs). A terminal hang-up or a
# supervisor's HUP thus killed jichi mid-turn and left its background
# children -- a dev server, a long sleep -- re-parented to init, holding
# their ports and files. Measured by the review (2026-10-17, headless
# --auto -p, background sleep): after SIGTERM the sleep was gone; after
# SIGHUP it was still running.
#
# THE FIX. on_sigint now handles SIGHUP like SIGTERM: set its own flag
# (NOT term_flag -- the term_flag readers all say "SIGTERM"), restore the
# default disposition (so a second HUP still kills), and let the shared
# abort_flag drive the graceful path. The abort exit code is 129
# (128+SIGHUP), documented in both help texts.
#
# Red-first: with src/main.c from 891d2983 (before any SIGHUP work),
# CHECK 1 ([aborted] marker) and CHECK 3 (the background child reaped)
# are red; CHECK 2 (exit 129) passes both ways, by design -- a default
# SIGHUP kill is also 129, so it is not a tooth.
#
# CHILD COUNTING, the bug behind a false "residual" (withdrawn 2026-10-19
# 15:19, LOG): the first draft counted children with
# `ps -eo args | grep "sleep N" | grep -v grep` -- a pipeline whose own
# grep processes carry "sleep N" in their args, so ps (running
# concurrently in the pipeline) counted the greps, not the child. The
# child was reaped; the instrument was not. The count below scans /proc
# cmdlines directly and cannot match its own pipeline.
#
# Each run is a FRESH jichi against a FRESH mockmodel with a UNIQUE sleep
# duration, so one run's child never counts as another's. The signal
# lands during a 20s mock delay (count 2): jichi is alive, mid-turn, with
# a live background child.
. "$(dirname "$0")/_smoke.sh"
# /proc precondition (before the plan, lint check 20): a rig with no /proc
# has no child counter -- every child check would false-red, so SKIP.
if [ ! -d /proc ]; then
    t_skip "needs /proc to count background children (the child-reaped check has no instrument here) -- the exit-code checks (1, 2) could still run, but a half-plan is worse than a skip"
fi

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# _child_running SLEEPS -- 0 if a process whose /proc cmdline contains
# "sleep SLEEPS" exists, 1 if none does. Scans /proc directly: no ps/grep
# pipeline, so the counter cannot match itself (the false-residual bug,
# above). The probe script's own path does not contain the marker.
_child_running() {
    _found=0
    for _d in /proc/[0-9]*; do
        _cl=$(tr '\0' ' ' < "$_d/cmdline" 2>/dev/null) || continue
        case "$_cl" in
            *"sleep $1"*) _found=1 ;;
        esac
    done
    [ "$_found" -eq 0 ]
}

# _kill_stale SLEEPS -- kill any `sleep SLEEPS` left over from a PREVIOUS
# driver run (an unfixed binary's SIGHUP check correctly leaves one, and it
# would otherwise be miscounted as this run's surviving child). Runs before
# each _run so the post-run liveness reflects only THIS run's child.
_kill_stale() {
    for _d in /proc/[0-9]*; do
        _cl=$(tr '\0' ' ' < "$_d/cmdline" 2>/dev/null) || continue
        case "$_cl" in
            *"sleep $1"*) _p=${_d#/proc/}
                [ "$_p" != "$$" ] && kill -9 "$_p" 2>/dev/null ;;
        esac
    done
}

# _run SIGNAL SLEEPS -- a fresh mockmodel + fresh jichi (no -q, so the
# graceful [aborted] marker reaches stderr). The model's first tool call
# starts a background `sleep SLEEPS`; the mock then delays 20s, so jichi
# is alive, mid-turn, with the child running when the signal lands 4s in.
# Echoes two lines: jichi's exit code, then running=yes|no (child liveness,
# checked 1s after jichi exited -- jc_bg_mgr_free gives 80 ms of grace).
_run() {
    _sig="$1"
    _sleeps="$2"
    _kill_stale "$_sleeps"
    cat > "$tmp/replies_$_sleeps.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sleep $_sleeps","run_in_background":true}
rule
  count 2
  delay 20000
  text E03_DONE
EOF
    mm_start "$tmp/replies_$_sleeps.mm" "$tmp"
    write_config "$tmp/config_$_sleeps.json" "$MM_PORT"
    (cd "$ws" && exec "$BIN" --config "$tmp/config_$_sleeps.json" --no-session \
        --timeout-stall 60 --auto -p "run it in the background" < /dev/null \
        > "$tmp/out_$_sleeps.txt" 2>&1) &
    _pid=$!
    sleep 4
    kill -$_sig "$_pid" 2>/dev/null
    wait "$_pid"
    _rc=$?
    sleep 1
    _run_r="running=no"
    _child_running "$_sleeps" || _run_r="running=yes"
    mm_stop
    echo "$_rc"
    echo "$_run_r"
}

# CHECK 1 (graceful, not default-kill; RED pre-fix): the SIGHUP run takes the
# graceful path -- [aborted] on stderr. A default SIGHUP kill prints nothing.
_res=$(_run HUP 83)
_rc=$(printf '%s\n' "$_res" | sed -n '1p')
_alive=$(printf '%s\n' "$_res" | sed -n '2p')
if grep -q 'aborted' "$tmp/out_83.txt"; then
    t_ok "SIGHUP takes the graceful path ([aborted] marker, rc=$_rc)"
else
    t_fail "SIGHUP left no [aborted] marker -- the default kill ran, not the graceful teardown (rc=$_rc, output: $(head -3 "$tmp/out_83.txt" | tr '\n' ' '))"
fi

# CHECK 2 (exit contract): the graceful SIGHUP run exits 129 (128+SIGHUP).
# Not a tooth on its own (a default kill is also 129); it pins the code the
# handler path reports.
if [ "$_rc" -eq 129 ]; then
    t_ok "SIGHUP exits 129 (128+SIGHUP)"
else
    t_fail "SIGHUP rc=$_rc (want 129)"
fi

# CHECK 3 (THE DEFECT, RED pre-fix): the SIGHUP run's background child must be
# GONE -- reaped by jc_bg_mgr_free in the graceful teardown. Pre-fix (the
# default kill) it survives, re-parented to init.
if [ "$_alive" = "running=no" ]; then
    t_ok "SIGHUP tore down: the background child is gone"
else
    t_fail "SIGHUP left the background child running (re-parented to init) -- teardown did not run"
fi

# CHECK 4 (control, SIGTERM): the graceful path the fix must not regress --
# exit 143, the [aborted] marker, and the child reaped, all as before.
_res=$(_run TERM 84)
_rc=$(printf '%s\n' "$_res" | sed -n '1p')
_alive=$(printf '%s\n' "$_res" | sed -n '2p')
if [ "$_rc" -eq 143 ] && [ "$_alive" = "running=no" ] \
   && grep -q 'aborted' "$tmp/out_84.txt"; then
    t_ok "SIGTERM (control) still exits 143, prints [aborted], and reaps the child"
else
    t_fail "SIGTERM control regressed: rc=$_rc (want 143), child=$_alive (want running=no)"
fi

t_done
