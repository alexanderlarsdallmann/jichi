#!/bin/sh
# smoke: a healthy answer that arrives AFTER a silence longer than the stall
# window must NOT be cut (the M22a premise test, 2026-10-15).
#
# THE PREMISE. M22a chose the 30 s stall default on the comment "the low-speed
# floor is 1 byte/sec, so any genuine progress keeps the stall timer from
# firing". The floor resets the timer only while the RESPONSE streams. While
# the server PREFILLS it has answered (HTTP 200) but sends no bytes: a healthy,
# working model is indistinguishable from a frozen one, so a slow healthy
# answer past the window is cut. The 2026-10-14/15 board runs measured it: 4 of
# 4 first attempts cut on the VisionFive 2, both turns on the Nano, the tool
# turn on the Pi 500+ (2026-10-15-stall-default-handoff.md). `timeouts.stall`
# is a CAP on silence; `timeouts.deadStream` (M810, 900 s) is the fence for a
# truly dead stream and stays on even at `stall 0`.
#
# THE MOCK. One `text-stream` rule that sends 200 + the SSE headers, then is
# SILENT for 25 s (the prefill shape: answered, working, no bytes), then
# delivers the healthy body. `text-stream` exists so the silence falls BETWEEN
# the headers and the body (a plain `delay` puts it before the headers, which
# curl's low-speed window does not count). Two runs, the same mock:
#   cap ON  (timeouts.stall 8)  -> 25 s of post-header silence exceeds the 8 s
#        window -> the call is cut, the run ends non-zero, result=timeout.
#   cap OFF (timeouts.stall 0)  -> the 25 s silence is allowed to run -> the
#        healthy answer IS delivered, the run ends 0, nothing cut. The
#        deadStream fence (900 s) is far away, so only the cap differs.
# maxRetries 0 keeps each run to a single bounded attempt.
#
# CHECKS:
#   1  the cap-on config actually carries stall 8 (the window under test)
#   2  cap ON: 25 s of post-header silence > the 8 s window cut the run
#      (non-zero exit) AND telemetry records result=timeout WITH stall_ms
#      (the time-to-cut on a nothing-arrived attempt -- the datum the 30 s
#      default was never measured with, 2026-10-15 stall handoff step 3)
#   3  cap OFF: the SAME 25 s-silent model delivers its healthy answer (rc 0,
#      the text is the mock's) -- the cap, not the model, was the wall
#   4  cap OFF: no result=timeout -- the 25 s silence ran clean
#
# RED-FIRST: with the stall cap made to ignore a post-header silence (or to cut
# even at stall 0) check 3 (or 2) fails. Proven green at delay 25s / stall 8 /
# 0: cap-on cut at ~14 s (result=timeout), cap-off delivered in ~25 s.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# 25 s of post-header silence, then the healthy answer.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  delay 25000
  text-stream The answer is FORTY-TWO.
EOF

# A deadline that comfortably outlasts both runs (cap-on cuts at ~14 s; cap-off
# delivers at ~25 s) while staying bounded. Scales with the tier knob.
_mult=${JC_SMOKE_TIMEOUT_MULT:-${JC_E2E_TIMEOUT_MULT:-1}}
_deadline=$((90 * _mult))

# --- the cap-ON run: stall 8, single attempt --------------------------------
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/cap_on.json" "$MM_PORT" '"timeouts":{"stall":8}'
if grep -q '"stall":8' "$tmp/cap_on.json"; then
    t_ok "the cap-on config carries the 8 s stall window"
else
    t_fail "cap-on config does not name stall 8 -- nothing is being tested"
fi
TELE1="$tmp/tele1.jsonl"
OUT1="$tmp/out1.txt"
(cd "$ws" && with_deadline "$_deadline" "$BIN" --config "$tmp/cap_on.json" \
    --no-session -q --log "$TELE1" --log-level metrics -p "hi" \
    < /dev/null > "$OUT1" 2>&1); rc1=$?
mm_stop

if [ "$rc1" -ne 0 ] && grep -q '"result":"timeout"' "$TELE1" 2>/dev/null \
    && grep -q '"result":"timeout"[^}]*"stall_ms"' "$TELE1" 2>/dev/null; then
    t_ok "cap ON: 25 s of post-header silence > the 8 s window cut the run (rc=$rc1, result=timeout, stall_ms recorded)"
else
    t_fail "cap ON: not cut, not classified, or the cut row lacks stall_ms (rc=$rc1) -- a silence longer than the stall window was not the stall cap, or the time-to-cut datum is still censored"
fi

# --- the cap-OFF run: stall 0, the SAME silent-then-healthy mock ------------
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/cap_off.json" "$MM_PORT" '"timeouts":{"stall":0}'
TELE2="$tmp/tele2.jsonl"
OUT2="$tmp/out2.txt"
(cd "$ws" && with_deadline "$_deadline" "$BIN" --config "$tmp/cap_off.json" \
    --no-session -q --log "$TELE2" --log-level metrics -p "hi" \
    < /dev/null > "$OUT2" 2>&1); rc2=$?
mm_stop

if [ "$rc2" -eq 0 ] && grep -q 'FORTY-TWO' "$OUT2"; then
    t_ok "cap OFF: the same 25 s-silent model delivers its healthy answer (the cap, not the model, was the wall)"
else
    t_fail "cap OFF: the healthy answer did not arrive (rc=$rc2) -- stall 0 still cut the call"
fi

if grep -q '"result":"timeout"' "$TELE2" 2>/dev/null; then
    t_fail "cap OFF: a result=timeout appeared -- the 25 s silence was cut with stall 0"
else
    t_ok "cap OFF: no result=timeout -- the 25 s silence ran clean under the fence alone"
fi

t_done
