#!/bin/sh
# smoke: /undo tells the MODEL what it told the human (M349).
#
# /undo reverts the FILES and keeps the conversation (the M34c split; /rewind
# is the both-halves form). The human sees "(reverted: <label>)"; until M349
# the model saw nothing -- its history still contained its own tool results
# asserting the file now holds the new content, and the next turn built on
# phantom state. M349 injects one [undo] user-role note naming the reverted
# files, saved immediately so a resume cannot revive the stale beliefs
# without the correction beside them.
#
# Flow: a mock turn writes hello.txt (auto mode; snapshots take the pre-edit
# checkpoint), /undo restores it, a second turn runs -- and the SECOND turn's
# captured request must carry the [undo] note while the first turn's requests
# must not (M310 pairing).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'ORIGINAL CONTENT\n' > "$ws/hello.txt"

cat > "$tmp/u.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"hello.txt","content":"MODIFIED BY THE AGENT\n"}
rule
  count 2
  text FIRST_TURN_OK
rule
  text SECOND_TURN_OK
EOF
mm_start "$tmp/u.mm" "$tmp/cap" 4

# selfReview off: in auto mode M39 injects a review continuation after the
# edit, adding a third model call to turn 1 -- which both shifts the request
# numbering and puts SECOND_TURN_OK on screen BEFORE the second turn (the
# stale-match trap of M293/M296, live). Found because the driver's first run
# asserted req.3 and the note was in req.4, working perfectly.
cat > "$tmp/c.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF

cat > "$tmp/undo.pd" <<'EOF'
expect "] " 15
delay 300
send "change the greeting\r"
expect "FIRST_TURN_OK" 30
delay 400
send "/undo\r"
expect "reverted" 15
delay 400
send "go on\r"
expect "SECOND_TURN_OK" 30
delay 300
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/undo.log" "$tmp/undo.pd" -- \
    "$BIN" --config "$tmp/c.json" --no-route); rc=$?
mm_stop

# --- 1: the flow ran end to end ------------------------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "edit, /undo, second turn, clean exit"
else
    t_fail "PTY script rc=$rc (see transcript above)"
fi

# --- 2: the file really reverted ------------------------------------------------
if grep -q "ORIGINAL CONTENT" "$ws/hello.txt" && \
   ! grep -q "MODIFIED" "$ws/hello.txt"; then
    t_ok "hello.txt is back to its pre-turn content"
else
    t_fail "hello.txt was not restored: $(cat "$ws/hello.txt")"
fi

# --- 3: the first turn's requests carry no note (M310 pairing) -----------------
if [ -s "$tmp/cap/req.2" ] && ! grep -q "\[undo\]" "$tmp/cap/req.2"; then
    t_ok "no [undo] note before the undo happened"
else
    t_fail "request 2 missing or already carries a note"
fi

# --- 4: the second turn's request tells the model what reverted ----------------
if grep -q "\[undo\]" "$tmp/cap/req.3" 2>/dev/null && \
   grep -q "hello.txt" "$tmp/cap/req.3" 2>/dev/null && \
   grep -q "re-read" "$tmp/cap/req.3" 2>/dev/null; then
    t_ok "the model is told: hello.txt reverted, earlier reads are stale"
else
    t_fail "no [undo] note in request 3: $(head_bytes 200 "$tmp/cap/req.3" 2>/dev/null)"
fi

t_done
