#!/bin/sh
# smoke: the basic headless round trip -- `-p` against the mock model
# prints the answer on stdout and the captured outgoing request carries
# the model id and the prompt.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text BASIC_ANSWER_OK_31
EOF

mm_start "$tmp/replies.mm" "$tmp" 1
write_config "$tmp/config.json" "$MM_PORT"

out=$(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp/config.json" \
      --no-session -q -p "hello smoke tier" < /dev/null); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "headless -p exits 0"
else
    t_fail "headless -p rc=$rc"
fi
case "$out" in
    *BASIC_ANSWER_OK_31*) t_ok "answer marker on stdout" ;;
    *) t_fail "marker missing from stdout: $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if grep -q '"model"' "$tmp/req.1"; then
    t_ok "request carries a model id"
else
    t_fail "no model id in the captured request"
fi
if grep -q "hello smoke tier" "$tmp/req.1"; then
    t_ok "request carries the prompt"
else
    t_fail "prompt missing from the captured request"
fi

# M375: an unquoted / `--`-form prompt is several argv words; only the first
# used to survive (`jichi -- tell me a joke` asked the model "tell", though
# --help had promised "all following args as the prompt"). The whole phrase
# must reach the wire.
tmp2=$(smoke_tmp)
cat > "$tmp2/replies.mm" <<'EOF'
wire openai
rule
  text JOINED_ANSWER_OK_75
EOF
mm_start "$tmp2/replies.mm" "$tmp2" 1
write_config "$tmp2/config.json" "$MM_PORT"

out2=$(cd "$ws" && with_deadline 45 "$BIN" --config "$tmp2/config.json" \
      --no-session -q -- tell me a joke please < /dev/null); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "multi-word positional prompt exits 0"
else
    t_fail "multi-word prompt rc=$rc"
fi
if grep -q "tell me a joke please" "$tmp2/req.1"; then
    t_ok "the whole multi-word prompt reaches the request"
else
    t_fail "joined prompt missing: $(grep -o '"content":"[^"]*"' "$tmp2/req.1" | head_bytes 120)"
fi

t_done
