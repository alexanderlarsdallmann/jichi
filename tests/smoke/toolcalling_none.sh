#!/bin/sh
# smoke: toolCalling "none" (M149) -- the model must NOT be advertised any
# tools. The mock answers NONE_BAD if a tools array is present in the
# request and NONE_OK otherwise, so a pass proves suppression at the
# source (the port of tests/e2e/toolcalling_none.py).
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"tools\""
  text NONE_BAD
rule
  text NONE_OK
EOF

mm_start "$tmp/replies.mm" "$tmp" 1
write_config "$tmp/config.json" "$MM_PORT" '' '"toolCalling":"none"'

out=$(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
      --no-session -q -p "plan something" < /dev/null); rc=$?
mm_stop

case "$out" in
    *NONE_BAD*)
        t_fail "tools were advertised despite toolCalling: none" ;;
    *NONE_OK*)
        t_ok "no tools array in the request" ;;
    *)
        t_fail "no marker on stdout (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if [ $rc -eq 0 ]; then
    t_ok "run exits 0"
else
    t_fail "rc=$rc"
fi

t_done
