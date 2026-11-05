#!/bin/sh
# smoke: `setup --from-global` (and its --inherit subset) never copies a
# literal `apiKey` from ~/.jichi into the project config (D-24), and never
# copies the literal secret members of an mcp server or a tool either
# (M1106-b, the D-24 neighbour): mcpServers[].env, mcpServers[].headers and
# tools[].env.
#
# The bug: jc_setup_inherit_config copied the global source verbatim -- whole
# config or the --inherit selected keys -- so a literal "apiKey" on a model
# object and on the top-level search object landed in a 0644 local/config.json,
# under a _comment that promised "apiKeyEnv only, never a literal key".
# `apiKeyEnv` (the env-var NAME) is legitimate and must survive; only the
# literal value is dropped. A one-time stderr warning names the affected model.
#
# M1106-b: the same promise covers the mcpServers and tools entries -- their
# name/command/args survive, but their literal env/headers members are dropped
# (one stderr warning per affected entry: env, headers, then the tool's env).
#
# jsonq is a dot-path extractor (tests/tools/jsonq.c): a missing path is rc 1,
# so `! "$JQ" -q '.models[0].apiKey'` asserts the member is ABSENT.
#
# OFFLINE: a pure config-write test -- no model call, no mockmodel.
# THE NO-ECHO DISCIPLINE (setup_key_env.sh): the literal secret is referenced
# only by a marker substring in failure lines, never pasted.
. "$(dirname "$0")/_smoke.sh"

t_plan 14
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# Five distinct fake literals, each with a unique marker so a failure line can
# name WHICH one leaked without printing the whole value.
MK1='MKM'
MK2='MKS'
MKE='MKE'
MKH='MKH'
MKT='MKT'
KEY1="LIT-$MK1-0001"
KEY2="LIT-$MK2-0002"
ENVVAL="LIT-$MKE-0003"
HEADVAL="LIT-$MKH-0004"
TOOLVAL="LIT-$MKT-0005"

# --- global config carrying literal keys on model + search, and literal
# --- secret members (env + headers) on the mcp server + env on the tool --
cat > "$HOME/.jichi" <<EOFGLOBAL
{
  "lowResource": false,
  "models": [
    { "name": "kg", "provider": "openai", "model": "m1",
      "apiKey": "$KEY1", "apiKeyEnv": "ENVVAR", "roles": ["chat"] }
  ],
  "search": { "url": "http://x", "apiKey": "$KEY2", "apiKeyEnv": "SENV" },
  "mcpServers": [
    { "name": "srv", "command": "node", "args": ["-e", "x"],
      "env": { "MCP_TOK": "$ENVVAL" },
      "headers": [ "Authorization: Bearer $HEADVAL" ] }
  ],
  "tools": [
    { "name": "tool", "env": { "TOOL_TOK": "$TOOLVAL" } }
  ]
}
EOFGLOBAL

# --- 1-4: --from-global (whole config) --------------------------------------
ws=$(smoke_tmp)
(cd "$ws" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --from-global --config-target local < /dev/null \
    > "$tmp/fg.out" 2> "$tmp/fg.err"); rc1=$?
CFG="$ws/local/config.json"

if [ $rc1 -eq 0 ] && [ -f "$CFG" ] && "$JQ" -q '.' "$CFG" >/dev/null 2>&1; then
    t_ok "--from-global writes a valid local/config.json (rc=$rc1)"
else
    t_fail "--from-global: rc=$rc1, present=$([ -f "$CFG" ] && echo yes || echo no), valid=$("$JQ" -q '.' "$CFG" >/dev/null 2>&1 && echo yes || echo no)"
fi

if ! "$JQ" -q '.models[0].apiKey' "$CFG" 2>/dev/null && \
   ! "$JQ" -q '.search.apiKey' "$CFG" 2>/dev/null && \
   ! grep -q "$MK1" "$CFG" && ! grep -q "$MK2" "$CFG"; then
    t_ok "no literal apiKey survives (model + search, both markers absent)"
else
    t_fail "a literal apiKey leaked: mk1=$(grep -c "$MK1" "$CFG" 2>/dev/null || echo 0), mk2=$(grep -c "$MK2" "$CFG" 2>/dev/null || echo 0)"
fi

