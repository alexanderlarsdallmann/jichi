#!/bin/sh
# smoke: the TYPED input path (M198 #2), the complement of paste.sh. A
# newline SUBMITS or merely commits a pasted row depending on whether more
# input is already buffered (jc_term input_pending). paste.sh proves one
# burst -> one turn; this proves three lines TYPED with gaps -> three
# separate turns. Each line is its own ptydrive `send` and the `expect`
# for that turn's reply between sends IS the human-scale gap (nothing is
# pending when the editor reads each newline), so each newline submits.
# If the heuristic glued them, only TURN1 would appear and expect TURN2
# would time out. (Port of tests/e2e/typed.py, M215.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# count rules stand in for the Python mock's "TURN<n>" per request
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  text TURN1
rule
  count 2
  text TURN2
rule
  count 3
  text TURN3
EOF

mm_start "$tmp/replies.mm" "$tmp"
# markdown off so the reply text is plain in the transcript
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"mock","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,
 "maxRetries":0,"markdown":false}
EOF

cat > "$tmp/typed.pd" <<'EOF'
expect "] " 15
send "alpha\n"
expect "TURN1" 12
delay 600
send "beta\n"
expect "TURN2" 12
delay 600
send "gamma\n"
expect "TURN3" 12
delay 300
send "/exit\n"
waitexit 10
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 80 \
    "$tmp/typed.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "three typed lines arrived as three separate turns (TURN1/2/3)"
else
    t_fail "typed script rc=$rc -- newlines may have glued into one turn"
fi

# each turn carried exactly its own line, not an accumulation
if grep -q "alpha" "$tmp/req.1" 2>/dev/null \
   && ! grep -q "beta" "$tmp/req.1" 2>/dev/null \
   && ! grep -q "gamma" "$tmp/req.1" 2>/dev/null; then
    t_ok "turn 1 carried only 'alpha' (the newline submitted, not committed)"
else
    t_fail "request 1 accumulated later lines -- a row was committed"
fi
if grep -q "gamma" "$tmp/req.3" 2>/dev/null; then
    t_ok "turn 3 carried 'gamma'"
else
    t_fail "request 3 missing 'gamma'"
fi

t_done
