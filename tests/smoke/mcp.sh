#!/bin/sh
# smoke: MCP resources + prompts (M43). Drives jichi's `mcp` subcommand
# against the Python-free mock MCP stdio server (smoke_write_mock_mcp) and
# asserts resources/prompts are discovered, listed, and fetched
# (resources/read, prompts/get route to the server). Exercises the real
# stdio transport + manager. (Port of tests/e2e/mcp.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
smoke_write_mock_mcp "$tmp/mock_mcp.sh"

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"x","apiKey":"k"}],
 "mcpServers":[{"name":"mock","command":"/bin/sh","args":["$tmp/mock_mcp.sh"]}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

mcp() { with_deadline 30 "$BIN" --config "$tmp/config.json" mcp "$@" \
        < /dev/null 2>&1; }

out=$(mcp resources)
if printf '%s' "$out" | grep -q "mem://notes" \
   && printf '%s' "$out" | grep -q "team notes"; then
    t_ok "resources/list discovered the resource"
else
    t_fail "resources not listed: $(printf '%s' "$out" | head_bytes 150)"
fi

out=$(mcp read "mem://notes")
case "$out" in
    *"NOTES_BODY for mem://notes"*) t_ok "resources/read returned the content" ;;
    *) t_fail "read got: $(printf '%s' "$out" | head_bytes 150)" ;;
esac

out=$(mcp prompts)
case "$out" in
    *greet*) t_ok "prompts/list discovered the prompt" ;;
    *) t_fail "prompts not listed: $(printf '%s' "$out" | head_bytes 150)" ;;
esac

out=$(mcp prompt greet)
case "$out" in
    *"GREETING from greet"*) t_ok "prompts/get rendered the message" ;;
    *) t_fail "prompt get got: $(printf '%s' "$out" | head_bytes 150)" ;;
esac

# M395: the OBJECT shape (Claude Code / Continue key theirs by server name) used
# to configure nothing in silence -- no servers, no tools, no warning, nothing to
# search for. A configured-but-ignored server is worse than an absent one.
objcfg=$(smoke_tmp)
cat > "$objcfg/config.json" <<'EOF'
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"x",
 "apiBase":"http://127.0.0.1:1/v1"}],
 "mcpServers":{"fs":{"command":"npx","args":["-y","server-filesystem"]}}}
EOF
out=$(with_deadline 30 "$BIN" --config "$objcfg/config.json" doctor < /dev/null 2>&1)
case "$out" in
    *"mcpServers must be an ARRAY"*)
        t_ok "the object-shaped mcpServers is refused loudly, not ignored" ;;
    *)  t_fail "no ARRAY warning for object-shaped mcpServers: $(printf '%s' "$out" | grep -i mcp | head -1)" ;;
esac

t_done
