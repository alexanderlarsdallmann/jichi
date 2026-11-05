#!/bin/sh
# smoke: typed input is kept across TUI runs, and a line with a secret is not
# (M763).
#
# THE GAP, from the operator's twelve questions (plan section 9, for users) and
# HARDENING.md section 7.5, open since M300: Up and Ctrl-R recalled only this
# session's lines. Keeping them needed one decision first -- whether a line with
# a secret in it may be written to disk. It may not, as far as jichi can tell it
# has one: a registered secret (a resolved API key), or a token shaped like a
# common credential. Everything else goes to ~/.jichi.d/input_history, 0600.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
H="$HOME/.jichi.d/input_history"

# --- session 1, no model: a plain line and a key-shaped one ----------------------------
cat > "$tmp/s1.pd" <<'EOF'
expect "for commands" 15
delay 500
send "HISTORY_ONE please\r"
expect "no model is configured" 10
delay 800
send "my token sk-abcdefghijklmnop0123\r"
delay 1500
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 --log "$tmp/s1.log" \
    "$tmp/s1.pd" -- "$BIN" --no-session --no-lite) > "$tmp/s1.out" 2>&1; rc1=$?

# 1: the file exists, owner-only, and holds the plain line
perm=$(ls -l "$H" 2>/dev/null | cut -c1-10)
if [ "$rc1" -eq 0 ] && [ "$perm" = "-rw-------" ] && grep -q '^HISTORY_ONE please$' "$H"; then
    t_ok "the typed line is kept in ~/.jichi.d/input_history, mode 0600"
else
    t_fail "history file (rc=$rc1, perm '$perm'): $(head_bytes 200 "$H" 2>/dev/null)"
fi

# 2: the key-shaped line was not written
if [ -f "$H" ] && ! grep -q 'sk-abcdefghijklmnop0123' "$H"; then
    t_ok "a line holding a key-shaped token is not written"
else
    t_fail "the key-shaped line reached the file: $(grep 'sk-' "$H" 2>/dev/null | head -1)"
fi

# --- session 2: Up recalls session 1 ---------------------------------------------------
# The newest entry is /exit; the one before it is the plain line (the key-shaped
# one was never written, so it is not recalled either).
cat > "$tmp/s2.pd" <<'EOF'
expect "for commands" 15
delay 500
send "\e[A"
delay 300
send "\e[A"
expect "HISTORY_ONE please" 10
delay 300
send "\x15"
delay 300
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 --log "$tmp/s2.log" \
    "$tmp/s2.pd" -- "$BIN" --no-session --no-lite) > "$tmp/s2.out" 2>&1; rc2=$?
# 3: the second session's editor showed the first session's line
if [ "$rc2" -eq 0 ] && grep -q 'HISTORY_ONE please' "$tmp/s2.log"; then
    t_ok "Up in a new session recalls a line typed in the one before"
else
    t_fail "recall in session 2 (ptydrive rc=$rc2): $(tail -c 200 "$tmp/s2.out")"
fi

# --- session 3: a line holding the configured API key --------------------------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text SECRET_TURN_DONE
EOF
mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config3.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"RegSecretXYZ123456","roles":["chat"]}],
"snapshots":false,"repoMap":false}
EOF
cat > "$tmp/s3.pd" <<'EOF'
expect "[chat:" 15
delay 500
send "use RegSecretXYZ123456 now\r"
expect "SECRET_TURN_DONE" 20
delay 800
send "/exit\r"
waitexit 15
assertexit 0
EOF
b3=$(wc -l < "$H" 2>/dev/null | tr -d ' ')
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 --log "$tmp/s3.log" \
    "$tmp/s3.pd" -- "$BIN" --config "$tmp/config3.json" --no-session --no-route) \
    > "$tmp/s3.out" 2>&1; rc3=$?
mm_stop
a3=$(wc -l < "$H" 2>/dev/null | tr -d ' ')
# 4: the line with the key in it is not in the file; the session's other line,
# its /exit, is -- so exactly one line was added (measured on this session alone)
if [ "$rc3" -eq 0 ] && ! grep -q 'RegSecretXYZ123456' "$H" \
   && [ -n "$b3" ] && [ "$a3" = "$((b3 + 1))" ] && [ "$(tail -1 "$H")" = "/exit" ]; then
    t_ok "a line holding the configured API key is not written (the others are)"
else
    t_fail "registered secret (rc=$rc3, lines $b3 -> $a3): $(grep -c RegSecret "$H" 2>/dev/null) line(s) with the key; $(tail -c 200 "$tmp/s3.out")"
fi

# --- session 4: inputHistory: false writes nothing -------------------------------------
printf '{"inputHistory": false}\n' > "$tmp/config4.json"
cat > "$tmp/s4.pd" <<'EOF'
expect "for commands" 15
delay 500
send "HISTORY_OFF_LINE\r"
expect "no model is configured" 10
delay 800
send "/exit\r"
waitexit 15
assertexit 0
EOF
before=$(wc -l < "$H" | tr -d ' ')
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 --log "$tmp/s4.log" \
    "$tmp/s4.pd" -- "$BIN" --config "$tmp/config4.json" --no-session --no-lite) \
    > "$tmp/s4.out" 2>&1; rc4=$?
after=$(wc -l < "$H" | tr -d ' ')
# 5: nothing was appended while the switch was off
if [ "$rc4" -eq 0 ] && [ "$before" = "$after" ] && ! grep -q 'HISTORY_OFF_LINE' "$H"; then
    t_ok "with inputHistory off, nothing typed is written ($after line(s) before and after)"
else
    t_fail "inputHistory off (rc=$rc4): $before -> $after line(s)"
fi

t_done