if [ "$("$JQ" '.models[0].apiKeyEnv' "$CFG" 2>/dev/null)" = "ENVVAR" ] && \
   [ "$("$JQ" '.search.apiKeyEnv' "$CFG" 2>/dev/null)" = "SENV" ]; then
    t_ok "apiKeyEnv (the env-var name) is kept on model and search"
else
    t_fail "apiKeyEnv lost: model=$("$JQ" '.models[0].apiKeyEnv' "$CFG" 2>/dev/null), search=$("$JQ" '.search.apiKeyEnv' "$CFG" 2>/dev/null)"
fi

if grep -q 'kg' "$tmp/fg.err" && grep -q 'dropped a literal apiKey' "$tmp/fg.err"; then
    t_ok "the stderr warning names the affected model (kg)"
else
    t_fail "missing/unnamed warning: $(head_bytes 200 "$tmp/fg.err")"
fi

# M1106-b: the mcpServers/tools literal secret members do not survive.
if ! "$JQ" -q '.mcpServers[0].env' "$CFG" 2>/dev/null && \
   ! "$JQ" -q '.mcpServers[0].headers' "$CFG" 2>/dev/null && \
   ! "$JQ" -q '.tools[0].env' "$CFG" 2>/dev/null && \
   ! grep -q "$MKE" "$CFG" && ! grep -q "$MKH" "$CFG" && ! grep -q "$MKT" "$CFG"; then
    t_ok "no literal mcpServers/tools secret members survive (env + headers + tool env, all three markers absent)"
else
    t_fail "a literal mcpServers/tools secret member leaked: mke=$(grep -c "$MKE" "$CFG" 2>/dev/null || echo 0), mkh=$(grep -c "$MKH" "$CFG" 2>/dev/null || echo 0), mkt=$(grep -c "$MKT" "$CFG" 2>/dev/null || echo 0)"
fi

if [ "$("$JQ" '.mcpServers[0].command' "$CFG" 2>/dev/null)" = "node" ] && \
   [ "$("$JQ" '.mcpServers[0].name' "$CFG" 2>/dev/null)" = "srv" ] && \
   [ "$("$JQ" '.tools[0].name' "$CFG" 2>/dev/null)" = "tool" ]; then
    t_ok "the mcpServers/tools entries themselves survive (name + command)"
else
    t_fail "an mcpServers/tools entry lost its non-secret fields: mcp_cmd=$("$JQ" '.mcpServers[0].command' "$CFG" 2>/dev/null), mcp_name=$("$JQ" '.mcpServers[0].name' "$CFG" 2>/dev/null), tool_name=$("$JQ" '.tools[0].name' "$CFG" 2>/dev/null)"
fi

if grep -q 'srv' "$tmp/fg.err" && \
   grep -q 'dropped a literal env from mcp server' "$tmp/fg.err" && \
   grep -q 'dropped literal headers from mcp server' "$tmp/fg.err" && \
   grep -q 'dropped a literal env from tool' "$tmp/fg.err"; then
    t_ok "the stderr warnings name the affected mcp server (srv) and tool"
else
    t_fail "missing/unnamed mcpServers/tools warning: $(head_bytes 400 "$tmp/fg.err")"
fi

# --- 8-11: --inherit "models,search" (subset) + the mcpServers/tools arm -----
ws2=$(smoke_tmp)
(cd "$ws2" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --from-global --inherit "models,search" --config-target local < /dev/null \
    > "$tmp/in.out" 2> "$tmp/in.err"); rc2=$?
CFG2="$ws2/local/config.json"

if [ $rc2 -eq 0 ] && [ -f "$CFG2" ] && \
   ! "$JQ" -q '.models[0].apiKey' "$CFG2" 2>/dev/null && \
   ! "$JQ" -q '.search.apiKey' "$CFG2" 2>/dev/null && \
   ! grep -q "$MK1" "$CFG2" && ! grep -q "$MK2" "$CFG2"; then
    t_ok "--inherit subset copies no literal apiKey (rc=$rc2)"
else
    t_fail "--inherit subset: rc=$rc2, present=$([ -f "$CFG2" ] && echo yes || echo no), mk1=$(grep -c "$MK1" "$CFG2" 2>/dev/null || echo 0), mk2=$(grep -c "$MK2" "$CFG2" 2>/dev/null || echo 0)"
fi

