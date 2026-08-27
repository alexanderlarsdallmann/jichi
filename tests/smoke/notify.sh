#!/bin/sh
# smoke: completion notification (M34f/F6) -- on a completed --auto run
# jichi rings the terminal bell (a BEL byte on STDERR, keeping stdout
# clean) and runs the --notify command with $JICHI_NOTIFY (session title)
# and $JICHI_CWD in its environment.
# (Port of tests/e2e/notify.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
marker="$ws/notified.txt"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text all done
EOF

mm_start "$tmp/replies.mm" "$tmp" 1
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" -q --auto \
    --bell --notify "printf '%s|%s' \"\$JICHI_NOTIFY\" \"\$JICHI_CWD\" > $marker" \
    -p "do the thing" < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop

if grep -q "$(printf '\a')" "$tmp/err"; then
    t_ok "terminal bell (BEL) on stderr"
else
    t_fail "no BEL byte on stderr (rc=$rc)"
fi

if [ -f "$marker" ]; then
    t_ok "the notify command ran"
else
    t_fail "notify command did not run (no marker)"
fi

if grep -q "do the thing" "$marker" 2>/dev/null \
   && grep -q "$ws" "$marker" 2>/dev/null; then
    t_ok "notify env carried \$JICHI_NOTIFY (title) + \$JICHI_CWD"
else
    t_fail "notify env incomplete: $(cat "$marker" 2>/dev/null)"
fi

t_done
