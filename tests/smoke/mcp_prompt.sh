#!/bin/sh
# smoke: MCP prompts as slash commands (M43/M49). A headless turn invokes
# `/greet World` -- a prompt advertised by the mock MCP server; jichi
# resolves it (prompts/get), maps the positional arg to `who`, renders
# the messages, and submits them as the user turn. The mock chat model
# answers PROMPT_OK only if the prompt's rendered text (who=World)
# reached it. (Port of tests/e2e/mcp_prompt.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
smoke_write_mock_mcp "$tmp/mock_mcp.sh"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "who=World"
  text PROMPT_OK
rule
  text PROMPT_MISSING
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"chat","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "mcpServers":[{"name":"mock","command":"/bin/sh","args":["$tmp/mock_mcp.sh"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 40 "$BIN" --config "$tmp/config.json" \
      -q --no-session -p "/greet World" < /dev/null); rc=$?
mm_stop

case "$out" in
    *PROMPT_OK*) t_ok "the prompt arg (who=World) reached the model" ;;
    *PROMPT_MISSING*) t_fail "the prompt rendered but the arg was lost" ;;
    *) t_fail "no marker (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if [ -f "$tmp/req.1" ]; then
    t_ok "the resolved prompt was submitted as a turn"
else
    t_fail "no model request -- the prompt did not resolve"
fi

t_done
