#!/bin/sh
# smoke: in-editor ghost-text suggestion (M9). Type a partial prompt,
# press Ctrl-G; the autocomplete-role model returns a continuation the
# editor renders as dim "ghost text". Asserts the model was queried and
# the continuation appears in the transcript, then Tab accepts it. The
# autocomplete call is NON-streaming (message.content), so the mock is a
# plain status+body JSON reply, not SSE.
# (Port of tests/e2e/ghost.py, M215.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  status 200
  body {"choices":[{"index":0,"message":{"role":"assistant","content":"AUTHBUG_MARKER"}}]}
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"ac","provider":"openai","model":"mock-ac",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
   "roles":["autocomplete"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

# \x07 = Ctrl-G (request a suggestion); \t accepts; \x15 = Ctrl-U (clear)
cat > "$tmp/ghost.pd" <<'EOF'
expect "] " 15
send "fix the "
delay 500
send "\x07"
expect "AUTHBUG_MARKER" 20
send "\t"
delay 500
send "\x15"
send "/exit\r"
waitexit 10
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 45 --cols 100 \
    "$tmp/ghost.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "Ctrl-G requested + rendered the ghost suggestion, Tab accepted"
else
    t_fail "ghost script rc=$rc"
fi
if [ -f "$tmp/req.1" ]; then
    t_ok "the autocomplete model was queried"
else
    t_fail "Ctrl-G did not query the autocomplete model"
fi

t_done
