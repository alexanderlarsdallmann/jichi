#!/bin/sh
# smoke: every verb the `config` subcommand ADVERTISES must do its job (M326c).
#
# `--help`, `describe` (an interface-contract surface) and CONVERT.md all
# promised `config [path|show|validate]`. Two dispatch sites for `config`
# existed in main.c; the first (show/set/telemetry) always returned, so the
# second -- the one serving path/validate -- was unreachable. Both read-only
# verbs answered "config editing is off": an error about a permission the user
# never asked for. Nothing failed, because nothing checked that an advertised
# verb produced an answer rather than a diagnostic.
#
# The gate belongs to the EDITING verbs only, so the checks below pin both
# halves: read-only verbs work with configEditable off, and an unknown verb
# gets usage rather than the gate's message.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
# This driver's SUBJECT is the config-resolution chain (a project local/
# overlaying ~/.jichi), so the M376 ambient-config pin must not hijack it.
# Its isolation is its own: an isolated $HOME and a cd into its own $ws.
unset JC_CONFIG
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A project config (no configEditable) overlaying a global one, so `path`
# is exercised on the MERGED case -- the one the dead implementation got
# wrong, since it re-derived a single file from the precedence rules.
cat > "$HOME/.jichi" <<'EOF'
{"models":[{"name":"global","provider":"openai","model":"g",
"apiBase":"http://127.0.0.1:1/v1"}],"lowResource":false}
EOF
mkdir -p "$ws/local"
cat > "$ws/local/config.json" <<'EOF'
{"models":[{"name":"proj","provider":"openai","model":"p",
"apiBase":"http://127.0.0.1:1/v1"}],"lowResource":false}
EOF

run_cfg() {
    (cd "$ws" && with_deadline 30 "$BIN" config "$@" < /dev/null \
        > "$tmp/out" 2> "$tmp/err")
    echo $?
}

# --- path: names the file(s) in effect, on stdout, exit 0 --------------------
rc=$(run_cfg path)
if [ "$rc" = 0 ] && grep -q 'local/config.json' "$tmp/out"; then
    t_ok "config path names the project config"
else
    t_fail "config path rc=$rc out=[$(head_bytes 120 "$tmp/out")] err=[$(head_bytes 120 "$tmp/err")]"
fi

# Both halves of a merge, not just one: the dead copy reported a single file.
if grep -q '\.jichi' "$tmp/out" && grep -q 'merged' "$tmp/out"; then
    t_ok "config path reports BOTH sources when they are merged"
else
    t_fail "merged sources not reported: [$(head_bytes 160 "$tmp/out")]"
fi

# --- validate: confirms the parse + counts models ---------------------------
rc=$(run_cfg validate)
if [ "$rc" = 0 ] && grep -q '^OK:' "$tmp/out" && grep -q '2 model(s)' "$tmp/out"; then
    t_ok "config validate confirms the parse and counts the merged models"
else
    t_fail "config validate rc=$rc out=[$(head_bytes 160 "$tmp/out")]"
fi

# --- the read-only verbs must NOT be gated on configEditable ----------------
# The exact regression: they fell through to the editing gate.
if ! grep -qi 'config editing is off' "$tmp/err"; then
    t_ok "validate is not gated on configEditable"
else
    t_fail "read-only verb hit the editable gate: [$(head_bytes 120 "$tmp/err")]"
fi

# --- show: unchanged summary (pinned separately by tui_model_name.sh) -------
rc=$(run_cfg show)
if [ "$rc" = 0 ] && grep -q '^model:' "$tmp/out"; then
    t_ok "config show still prints the resolved summary"
else
    t_fail "config show rc=$rc out=[$(head_bytes 120 "$tmp/out")]"
fi

# --- an unknown verb gets usage, not the gate's message ---------------------
rc=$(run_cfg wibble)
if [ "$rc" != 0 ] && grep -q 'usage: config' "$tmp/err"; then
    t_ok "an unknown verb gets the usage line"
else
    t_fail "unknown verb rc=$rc err=[$(head_bytes 160 "$tmp/err")]"
fi

# --- an editing verb IS still gated -----------------------------------------
rc=$(run_cfg set craft false)
if [ "$rc" != 0 ] && grep -qi 'config editing is off' "$tmp/err"; then
    t_ok "config set is still refused when configEditable is off"
else
    t_fail "editing gate lost: rc=$rc err=[$(head_bytes 160 "$tmp/err")]"
fi

t_done
