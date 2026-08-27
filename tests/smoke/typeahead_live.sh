#!/bin/sh
# smoke: the three type-ahead controls added in M258.
#
#   1. VISIBLE DURING A TOOL. A foreground shell command blocks the agent loop,
#      so no front-end callback used to fire while it ran -- minutes of blind
#      typing during a build, the worst remaining window. The command runner now
#      ticks the front-end (jc_app_tick) whenever it is idle waiting on output.
#      Asserted structurally: a working line carrying the typed text must appear
#      AFTER the tool-start line, i.e. while the tool was running.
#   2. Ctrl-K UN-QUEUES. A line committed while the echo was not visible could
#      not be recalled (limitation D10). Ctrl-K drops the pending queue: the
#      notice appears and the text never reaches the wire (STEER_MISSED).
#   3. /typeahead TOGGLES AT RUNTIME. Started without --type-ahead, `/typeahead
#      on` enables it for the rest of the session -- no restart to try it.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home

# --- 1. the echo is alive while a tool runs -------------------------------
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sleep 4"}
rule
  match "[operator] during the tool"
  text TICK_OK
rule
  text TICK_MISSED
EOF
cat > "$tmp/t.pd" <<'EOF'
expect "] " 15
send "run something slow\r"
expect "run_terminal_command" 20
delay 700
send "during the tool"
delay 1200
expect "working" 15
send "\r"
expect "queued" 15
expect "TICK_OK" 30
delay 400
send "/exit\r"
waitexit 10
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 80 --cols 100 \
    --log "$tmp/tr.log" "$tmp/t.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route --auto --type-ahead); rc=$?
mm_stop

if [ $rc -ne 0 ]; then
    t_fail "tool-tick script rc=$rc"
elif tr '\r' '\n' < "$tmp/tr.log" | awk '
        /run_terminal_command/ { started = 1 }
        started && /working.*during the tool/ { found = 1 }
        END { exit !found }'; then
    t_ok "typing stays visible while a foreground tool runs (the tick hook)"
else
    t_fail "no working-line echo after the tool started: the tick never arrived"
fi

# --- 2. Ctrl-K drops the queue ---------------------------------------------
tmp2=$(smoke_tmp); ws2=$(smoke_tmp)
cat > "$tmp2/replies.mm" <<'EOF'
wire openai
rule
  count 1
  delay 4000
  tool list_files {"path":"."}
rule
  match "[operator]"
  text KILL_FAILED
rule
  text STEER_MISSED
EOF
cat > "$tmp2/k.pd" <<'EOF'
expect "] " 15
send "hello\r"
delay 800
send "oops wrong instruction\r"
expect "queued" 15
send "\x0b"
expect "dropped" 15
expect "STEER_MISSED" 30
delay 400
send "/exit\r"
waitexit 10
EOF
mm_start "$tmp2/replies.mm" "$tmp2"
write_config "$tmp2/config.json" "$MM_PORT"
(cd "$ws2" && "$SMOKE_TOOLS/ptydrive" --deadline 80 --cols 100 \
    --log "$tmp2/tr.log" "$tmp2/k.pd" \
    -- "$BIN" --config "$tmp2/config.json" --no-route --type-ahead); rc2=$?
mm_stop

if [ $rc2 -ne 0 ]; then
    t_fail "Ctrl-K script rc=$rc2 (expected queue dropped, run reaching STEER_MISSED)"
elif grep -q "oops wrong instruction" "$tmp2/req.2" 2>/dev/null; then
    t_fail "Ctrl-K did not un-queue: the dropped line still reached the model"
else
    t_ok "Ctrl-K un-queues a committed line before it is sent"
fi

# --- 3. /typeahead enables it mid-session (started WITHOUT the flag) -------
tmp3=$(smoke_tmp); ws3=$(smoke_tmp)
cp "$tmp/replies.mm" "$tmp3/replies.mm"
cat > "$tmp3/g.pd" <<'EOF'
expect "] " 15
send "/typeahead on\r"
expect "type-ahead on" 15
delay 400
send "run something slow\r"
expect "run_terminal_command" 20
delay 700
send "during the tool\r"
expect "queued" 20
expect "TICK_OK" 30
delay 400
send "/exit\r"
waitexit 10
EOF
mm_start "$tmp3/replies.mm" "$tmp3"
write_config "$tmp3/config.json" "$MM_PORT"
(cd "$ws3" && "$SMOKE_TOOLS/ptydrive" --deadline 80 --cols 100 \
    --log "$tmp3/tr.log" "$tmp3/g.pd" \
    -- "$BIN" --config "$tmp3/config.json" --no-route --auto); rc3=$?
mm_stop

if [ $rc3 -eq 0 ]; then
    t_ok "/typeahead on enables the queue mid-session (no restart)"
else
    t_fail "/typeahead toggle script rc=$rc3"
fi

t_done
