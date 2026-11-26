#!/bin/sh
# smoke: "return when the shell exits" (M1192 B1, def-376) -- the RED-FIRST
# driver (wave B). No fix, no code change: B1 (the waitpid(WNOHANG) after each
# select wake, the drain-and-return, and the B1-adopt hand-off to jc_bg) is
# wave C's. This driver pins the measured F2 defect and shows it red.
#
# THE UNIVERSE (what this driver pins) -- three cells, one headless --auto run
# each, a real `sh`, the mock model for the model, all under "runTimeout":20 in
# the config (D7: every model-issued command therefore takes the WATCHED path,
# run_command_watched -- the one every drive uses; no per-call timeout is set
# anywhere):
#   1. THE HELLED CALL (the measured F2 case): the model's first tool call is a
#      backgrounded job that keeps fd 1 of the command's output pipe open for
#      30 s, while the shell itself finishes immediately. Without B1 the call
#      holds until the 20 s runTimeout fires (which also KILLS the job); with
#      B1 the shell's exit is noticed, the call returns promptly, and the job
#      is ADOPTED into a jc_bg spool file (B1-adopt, D1).
#   2. A NORMAL COMMAND (edge, green-invariant): a command that finishes on its
#      own -- B1 changes nothing for it.
#   3. `cmd & wait` (edge, B1 must NOT fire): the shell waits for its job, so
#      it has not exited -- B1.7's no-regression edge.
#
# RED BY CONSTRUCTION (this IS the red). B1 is not in the tree, so cell 1's
# call reads the output pipe to EOF, and EOF does not come until the 20 s
# runTimeout fires (jc_app.c: the deadline branch kills the process group, so
# the backgrounded job DIES with it -- the model loses both the call's time and
# the job). The driver measures `date +%s` around the jichi invocation and
# reads the tool result the model actually sees (mockmodel's captured req.2).
# The measurable red contract is the PRESENCE of the timeout note
# `[stopped: command timed out after 20s and was terminated]` in req.2, with
# exit status 124, an elapsed >= 20 s, the group-kill having taken the job
# down (the unique marker scan finds nothing surviving), and the B1-adopt spool
# file ABSENT. Wave C turns cell 1 green: the call returns in well under 20 s,
# the timeout note is GONE, `started` is present, the shell exits 0, and a
# bg-*.out spool file appears (the adopted job). Cells 2 and 3 pin today's
# correct behaviour and stay green in both states.
#
# THE F2 COMMAND SHAPE (the measured case, docs/internal/
# 2026-11-10-blocked-foreground-calls.md F2: 7 calls held 10 min or more, 3.7 h).
# jichi's watched path runs the command as `sh -c "<command> 2>&1"` with fd 1
# and fd 2 both on the output pipe (jc_app.c:745, sr.fd_stdout/sr.fd_stderr).
# The hold shape is the subshell's OWN stdout redirected to a file so the
# subshell (and its `sleep`) keep the PIPE open -- NOT the file:
#
#   ( sleep 30.<run> > b1out.<run> 2>&1; echo b1mark_<run>_late >> b1out.<run> ) & echo started
#
# The `&` backgrounds the subshell; `echo started` is the shell's stdout (the
# pipe, 35-120 bytes of output in the measured cases). The subshell holds the
# pipe's write end for 30 s, so EOF does not arrive at 1 s -- the call is held.
#
# MEASURED DEVIATION FROM THE BRIEF'S LITERAL STRING (recorded here, not hidden).
# The brief named the job `( sleep 30 b1mark_<run>_late ) > b1out_<run> 2>&1 &`.
# Probed on this host, that string does NOT hold the pipe (the call returns in
# ~1 s), for two independent reasons: (1) `sleep 30 b1mark_...` is invalid --
# sleep takes a single time operand, so the subshell errors and exits at once;
# and (2) the redirect sits OUTSIDE the parens, so the subshell's fd 1 is the
# FILE, not the pipe -- the pipe closes as soon as the shell's `echo started`
# ends. The authority doc's own F2 shape has the redirect INSIDE the parens
# (`( job > f 2>&1; echo rc >> f ) &`), which IS the hold. This driver uses the
# doc's shape with the brief's marker (a `b1mark_<run>_late` line echoed into
# b1out, and a unique `sleep 30.<run>` duration riding in the surviving
# subshell's cmdline), so it reproduces the exact defect the brief describes:
# "the backgrounded job holds fd 1 of the command's output, so the shell's
# waitpid-EOF does not arrive until the 20 s deadline." The driver's intent and
# red contract are unchanged by the correction -- the literal string would have
# been a test that cannot fire (the pipe is never held, so there is no 20 s
# hold to measure), which is the failure class M1192's own LOG warns against
# (the comment-only commit e3440465). The marker is this driver's own pid
# (RUNPID=$$), distinct from run_timeout_after_eof.sh's marker (that driver's
# $$), so its global /proc scan cannot cross-contaminate this one.
#
# THE B1-ADOPT CONTRACT (D1, notes/M1192.md addendum): B1-adopt is the design;
# B1-min is only the registry-full fallback. Adopt hands the still-open read end
# to the background registry (a drainer copies it into a jc_bg spool file under
# $HOME/.jichi.d/bgspool, named bg-<id>-<pid>.out), so read_background_output
# and kill_background work on the job. The spool-file assertion in cell 1 is the
# adopt measurement: ABSENT in red (the job is group-killed at the deadline, no
# registry), PRESENT in green (the job is adopted and keeps running).
#
# THE NO-CAPS RULE: the jichi invocations inside carry no `timeout` wrapper and
# no per-call `timeout`. The 30 s sleep / 2 s wait are INSIDE jichi -- in cell 1
# the job is ended at the config's own 20 s runTimeout (red) or adopted
# promptly (green), never at a driver cap. The `with_deadline` fence around each
# headless run is the DRIVER'S OWN guard (allowed -- it bounds a broken driver,
# not the measurement), well above every cell's expected end.
. "$(dirname "$0")/_smoke.sh"

