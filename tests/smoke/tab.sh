#!/bin/sh
# smoke: TUI Tab completion. '/re'+Tab lists /review and /resume (an
# ambiguous command prefix); Ctrl-U clears; '@stackf'+Tab completes to a
# unique file path. Driven on a real PTY via ptydrive; NO_COLOR keeps the
# candidate text as plain substrings in the transcript.
# (Port of tests/e2e/tab.py, M215.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
: > "$ws/stackfile.txt"
write_config "$tmp/config.json" 9

# ambiguous prefix -> the list must name both commands
cat > "$tmp/list.pd" <<'EOF'
expect "] " 15
send "/re\t"
expect "/review" 20
expect "/resume" 20
delay 300
send "\x03/exit\r"
waitexit 10
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 \
    "$tmp/list.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "'/re'+Tab lists /review and /resume"
else
    t_fail "ambiguous-completion script rc=$rc"
fi

# unique file prefix -> completes to the full path
cat > "$tmp/file.pd" <<'EOF'
expect "] " 15
send "@stackf\t"
expect "@stackfile.txt" 20
delay 300
send "\x03/exit\r"
waitexit 10
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 \
    "$tmp/file.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "'@stackf'+Tab completes to @stackfile.txt"
else
    t_fail "file-completion script rc=$rc"
fi

t_done
