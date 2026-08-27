#!/bin/sh
# smoke: the empty-answer warning (M167). When tools are advertised and
# the model returns neither a tool call nor any text, that triple is the
# signature of a malformed/rejected request (ANECDOTES #19) -- pre-M167 it
# was completely silent. The warning must reach stderr once, name the
# advertised tool count, and point at capture-and-replay rather than
# blaming the model. Deliberately NOT -q: the diagnostic is the point.
# (Port of tests/e2e/empty_answer.py, M211; the M166 empty signature --
# a role delta with no content, then stop -- via sse-file.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/empty.sse" <<'EOF'
data: {"choices":[{"index":0,"delta":{"role":"assistant"},"finish_reason":null}]}

data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}

data: [DONE]

EOF

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  sse-file $tmp/empty.sse
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "read a.txt and summarise it" \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop

# the pass must not be vacuous: the request really advertised tools
if grep -q '"tools"' "$tmp/req.1" 2>/dev/null; then
    t_ok "the request carried a tools array (pass is not vacuous)"
else
    t_fail "no tools array in the captured request"
fi

if grep -q "no tool call and no text" "$tmp/err"; then
    t_ok "the empty-answer warning reached stderr"
else
    t_fail "no warning: $(tail -c 300 "$tmp/err")"
fi

if grep -q "tools were advertised" "$tmp/err"; then
    t_ok "the warning names the advertised tool count"
else
    t_fail "tool count missing from the warning"
fi

if grep -q "docs/LOCAL_MODELS.md" "$tmp/err"; then
    t_ok "the warning points at the troubleshooting order"
else
    t_fail "no pointer to docs/LOCAL_MODELS.md"
fi

if [ "$(grep -c "no tool call and no text" "$tmp/err")" -eq 1 ]; then
    t_ok "the warning fires exactly once per session"
else
    t_fail "warning fired $(grep -c "no tool call and no text" "$tmp/err") times"
fi

t_done