# /proc precondition (before the plan, lint check 20): the marker scan (cell 1
# group-kill, cell 3 reap) reads /proc/<pid>/cmdline; a rig with no /proc has no
# child counter, so SKIP before declaring the plan.
if [ ! -d /proc ]; then
    t_skip "needs /proc to assert no background child survived (the marker scan)"
fi

t_plan 3
tmp=$(smoke_tmp)

# The config's runTimeout (D7: the watched path for every model-issued command).
RT=20
# The runTimeout note as rendered by run_command_watched (jc_app.c:885):
#   [stopped: command timed out after <RT>s and was terminated]
TIMEOUT_NOTE="command timed out after ${RT}s"
# Per-run unique marker: this driver's pid. run_timeout_after_eof.sh uses its
# OWN $$ as the suffix, so the two markers never collide across concurrent runs.
RUNPID=$$
MARKER="b1mark_${RUNPID}_late"

# scan_alive PATTERN -- the CHECK-5 idiom of run_timeout_after_eof.sh: walk
# /proc, tr the NUL-separated cmdline to spaces, and collect any pid whose
# cmdline carries PATTERN. PATTERN embeds RUNPID ($$) at every call site, so it
# is unique to this run and cannot cross-contaminate run_timeout_after_eof's own
# marker scan (that driver's suffix is its own $$, a different pid). The pattern
# names the JOB's own command -- `sleep 30.<RUNPID>` for cell 1, `sleep 2.<RUNPID>`
# for cell 3 -- so it matches the backgrounded job (and, while held, the
# subshell that wraps it), not this driver: the driver's /proc cmdline is
# `sh tests/smoke/harness_cmd_shellexit.sh`, which carries no "sleep", so it
# never self-matches. The scan runs only AFTER the run has returned (its
# with_deadline fence resolved), so jichi -- whose own cmdline carries the whole
# command while it is mid-call -- is gone before the scan. Excludes $$/$$ in the
# path as a precaution. Prints the pids, or nothing.
scan_alive() {
    _sa_pat="$1"
    _sa_out=""
    for _sa_d in /proc/[0-9]*; do
        [ -r "$_sa_d/cmdline" ] || continue
        case "$_sa_d" in
            */$$*) continue ;;
        esac
        _sa_cl=$(tr '\0' ' ' < "$_sa_d/cmdline" 2>/dev/null) || continue
        case "$_sa_cl" in
            *"_sa_pat"*)
                _sa_out="$_sa_out $(basename "$_sa_d")"
                ;;
        esac
    done
    printf '%s' "$_sa_out"
}

