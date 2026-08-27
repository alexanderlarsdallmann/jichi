#!/bin/sh
# smoke: line-editor Ctrl-R incremental reverse history search (M69).
# Submit a slash command (added to history, no model call), then Ctrl-R +
# a query; the reverse-i-search prompt must surface the prior /help entry.
# Driven on a real PTY via ptydrive; no turn is run.
# (Port of tests/e2e/editor.py, M215.)
. "$(dirname "$0")/_smoke.sh"

t_plan 1
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
write_config "$tmp/config.json" 9

# \x12 = Ctrl-R (reverse search), \x1b = Esc (cancel it)
cat > "$tmp/rsearch.pd" <<'EOF'
expect "] " 15
delay 400
send "/help\r"
expect "Commands" 20
delay 400
send "\x12hel"
expect "reverse-i-search" 20
expect "/help" 20
delay 300
send "\x1b"
delay 300
send "/exit\r"
waitexit 12
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 80 \
    "$tmp/rsearch.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "Ctrl-R reverse search surfaced the prior /help entry"
else
    t_fail "reverse-search script rc=$rc"
fi

t_done