if [ "$("$JQ" '.models[0].apiKeyEnv' "$CFG2" 2>/dev/null)" = "ENVVAR" ] && \
   [ "$("$JQ" '.search.apiKeyEnv' "$CFG2" 2>/dev/null)" = "SENV" ]; then
    t_ok "--inherit subset keeps apiKeyEnv on model and search"
else
    t_fail "--inherit subset lost apiKeyEnv: model=$("$JQ" '.models[0].apiKeyEnv' "$CFG2" 2>/dev/null), search=$("$JQ" '.search.apiKeyEnv' "$CFG2" 2>/dev/null)"
fi

if grep -q 'kg' "$tmp/in.err" && grep -q 'dropped a literal apiKey' "$tmp/in.err"; then
    t_ok "--inherit subset warning names the model"
else
    t_fail "--inherit subset warning missing: $(head_bytes 200 "$tmp/in.err")"
fi

# M1106-b subset arm: a second run that also inherits the secret members must
# still drop them (the --inherit "mcpServers,tools" subset re-parses a copy).
ws2b=$(smoke_tmp)
(cd "$ws2b" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --from-global --inherit "mcpServers,tools" --config-target local < /dev/null \
    > "$tmp/inb.out" 2> "$tmp/inb.err"); rc2b=$?
CFG2B="$ws2b/local/config.json"

if [ $rc2b -eq 0 ] && [ -f "$CFG2B" ] && \
   ! "$JQ" -q '.mcpServers[0].env' "$CFG2B" 2>/dev/null && \
   ! "$JQ" -q '.mcpServers[0].headers' "$CFG2B" 2>/dev/null && \
   ! "$JQ" -q '.tools[0].env' "$CFG2B" 2>/dev/null && \
   ! grep -q "$MKE" "$CFG2B" && ! grep -q "$MKH" "$CFG2B" && ! grep -q "$MKT" "$CFG2B"; then
    t_ok "--inherit \"mcpServers,tools\" subset copies no literal secret member (rc=$rc2b)"
else
    t_fail "--inherit mcpServers/tools subset: rc=$rc2b, present=$([ -f "$CFG2B" ] && echo yes || echo no), mke=$(grep -c "$MKE" "$CFG2B" 2>/dev/null || echo 0), mkh=$(grep -c "$MKH" "$CFG2B" 2>/dev/null || echo 0), mkt=$(grep -c "$MKT" "$CFG2B" 2>/dev/null || echo 0)"
fi

# --- 12-14: control -- a global config with ONLY apiKeyEnv inherits cleanly --
cat > "$HOME/.jichi" <<EOFCLEAN
{
  "lowResource": false,
  "models": [
    { "name": "kg", "provider": "openai", "model": "m1",
      "apiKeyEnv": "ENVVAR", "roles": ["chat"] }
  ],
  "search": { "url": "http://x", "apiKeyEnv": "SENV" }
}
EOFCLEAN

ws3=$(smoke_tmp)
(cd "$ws3" && with_deadline 60 "$BIN" setup --non-interactive --preset generic \
    --from-global --config-target local < /dev/null \
    > "$tmp/cl.out" 2> "$tmp/cl.err"); rc3=$?
CFG3="$ws3/local/config.json"

if [ $rc3 -eq 0 ] && [ -f "$CFG3" ] && \
   [ "$("$JQ" '.models[0].apiKeyEnv' "$CFG3" 2>/dev/null)" = "ENVVAR" ] && \
   [ "$("$JQ" '.search.apiKeyEnv' "$CFG3" 2>/dev/null)" = "SENV" ]; then
    t_ok "control (no literal key) still inherits apiKeyEnv (rc=$rc3)"
else
    t_fail "control: rc=$rc3, present=$([ -f "$CFG3" ] && echo yes || echo no), model=$("$JQ" '.models[0].apiKeyEnv' "$CFG3" 2>/dev/null), search=$("$JQ" '.search.apiKeyEnv' "$CFG3" 2>/dev/null)"
fi

if ! grep -q 'dropped a literal apiKey' "$tmp/cl.err"; then
    t_ok "control emits no dropped-key warning"
else
    t_fail "control wrongly warned: $(grep 'dropped' "$tmp/cl.err" | head -1)"
fi

if ! "$JQ" -q '.models[0].apiKey' "$CFG3" 2>/dev/null; then
    t_ok "control: no apiKey member appears (none was there to drop)"
else
    t_fail "control: an apiKey member appeared where there was none"
fi

t_done