# ============================================================================
# --- cell 1: THE HELLED CALL (the measured F2 case) -- the red cell ---------
# req.1 -> the backgrounded-job tool call; req.2 (carries the tool result) ->
# the closing text. Each cell gets its own smoke_home (a fresh $HOME), so its
# .jichi.d/bgspool starts empty and the spool count is measured against nothing.
smoke_home
ws1=$(smoke_tmp)
cat > "$tmp/c1.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"( sleep 30.${RUNPID} > b1out.${RUNPID} 2>&1; echo ${MARKER} >> b1out.${RUNPID} ) & echo started"}
rule
  match "\"role\":\"tool\""
  text B1-DONE
EOF
mm_start "$tmp/c1.mm" "$tmp/cap1" 3
write_config "$tmp/c1.json" "$MM_PORT" "\"runTimeout\":${RT}"

t0=$(date +%s)
(cd "$ws1" && with_deadline 45 "$BIN" --config "$tmp/c1.json" --auto \
    --no-session -q -p "run it" < /dev/null) >/dev/null 2>&1
rc1=$?
t1=$(date +%s)
mm_stop
el1=$((t1 - t0))
req1="$tmp/cap1/req.2"
B1SPOOL1="$HOME/.jichi.d/bgspool"
# B1-adopt: how many bg-*.out spool files does this run's (fresh) $HOME hold?
sp1=$(ls "$B1SPOOL1"/bg-*.out 2>/dev/null | wc -l); sp1=${sp1:-0}
# the group-kill / adopt hygiene: is the backgrounded job (its own `sleep
# 30.<RUNPID>`, and the subshell wrapping it -- both carry the marker in their
# cmdline) still alive? A SIGKILL needs a moment to propagate and be reaped,
# so sleep 2 before the scan (the sibling's CHECK-5 does the same).
sleep 2
alive1=$(scan_alive "sleep 30.${RUNPID}")
alive1=${alive1:-}

if [ ! -s "$req1" ]; then
    t_fail "cell 1 (the held call): no tool result captured (req.2 missing/empty) -- the run never reached its second model call; rc=$rc1 elapsed=${el1}s (a broken run, not the red contract)"
elif grep -q "$TIMEOUT_NOTE" "$req1" 2>/dev/null; then
    # RED (today): B1 is not in the tree. The call held until the config's own
    # runTimeout fired (elapsed >= 20 s); the group kill targeted the job;
    # nothing was adopted. Every red fact is named in the line:
    t_fail "cell 1 (the held call): RED by construction -- B1 is not in the tree, so the call held until the config's runTimeout (${RT}s) fired: elapsed ${el1}s (>= ${RT}s), req.2 carries the timeout note '[stopped: ${TIMEOUT_NOTE} and was terminated]' with exit status 124 (the group kill), the runTimeout's group kill targeted the backgrounded job ('sleep 30.${RUNPID}' survivors after it: ${alive1:-none}), and the B1-adopt spool is ABSENT (bg-*.out under $B1SPOOL1 = ${sp1}, want >= 1); run rc=$rc1"
else
    # GREEN (wave C): B1 fired -- the shell's exit was noticed before EOF, the
    # call returned promptly, the job was adopted into a spool file. Wave C
    # names the exact B1 note string; until then the driver asserts elapsed,
    # the ABSENCE of the timeout note (established by this branch), the spool
    # file, `started`, and the shell's exit 0.
    if [ "$el1" -lt 10 ] \
       && grep -q 'started' "$req1" 2>/dev/null \
       && [ "$sp1" -ge 1 ] \
       && [ "$rc1" -eq 0 ]; then
        t_ok "cell 1 (the held call): GREEN -- the call returned in ${el1}s (< 10s: the shell exited before EOF), req.2 carries 'started' and no timeout note, the shell exited 0, and B1-adopt spooled the job (bg-*.out under $B1SPOOL1 = ${sp1})"
    else
        t_fail "cell 1 (the held call) green assertions: elapsed ${el1}s (want < 10), started present=$([ -s "$req1" ] && grep -c 'started' "$req1" 2>/dev/null), spool bg-*.out count=${sp1} (want >= 1, B1-adopt), run rc=$rc1 (want 0)"
    fi
fi

# ============================================================================
# --- cell 2: a NORMAL command (edge, green-invariant) ------------------------
# `sleep 1 && echo waited` finishes on its own: the shell exits, EOF arrives,
# B1 changes nothing. exit 0, output has `waited`, NO [stopped:...] note, and
# no job is adopted (the fresh $HOME's bgspool stays empty).
smoke_home
ws2=$(smoke_tmp)
cat > "$tmp/c2.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sleep 1 && echo waited"}
rule
  match "\"role\":\"tool\""
  text B1-DONE
