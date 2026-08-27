#!/bin/sh
# smoke: the TUI on a real PTY (via ptydrive) -- starts offline, shows a
# prompt, /help lists commands, /exit ends the process cleanly; and a
# TIOCSWINSZ+SIGWINCH resize mid-session does not crash the editor.
# Assertions are deliberately coarse (prompt/keywords/exit code), not
# cosmetic: old-kernel PTY drift is what this driver exists to surface.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Offline config (unreachable model): the TUI must still start and serve
# slash commands without a working model.
write_config "$tmp/config.json" 9

# Two subtleties, both discovered on the first live run: the expect after
# /help must be a string that is NOT in the startup banner (the banner
# says "Type /exit to quit", so expecting "/exit" fires instantly), and
# sends need human-scale gaps -- back-to-back writes land inside the M156
# burst-paste window and merge into ONE logical line ("/help\n/exit").
cat > "$tmp/basic.pd" <<'EOF'
expect "] " 15
delay 300
send "/help\r"
expect "/model" 20
delay 300
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/basic.log" "$tmp/basic.pd" -- \
    "$BIN" --config "$tmp/config.json"); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "TUI prompt, /help, /exit (clean exit)"
else
    t_fail "basic TUI script rc=$rc (see transcript above)"
fi

# M345: a typo'd slash command gets the same kindness the model has had since
# M91 -- the nearest known command. "did you mean" is unique to this branch in
# a basic session, so the expect is anchored (the M293/M296 rule).
cat > "$tmp/typo.pd" <<'EOF'
expect "] " 15
delay 300
send "/hlep\r"
expect "did you mean /help?" 20
delay 300
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/typo.log" "$tmp/typo.pd" -- \
    "$BIN" --config "$tmp/config.json"); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "/hlep suggests /help (did-you-mean on the human's typo)"
else
    t_fail "typo TUI script rc=$rc (see transcript above)"
fi

cat > "$tmp/resize.pd" <<'EOF'
expect "] " 15
winsize 24 120
delay 300
send "/help\r"
expect "/model" 20
delay 300
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 80 \
    --log "$tmp/resize.log" "$tmp/resize.pd" -- \
    "$BIN" --config "$tmp/config.json"); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "mid-session resize survives (SIGWINCH)"
else
    t_fail "resize TUI script rc=$rc"
fi

t_done
