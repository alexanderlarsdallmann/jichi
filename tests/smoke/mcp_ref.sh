#!/bin/sh
# smoke: @mcp:<uri> resource reference (M47). A plain message carrying
# `@mcp:mem://notes` must inline that MCP resource's text into the user
# turn (via read_mcp_resource -> the mock's resources/read). The mock
# chat model answers REF_OK only if the resource body (NOTES_BODY)
# reached it. (Port of tests/e2e/mcp_ref.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
smoke_write_mock_mcp "$tmp/mock_mcp.sh"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "NOTES_BODY"
  text REF_OK
rule
  text REF_MISSING
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"chat","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "mcpServers":[{"name":"mock","command":"/bin/sh","args":["$tmp/mock_mcp.sh"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 40 "$BIN" --config "$tmp/config.json" \
      -q --no-session -p "summarize @mcp:mem://notes please" \
      < /dev/null); rc=$?
mm_stop

case "$out" in
    *REF_OK*) t_ok "the @mcp resource text was inlined into the message" ;;
    *REF_MISSING*) t_fail "the reference expanded but carried no body" ;;
    *) t_fail "no marker (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac
if grep -q "NOTES_BODY" "$tmp/req.1" 2>/dev/null; then
    t_ok "the resource body is in the outgoing request"
else
    t_fail "NOTES_BODY missing from the captured request"
fi

t_done