EOF
mm_start "$tmp/c2.mm" "$tmp/cap2" 3
write_config "$tmp/c2.json" "$MM_PORT" "\"runTimeout\":${RT}"

t0=$(date +%s)
(cd "$ws2" && with_deadline 20 "$BIN" --config "$tmp/c2.json" --auto \
    --no-session -q -p "run it" < /dev/null) >/dev/null 2>&1
rc2=$?
t1=$(date +%s)
mm_stop
el2=$((t1 - t0))
req2="$tmp/cap2/req.2"
B1SPOOL2="$HOME/.jichi.d/bgspool"
sp2=$(ls "$B1SPOOL2"/bg-*.out 2>/dev/null | wc -l); sp2=${sp2:-0}

if [ "$el2" -lt 5 ] && [ "$rc2" -eq 0 ] \
   && grep -q 'waited' "$req2" 2>/dev/null \
   && ! grep -q 'stopped:' "$req2" 2>/dev/null \
   && [ "$sp2" -eq 0 ]; then
    t_ok "cell 2 (normal command): GREEN in both states -- 'sleep 1 && echo waited' returns in ${el2}s (< 5s), exit 0, output has 'waited' and NO [stopped:...] note, and no job is adopted (bg-*.out under $B1SPOOL2 = ${sp2})"
else
    t_fail "cell 2 (normal command): elapsed ${el2}s (want < 5), rc=$rc2 (want 0), waited present=$([ -s "$req2" ] && grep -c 'waited' "$req2" 2>/dev/null), note present=$([ -s "$req2" ] && grep -c 'stopped:' "$req2" 2>/dev/null) (want 0), spool bg-*.out count=${sp2} (want 0)"
fi

# ============================================================================
# --- cell 3: `cmd & wait` (edge -- the shell WAITS, so B1 must NOT fire) -----
# `sleep 2.<run> & wait; echo done`: the shell backgrounds the sleep, then
# `wait`s for it -- so the shell has NOT exited until the job ends, and B1.7's
# no-regression edge says B1 must fire for nothing. elapsed >= 2, exit 0,
# `done` present, no note, and the sleep is reaped by the shell's own `wait`
# (the marker scan finds nothing surviving).
smoke_home
ws3=$(smoke_tmp)
cat > "$tmp/c3.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sleep 2.${RUNPID} & wait; echo done"}
rule
  match "\"role\":\"tool\""
  text B1-DONE
EOF
mm_start "$tmp/c3.mm" "$tmp/cap3" 3
write_config "$tmp/c3.json" "$MM_PORT" "\"runTimeout\":${RT}"

t0=$(date +%s)
(cd "$ws3" && with_deadline 20 "$BIN" --config "$tmp/c3.json" --auto \
    --no-session -q -p "run it" < /dev/null) >/dev/null 2>&1
rc3=$?
t1=$(date +%s)
mm_stop
el3=$((t1 - t0))
req3="$tmp/cap3/req.2"
# the sleep is reaped by the shell's own `wait` -- after the run, nothing
# carries this run's unique sleep duration.
sleep 1
alive3=$(scan_alive "sleep 2.${RUNPID}")
alive3=${alive3:-}

if [ "$el3" -ge 2 ] && [ "$rc3" -eq 0 ] \
   && grep -q 'done' "$req3" 2>/dev/null \
   && ! grep -q 'stopped:' "$req3" 2>/dev/null \
   && [ -z "$alive3" ]; then
    t_ok "cell 3 (the shell waits, cmd & wait): GREEN in both states -- 'sleep 2 & wait; echo done' waits ${el3}s (>= 2s: the shell has not exited, so B1 must not fire), exit 0, output has 'done' and NO [stopped:...] note, and the sleep is reaped by the shell's own wait (survivors: ${alive3:-none})"
else
    t_fail "cell 3 (cmd & wait): elapsed ${el3}s (want >= 2), rc=$rc3 (want 0), done present=$([ -s "$req3" ] && grep -c 'done' "$req3" 2>/dev/null), note present=$([ -s "$req3" ] && grep -c 'stopped:' "$req3" 2>/dev/null) (want 0), survivors=${alive3:-none} (want none)"
fi

t_done
