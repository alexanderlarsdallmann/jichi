#!/bin/sh
# smoke: pasting multi-line text and special characters (M363). Three defects
# pinned end-to-end through --dump-requests (what the MODEL received is the
# truth, not what the screen showed):
#   1. a bracketed paste carrying control bytes: C0 (here BEL) is stripped --
#      the editor re-emits the buffer per redraw, so pasted controls replayed
#      into the terminal on every keystroke (output-side paste injection) --
#      while CRLF normalizes and TAB survives as content;
#   2. a NON-bracketed CRLF burst paste: the '\r' committed the row and the
#      pending '\n' used to commit a second, EMPTY row -- a Windows-lineage
#      paste gained a blank line per row;
#   3. a multi-line submission used to enter line HISTORY, and arrow-up then
#      loaded raw newlines into the one-row editor (stair-step corruption
#      under OPOST-off raw mode); it is now skipped, so Up recalls the
#      previous single-line entry.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text ok
EOF

# Four turns: a single-line anchor for history, the hostile bracketed paste,
# the CRLF burst, then arrow-up + Enter (which must recall "solo").
cat > "$tmp/run.pd" <<'EOF'
delay 1500
send "solo\r"
delay 2500
send "\x1b[200~alpha\r\nbeta\tgamma\x07X\x1b[201~"
delay 400
send "\r"
delay 2500
send "one\r\ntwo\r\nthree"
delay 400
send "\r"
delay 2500
send "\x1b[A"
delay 400
send "\r"
delay 2500
send "/exit\r"
waitexit 15
assertexit 0
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/t.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto \
    --dump-requests "$tmp/reqs" \
    > /dev/null 2>&1); rc=$?
mm_stop

# --- 1: the session ran all four turns ----------------------------------------
nreq=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | wc -l)
if [ "$rc" -eq 0 ] && [ "$nreq" -ge 4 ]; then
    t_ok "session exited 0 with $nreq captured requests"
else
    t_fail "rc=$rc, $nreq requests captured"
fi

# The Nth turn's user message sits at a fixed index (system,u1,a1,u2,...):
# turn 2 -> .messages[3], turn 3 -> .messages[5], turn 4 -> .messages[7].
r2=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | sed -n 2p)
r3=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | sed -n 3p)
r4=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | sed -n 4p)

# --- 2: bracketed paste -- BEL stripped, CRLF -> LF, TAB kept ------------------
want2=$(printf 'alpha\nbeta\tgammaX')
got2=$([ -n "$r2" ] && "$JQ" '.body.messages[3].content' "$r2")
if [ "$got2" = "$want2" ]; then
    t_ok "hostile bracketed paste arrived as content: BEL stripped, tab + newline kept"
else
    t_fail "turn-2 content mismatch: [$got2]"
fi

# --- 3: burst CRLF paste -- no blank row per line ------------------------------
want3=$(printf 'one\ntwo\nthree')
got3=$([ -n "$r3" ] && "$JQ" '.body.messages[5].content' "$r3")
if [ "$got3" = "$want3" ]; then
    t_ok "burst CRLF paste has no blank rows (the LF half is swallowed)"
else
    t_fail "turn-3 content mismatch: [$got3]"
fi

# --- 4: history skips the multi-line entries -----------------------------------
got4=$([ -n "$r4" ] && "$JQ" '.body.messages[7].content' "$r4")
if [ "$got4" = "solo" ]; then
    t_ok "arrow-up recalls 'solo' -- multi-line submissions never enter history"
else
    t_fail "turn-4 content: [$got4] (wanted the single-line anchor)"
fi

# --- 5: no pasted control byte reached any request ------------------------------
# cJSON serializes a control byte as \u0007, never raw -- the first teeth run
# proved a raw-byte grep stays green even when BEL leaks, so the pin matches
# the ESCAPED form the wire would actually carry.
if ! grep -q 'u0007' "$tmp/reqs"/req-*.json 2>/dev/null; then
    t_ok "no BEL (escaped or raw) in any captured request body"
else
    t_fail "a pasted control byte leaked into a request"
fi

t_done
