#!/bin/sh
# smoke: type-ahead + mid-turn steering from the TUI keyboard (M254/M257).
#
# Before M254 the human at the TUI was the only operator with no mid-run
# channel: keystrokes typed while the agent worked were echoed into the
# streamed output by the tty and then DISCARDED by the TCSAFLUSH at the next
# raw-mode entry. Since M257 the feature is OPT-IN (--type-ahead), because
# jichi cannot guarantee the typing is visible in every window and input you
# cannot see is input you cannot correct.
#
# Four assertions, two of them about what must NOT happen:
#   1. with --type-ahead: a line typed during the first (slow) model call and
#      committed with Enter is confirmed "queued", and the run ends STEER_OK --
#      which the mock returns ONLY when the request carried the "[operator]"
#      steering message.
#   2. it reached the SECOND model call, not the first: req.1 must not contain
#      it (typed after that request went out) and req.2 must. That is the whole
#      contract -- captured mid-turn, applied at the next tool-call boundary.
#   3. the typing was VISIBLE while it was being typed: the working line and
#      the text appear on the same rendered line. This runs under NO_COLOR
#      (whole tier does), which is exactly the case that used to be blind --
#      before M257 the working line existed only when colour did.
#   4. WITHOUT the flag the feature is off: the same script leaves the typed
#      text out of the wire entirely (STEER_MISSED). Pins the default, so it
#      cannot drift back on unnoticed.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Rule 1 stalls 3s (the window to type) then calls a read-only tool, which
# needs no approval in chat mode -- so the run reaches a tool boundary. Rule 2
# only fires when the steering message actually made it onto the wire.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  delay 3000
  tool list_files {"path":"."}
rule
  match "[operator] also read the docs"
  text STEER_OK
rule
  text STEER_MISSED
EOF

cat > "$tmp/steer.pd" <<'EOF'
expect "] " 15
send "hello\r"
delay 900
send "also read the docs"
delay 700
send "\r"
expect "queued" 20
expect "STEER_OK" 25
delay 500
send "/exit\r"
waitexit 10
EOF

# --- run A: opted in ------------------------------------------------------
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 70 --cols 100 \
    --log "$tmp/tr.log" "$tmp/steer.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route --type-ahead); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "typed-ahead line is queued mid-turn and steers the run"
else
    t_fail "type-ahead script rc=$rc"
fi

if [ ! -f "$tmp/req.1" ] || [ ! -f "$tmp/req.2" ]; then
    t_fail "expected two captured requests (req.1, req.2)"
elif grep -q "also read the docs" "$tmp/req.1"; then
    t_fail "req.1 already carried the typed text (test window is wrong)"
elif grep -q "\[operator\] also read the docs" "$tmp/req.2"; then
    t_ok "the typed text reached the next model call as one [operator] message"
else
    t_fail "req.2 is missing the [operator] steering message"
fi

# The echo shares the working line, so both land in one CR-delimited render.
# tr '\r' '\n' splits the in-place redraws into inspectable lines.
if tr '\r' '\n' < "$tmp/tr.log" | grep -q "working.*also read the docs"; then
    t_ok "the typing was visible on the working line (with NO_COLOR)"
else
    t_fail "no working-line echo of the typed text: $(tr '\r' '\n' \
        < "$tmp/tr.log" | grep -c working) working line(s) rendered"
fi

# --- run B: the default (no flag) ----------------------------------------
ws2=$(smoke_tmp)
tmp2=$(smoke_tmp)
cp "$tmp/replies.mm" "$tmp2/replies.mm"
cat > "$tmp2/off.pd" <<'EOF'
expect "] " 15
send "hello\r"
delay 900
send "also read the docs\r"
expect "STEER_MISSED" 30
delay 500
send "/exit\r"
waitexit 10
EOF
mm_start "$tmp2/replies.mm" "$tmp2"
write_config "$tmp2/config.json" "$MM_PORT"
(cd "$ws2" && "$SMOKE_TOOLS/ptydrive" --deadline 70 --cols 100 \
    --log "$tmp2/tr.log" "$tmp2/off.pd" \
    -- "$BIN" --config "$tmp2/config.json" --no-route); rc2=$?
mm_stop

if [ $rc2 -ne 0 ]; then
    t_fail "default-off script rc=$rc2 (expected the run to reach STEER_MISSED)"
elif grep -q "also read the docs" "$tmp2/req.2" 2>/dev/null; then
    t_fail "type-ahead is ON without --type-ahead: the default drifted"
else
    t_ok "off by default: without --type-ahead the typed text never ships"
fi

t_done
