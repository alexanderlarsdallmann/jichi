#!/bin/sh
# smoke: the ask_user tool's non-interactive path (M34d/F4) -- the
# safety-critical one: a headless/--auto run with no ask delegate must
# NEVER block waiting for input. The mock calls ask_user; the tool must
# return the "proceed with your best judgment" note (visible in the
# second request's tool result) and the turn must complete.
# (Port of tests/e2e/ask.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool ask_user {"question":"Which database should I use?","options":["postgres","sqlite"]}
rule
  text ASK_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

out=$(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto --no-stdin -p "set up the project" \
      < /dev/null); rc=$?
mm_stop

if [ -f "$tmp/req.2" ]; then
    t_ok "the model was called twice (ask_user did not hang)"
else
    t_fail "no second request -- ask_user may have blocked (rc=$rc)"
fi
if grep -q "Proceed with your best judgment" "$tmp/req.2" 2>/dev/null; then
    t_ok "the non-interactive proceed note reached the model"
else
    t_fail "proceed note missing from the tool result"
fi
case "$out" in
    *ASK_DONE*) t_ok "the turn completed" ;;
    *) t_fail "turn incomplete: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

t_done
