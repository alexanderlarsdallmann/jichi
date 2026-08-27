#!/bin/sh
# smoke: the tool round trip -- the mock orders a read_file call; the
# captured requests prove tools were advertised (req.1) and the tool
# actually ran (req.2 carries the tool-role result with the file's
# content); the final answer reaches stdout.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
echo "hello from the note file" > "$ws/note.txt"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"note.txt"}
rule
  match "\"role\":\"tool\""
  text TOOL_ANSWER_OK_42
rule
  status 500
  body {"error":"unexpected request"}
EOF

mm_start "$tmp/replies.mm" "$tmp" 2
write_config "$tmp/config.json" "$MM_PORT"

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --auto --no-session -q -p "read the note" < /dev/null); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "headless --auto exits 0"
else
    t_fail "headless --auto rc=$rc"
fi
case "$out" in
    *TOOL_ANSWER_OK_42*) t_ok "final answer on stdout" ;;
    *) t_fail "answer missing: $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if grep -q '"tools"' "$tmp/req.1"; then
    t_ok "request 1 advertises tools"
else
    t_fail "no tools array in request 1"
fi
if grep -q '"role":"tool"' "$tmp/req.2" 2>/dev/null; then
    t_ok "request 2 carries the tool result"
else
    t_fail "no tool-role message in request 2"
fi
if grep -q "hello from the note file" "$tmp/req.2" 2>/dev/null; then
    t_ok "the tool result carries the file's content"
else
    t_fail "file content missing from the tool result"
fi

t_done
