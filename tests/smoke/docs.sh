#!/bin/sh
# smoke: the external documentation index (M34a) -- `docs search`. The
# mock embeddings endpoint (mockmodel's `embed` action) answers each
# input with [count("hooks"), count("state"), 0.1], so cosine ranking is
# fully deterministic: a query about hooks must rank hooks.md ahead of
# state.md.
# (Port of tests/e2e/docs.py, M213.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
docs=$(smoke_tmp)

cat > "$docs/hooks.md" <<'EOF'
# Hooks

Use hooks hooks hooks to manage lifecycle.
EOF
cat > "$docs/state.md" <<'EOF'
# State

State state state holds the component data.
EOF

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  embed hooks state
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["embed"]}],
 "docs":[{"name":"docs","path":"$docs"}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

(cd "$docs" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    docs search docs "how do hooks work" < /dev/null \
    > "$tmp/out" 2>"$tmp/err"); rc=$?
mm_stop

if [ -f "$tmp/req.1" ]; then
    t_ok "the embeddings endpoint was queried"
else
    t_fail "no embeddings request (rc=$rc): $(tail -c 200 "$tmp/err")"
fi
if grep -q "hooks.md" "$tmp/out"; then
    t_ok "hooks.md is in the results"
else
    t_fail "hooks.md missing: $(head_bytes 200 "$tmp/out")"
fi
# ranking: hooks.md must appear before state.md (if the latter shows)
hooks_line=$(grep -n "hooks.md" "$tmp/out" | head -1 | cut -d: -f1)
state_line=$(grep -n "state.md" "$tmp/out" | head -1 | cut -d: -f1)
if [ -z "$state_line" ] || [ "${hooks_line:-9999}" -lt "$state_line" ]; then
    t_ok "the hooks doc ranks first"
else
    t_fail "state.md ranked ahead of hooks.md"
fi

t_done
