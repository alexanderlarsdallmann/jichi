#!/bin/sh
# smoke: the prompt-advice gesture (Ctrl-Q, M280). Type a request, press
# Ctrl-Q; the ACTIVE model returns one line about what is unclear, which the
# editor prints on its own labelled line ABOVE a redrawn prompt -- and, the
# property that distinguishes it from Ctrl-G ghost text, does NOT insert it
# into the input. Both halves are asserted, because "it printed something" is
# only half the contract; the other half is that your line is untouched, which
# is exactly what went wrong when a model's clarifying question was spliced in
# as if it were a continuation (docs/AUTOCOMPLETE.md).
#
# The advice call is NON-streaming (message.content), so the mock replies with
# a plain status+body JSON, not SSE.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  status 200
  body {"choices":[{"index":0,"message":{"role":"assistant","content":"advice: which project? name the repo or path"}}]}
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

# \x11 = Ctrl-Q (ask for advice); \x15 = Ctrl-U (clear the line)
cat > "$tmp/advice.pd" <<'EOF'
expect "] " 15
send "what is the name of this pr"
delay 500
send "\x11"
expect "advice:" 20
delay 400
send "\x15"
send "/exit\r"
waitexit 10
EOF

# M368: 90s, not 45 -- in the full `make ci` tier this driver runs right
# after the valgrind stage on a small VM, and the 45s deadline was killed
# (rc=143) in-suite while passing standalone: a load flake the runner itself
# classified. Deadlines here assume a hot, small box.
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/advice.log" "$tmp/advice.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "Ctrl-Q asked the model and the advice line was rendered"
else
    t_fail "advice script rc=$rc"
fi

if [ -f "$tmp/req.1" ]; then
    t_ok "the active model was queried"
else
    t_fail "Ctrl-Q did not query a model"
fi

# The label the model emitted must have been stripped by jc_advice_clean, so
# the rendered line carries exactly ONE "advice:" -- the editor's own label.
# Two would mean the cleaner let the model's label through.
_n=$(smoke_plain "$tmp/advice.log" \
     | grep -o 'advice:' | wc -l)
if [ "$_n" -eq 1 ]; then
    t_ok "the model's own 'advice:' label was stripped (one label rendered)"
else
    t_fail "expected exactly one 'advice:' on screen, saw $_n"
fi

# The load-bearing difference from ghost text: the buffer is NOT touched. If
# the advice had been spliced in, the prompt line would carry its words.
if smoke_plain "$tmp/advice.log" \
   | grep -q 'this prwhich project'; then
    t_fail "the advice was spliced into the input line (it must only be printed)"
else
    t_ok "the input line was left untouched -- advice is printed, not inserted"
fi

t_done
