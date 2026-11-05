#!/bin/sh
# smoke: the second SIGINT aborts, it does not kill (M918, R03-F1).
#
# THE DEFECT (whole-tree review 2026-10-08, R03-main-b.md F1, verified by
# driving the real TUI in a pty): every handler was installed with signal(),
# which under this build's flags (-std=c89 -pedantic -D_POSIX_C_SOURCE=200112L)
# is glibc's __sysv_signal -- SA_RESETHAND|SA_NODEFER. After ONE delivery the
# disposition reset to SIG_DFL, so the first Ctrl-C aborted the current turn
# and the second KILLED the process (exit 130) with no teardown. A
# BSD-semantics libc keeps the handler, so the defect is invisible there --
# this driver measures the effect on the binary it is given.
#
# WHY A PTY AND TWO TURNS (a headless kill -INT cannot show this): in a
# headless -p run the FIRST SIGINT already ends the process, so there is no
# "still alive to receive a second one". The TUI is the surface where the
# first Ctrl-C aborts the turn and the process keeps running. ptydrive's
# `signal INT` verb sends the signal by number (kill(child, 2)) to the jichi
# process; the TUI's prompt keeps ISIG on, which is what an operator's Ctrl-C
# does. ptydrive exits 7 when the child dies by a signal that no assertexit
# named (M758) -- so a still-killing second SIGINT fails this script with
# that very code, not a silent green.
#
# WHAT IT HOLDS:
#   1. the first SIGINT aborts the running turn -- "[interrupted]" prints and
#      the model's late answer never reaches the transcript.
#   2. the process is STILL ALIVE after the second SIGINT aborts its turn:
#      the second "[interrupted]" is printed (pre-fix: death by signal 2,
#      ptydrive rc 7).
#   3. the session ends cleanly on /exit (exit 0; pre-fix this line is
#      unreachable -- nothing is left to /exit).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# One slow turn: the model takes 6 s, so a SIGINT 1.5 s after submit lands
# mid-call, and the first turn's answer (SLOW_ANSWER) is still in flight when
# the second turn is submitted -- the late answer of turn one must never
# surface either.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "go"
  delay 6000
  text SLOW_ANSWER
rule
  text OTHER
EOF

cat > "$tmp/sig.pd" <<'EOF'
expect "] " 30
send "go\r"
delay 1500
signal INT
expect "[interrupted]" 10
delay 1000
send "go\r"
delay 1500
signal INT
expect "[interrupted]" 10
# The pending "go" that raced interrupt 2's prompt is consumed DIFFERENTLY by
# speed: on the fast host it starts a turn (then is itself aborted) with no
# "please retype it" note, while on the slow V2f guest (1 vCPU, 2026-10-16)
# it is flushed with that note at the next prompt -- and a /exit typed before
# that prompt settles is discarded too, so the old single send + waitexit
# expired with the child at a live prompt (ptydrive rc 5). No output marker is
# stable across both, so settle then send /exit repeatedly with a drain
# between: the first one that lands a stable prompt exits cleanly, and the
# later ones are no-ops (the prompt is gone, so nothing reads them). drain
# scales with the timeout multiplier; it cannot fail, so this bounds no
# measurement.
drain 1000
send "/exit\r"
drain 1000
send "/exit\r"
drain 1000
send "/exit\r"
drain 1000
send "/exit\r"
waitexit 15
assertexit 0
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/sig.log" "$tmp/sig.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route --no-session) \
    > "$tmp/sig.out" 2>&1; rc=$?
mm_stop
tr '\r' '\n' < "$tmp/sig.log" > "$tmp/sig.txt"

# 1: the first SIGINT aborted the turn; the late answer never printed
if grep -q '\[interrupted\]' "$tmp/sig.txt" && ! grep -q 'SLOW_ANSWER' "$tmp/sig.txt"; then
    t_ok "the first SIGINT aborts the running turn, before its answer"
else
    t_fail "first SIGINT: no [interrupted] or the late answer surfaced (rc=$rc)"
fi

# 2: the second SIGINT aborted too -- the process survived it. Pre-fix the
# disposition had reset to SIG_DFL after the first delivery, so this signal
# kills jichi; ptydrive reports the death by signal with rc 7 and the second
# [interrupted] never prints.
n_int=$(grep -c '\[interrupted\]' "$tmp/sig.txt")
if [ "$n_int" -ge 2 ] && [ $rc -ne 7 ]; then
    t_ok "the second SIGINT aborts the next turn; the process is still alive"
else
    t_fail "the second SIGINT killed jichi (ptydrive rc=$rc, [interrupted] x$n_int) -- the handler was one-shot"
fi

# 3: the session that survived two interrupts ends cleanly
if [ $rc -eq 0 ]; then
    t_ok "after two interrupts the session exits cleanly on /exit"
else
    t_fail "jichi exited rc=$rc (want 0 on /exit)"
fi

t_done
