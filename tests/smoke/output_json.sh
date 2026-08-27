#!/bin/sh
# smoke: the machine-readable stream (M63) -- `--output jsonl` emits one
# JSON object per event (message_start/tool_call/tool_result/done), every
# event carries the schema version, and the terminal done object carries
# the contract fields a supervisor keys off (incl. the M97 run economics:
# tools counts, peak_input, cache, starved). Asserted through jsonq, i.e.
# through the JSON parser jichi itself ships.
# (Absorbs tests/e2e/headless_jsonl.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 17
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
echo "jsonl note body" > "$ws/note.txt"
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"note.txt"}
rule
  match "\"role\":\"tool\""
  text JSONL_ANSWER_77
  usage 20 5
EOF

mm_start "$tmp/replies.mm" "$tmp" 2
write_config "$tmp/config.json" "$MM_PORT"

# Note: no --no-session, so the done event carries a session_id (the
# per-driver HOME keeps the store hygienic).
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --auto --output jsonl -q -p "read the note" \
    < /dev/null > "$tmp/out.jsonl"); rc=$?
mm_stop

if [ $rc -eq 0 ] && [ -s "$tmp/out.jsonl" ]; then
    t_ok "jsonl run exits 0 with output"
else
    t_fail "jsonl run rc=$rc"
fi

# every line is JSON and carries v=1
if "$JQ" -l '.v' "$tmp/out.jsonl" > "$tmp/vs" 2>/dev/null \
   && [ "$(grep -cv '^1$' "$tmp/vs")" -eq 0 ]; then
    t_ok "every event parses and carries v=1"
else
    t_fail "a line failed to parse or lacks v=1"
fi

"$JQ" -l '.type' "$tmp/out.jsonl" > "$tmp/types" 2>/dev/null
for want in message_start tool_call tool_result; do
    if grep -q "^$want\$" "$tmp/types"; then
        t_ok "stream carries a $want event"
    else
        t_fail "no $want event; types: $(tr '\n' ' ' < "$tmp/types")"
    fi
done

ndone=$(grep -c '^done$' "$tmp/types")
if [ "$ndone" -eq 1 ]; then
    t_ok "exactly one done event"
else
    t_fail "$ndone done events"
fi

# the terminal object: stop_reason + numeric token accounting
tail -1 "$tmp/out.jsonl" > "$tmp/done.json"
if [ "$("$JQ" '.stop_reason' "$tmp/done.json")" = "done" ]; then
    t_ok "done.stop_reason is 'done'"
else
    t_fail "done.stop_reason: $("$JQ" '.stop_reason' "$tmp/done.json")"
fi
if "$JQ" -q -t number '.tokens.input' "$tmp/done.json" \
   && "$JQ" -q -t number '.tokens.output' "$tmp/done.json"; then
    t_ok "done.tokens carries numeric input/output"
else
    t_fail "done.tokens missing or non-numeric"
fi

# M97 run economics + persistence, the rest of the done contract
sid=$("$JQ" '.session_id' "$tmp/done.json" 2>/dev/null)
if [ -n "$sid" ] && [ "$sid" != "null" ]; then
    t_ok "done.session_id present (the session was saved)"
else
    t_fail "done.session_id missing"
fi

nread=$("$JQ" '.tools.read' "$tmp/done.json" 2>/dev/null)
if [ -n "$nread" ] && [ "$nread" -ge 1 ] 2>/dev/null; then
    t_ok "done.tools counts the read_file call (read=$nread)"
else
    t_fail "done.tools.read missing or < 1: '$nread'"
fi

# both mock calls reported prompt_tokens=20, so the largest single input
# is exactly 20
if [ "$("$JQ" '.peak_input' "$tmp/done.json" 2>/dev/null)" = "20" ]; then
    t_ok "done.peak_input is the largest single call input (20)"
else
    t_fail "done.peak_input != 20"
fi

if "$JQ" -q '.cache' "$tmp/done.json"; then
    t_ok "done.cache present"
else
    t_fail "done.cache missing"
fi

if [ "$("$JQ" '.starved' "$tmp/done.json" 2>/dev/null)" = "false" ]; then
    t_ok "done.starved is false on a completed run"
else
    t_fail "done.starved not false"
fi

# --- M431c: the run id, the join key -----------------------------------------
# The envelope's run id is what the journal stamps on every line and telemetry
# stamps on every event, so it is what JOINS a worker's stdout to its own audit
# trail (M420 built that join). It appeared in NEITHER machine surface, so a
# supervisor could not correlate the two unless it had passed --journal itself and
# remembered the path.
#
# Two-sided WITHOUT a second binary: the run above armed no envelope (--auto alone
# does not), so it must carry neither surface; the run below arms one with
# --journal and must carry both, agreeing with each other AND with the journal.

if ! "$JQ" -q '.run' "$tmp/done.json"; then
    t_ok "no envelope: done carries no run id (nothing to correlate with)"
else
    t_fail "done.run present on a run with no envelope: $("$JQ" '.run' "$tmp/done.json")"
fi

if ! grep -q '"type":"run_start"' "$tmp/out.jsonl"; then
    t_ok "no envelope: no run_start event"
else
    t_fail "run_start emitted without an envelope"
fi

mm_start "$tmp/replies.mm" "$tmp/cap2" 2
write_config "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    --auto --output jsonl -q --no-session --journal "$tmp/j.jsonl" \
    -p "read the note" \
    < /dev/null > "$tmp/out2.jsonl"); rc2=$?
mm_stop

# run_start must be FIRST: its whole purpose is to arrive before the work, so a
# supervisor can start tailing while the run is live. Emitting it late would make
# the event technically present and practically useless.
first_type=$(head -1 "$tmp/out2.jsonl" | "$JQ" '.type' 2>/dev/null)
if [ "$first_type" = "run_start" ]; then
    t_ok "run_start is the FIRST event on the stream"
else
    t_fail "first event is '$first_type', not run_start (rc2=$rc2)"
fi

# The join, end to end: stream id == terminal id == the journal's own key.
head -1 "$tmp/out2.jsonl" > "$tmp/rs.json"
grep '"type":"done"' "$tmp/out2.jsonl" | tail -1 > "$tmp/done2.json"
grep '"event":"start"' "$tmp/j.jsonl" 2>/dev/null | head -1 > "$tmp/jstart.json"
rs_run=$("$JQ" '.run' "$tmp/rs.json" 2>/dev/null)
dn_run=$("$JQ" '.run' "$tmp/done2.json" 2>/dev/null)
j_run=$("$JQ" '.run' "$tmp/jstart.json" 2>/dev/null)
if [ -n "$rs_run" ] && [ "$rs_run" = "$dn_run" ] && [ "$rs_run" = "$j_run" ]; then
    t_ok "run_start.run == done.run == the journal's run ($rs_run)"
else
    t_fail "join broken: run_start='$rs_run' done='$dn_run' journal='$j_run'"
fi

t_done
