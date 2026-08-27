#!/bin/sh
# smoke: a tool-fence refusal names the way forward (M360). Under the core
# tool profile the TOP-LEVEL agent is fenced (M74, opts.allow), so a model
# that calls a tool it remembers from pretraining -- spawn_subagent -- used
# to be told only "Tool not permitted by this agent's allowed-tools.": a
# cause with no way forward, the M342 message class that amplifies retry
# loops. The refusal must now name the tools that ARE available and say the
# refused one will not appear, and the run must still complete.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

{
    echo "wire openai"
    echo "rule"
    echo "  count 1"
    echo '  tool spawn_subagent {"task":"summarize the project"}'
    echo "rule"
    echo "  text done"
} > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp"
# write_config bakes toolProfile:full, so the config is hand-rolled here --
# the core profile IS the subject.
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"core","lowResource":false,
"maxRetries":0}
EOF
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --dump-requests "$tmp/reqs" \
    -p "delegate this" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

last=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | tail -1)
nreq=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | wc -l)

# --- 1: the exchange happened -------------------------------------------------
if [ -n "$last" ] && [ "$nreq" -ge 2 ]; then
    t_ok "$nreq request bodies captured"
else
    t_fail "too few captured requests ($nreq) -- the refusal never went back"
fi

# --- 2: the refusal states the cause with the refused name --------------------
if grep -q "'spawn_subagent' is not available to this agent" "$last"; then
    t_ok "the refusal names the refused tool and the cause"
else
    t_fail "refusal head missing from the wire"
fi

# --- 3: ...and the way forward: the tools that ARE available ------------------
if grep -q 'Available tools:' "$last" && grep -q 'search_code' "$last"; then
    t_ok "the refusal lists the available tools (way forward)"
else
    t_fail "no available-tools list in the refusal"
fi

# --- 4: the old no-way-forward text is gone -----------------------------------
if grep -q "not permitted by this agent's allowed-tools" "$last"; then
    t_fail "the old wrong-cause-class text is still on the wire"
else
    t_ok "the old no-way-forward text is retired"
fi

# --- 5: the run completed (a refusal is a value, not control flow) ------------
if grep -q 'done' "$tmp/out"; then
    t_ok "the run completed after the refusal"
else
    t_fail "run did not finish: $(head_bytes 150 "$tmp/err")"
fi

t_done
