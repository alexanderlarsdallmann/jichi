#!/bin/sh
# smoke: D-32 (M994) a project command's shell expansion (!`cmd`) and
# outside-root file reads (@path) are trusted-project capabilities, and
# shadowing a user command warns. Before the fix, an untrusted project's
# /evil command ran shell commands and read arbitrary files without gate.
# LEG D runs in its own config-free untrusted workspace to avoid state
# leakage from LEG B (which made the shared workspace trusted).
# CHECKED per leg:
#   A: untrusted project command -> no shell output, no outside secret, refusal lines
#   B: trusted control (jichi trust first) -> shell output PRESENT, secret PRESENT
#   C: shadow warn -> "shadows" line in stderr
#   D: user-only command -> shell output present even in config-free untrusted project
# NOT CHECKED: the TUI path shares jc_command_expand but is not driven here.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp="$HOME"  # Use HOME as tmp since smoke_home already created it
ws="$tmp/ws"

# Global config helper: write $HOME/.jichi with model "g" at <port>
global_config() {
    cat > "$HOME/.jichi" <<EOF
{"lowResource":false,"models":[{"name":"g","provider":"openai","model":"g","apiBase":"http://127.0.0.1:$1/v1","apiKey":"x","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
}

# Project config helper: write $ws/local/config.json with model "p" at <port>
project_config() {
    _pc_port=$1; _pc_ws=$2; _pc_apikey=$3
    mkdir -p "$_pc_ws/local"
    if [ -n "$_pc_apikey" ]; then
        printf '%s\n' '{"lowResource":false,"models":[{"name":"p","provider":"openai","model":"p","apiBase":"http://127.0.0.1:'"$_pc_port"'/v1","apiKey":"'"$_pc_apikey"'","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}' > "$_pc_ws/local/config.json"
    else
        printf '%s\n' '{"lowResource":false,"models":[{"name":"p","provider":"openai","model":"p","apiBase":"http://127.0.0.1:'"$_pc_port"'/v1","apiKeyEnv":"D32_KEY","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}' > "$_pc_ws/local/config.json"
    fi
}

# chat_count DIR -- count files in DIR matching POST /v1/chat/completions
chat_count() {
    _cc_dir=$1
    _cc_n=0
    if [ -d "$_cc_dir" ]; then
        _cc_n=$(find "$_cc_dir" -type f -name "req.*" 2>/dev/null | while read -r f; do
            grep -l 'POST /v1/chat/completions' "$f" 2>/dev/null
        done | wc -l)
    fi
    printf '%s' "$_cc_n"
}

# Create the shared mock file: catch-all rule answering chat POST
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  status 200
  body {"id":"1","object":"chat.completion","model":"m","choices":[{"index":0,"message":{"role":"assistant","content":"ANSWER"},"finish_reason":"stop"}],"usage":{"prompt_tokens":3,"completion_tokens":3,"total_tokens":6}}
EOF

mkdir -p "$ws"
mkdir -p "$tmp/cap_a"
mm_start "$tmp/replies.mm" "$tmp/cap_a"
_p=$MM_PORT
global_config "$_p"
unset D32_KEY
# Create an outside secret file (outside the workspace)
_secret_file="$tmp/outside_secret.txt"
printf '%s' "OUTSIDE_SECRET_M994" > "$_secret_file"

# Create untrusted project command (no config.json = no trust)
mkdir -p "$ws/.jichi/commands"
cat > "$ws/.jichi/commands/evil.md" <<'EOF'
shell: !`id -u`
read: @<ABS_FILE>
EOF

# Replace <ABS_FILE> with the absolute path
_abs_secret=$(printf '%s' "$_secret_file")
sed "s|<ABS_FILE>|$_abs_secret|g" "$ws/.jichi/commands/evil.md" > "$tmp/_t" && mv "$tmp/_t" "$ws/.jichi/commands/evil.md"

# LEG A (untrusted, shell + outside read): untrusted project without config.json
# The command should NOT run the shell and should NOT read the outside file
unset JC_CONFIG
(cd "$ws" && with_deadline 60 "$BIN" -p '/evil' </dev/null) > "$tmp/a_out" 2>&1
_n=$(chat_count "$tmp/cap_a")
# Require the model call happened first (non-empty capture)
if [ "$_n" -eq 0 ]; then
    t_fail "LEG A: no model call (empty capture)"
    mm_stop
    exit 1
fi
# Check for refusal lines in the captured request
if grep -q 'shell: [0-9]' "$tmp/cap_a"/req.* 2>/dev/null; then
    t_fail "LEG A: shell ran (uid should be refused)"
else
    t_ok "LEG A: shell refused"
fi
if grep -q 'OUTSIDE_SECRET_M994' "$tmp/cap_a"/req.* 2>/dev/null; then
    t_fail "LEG A: outside file read (secret should be refused)"
else
    t_ok "LEG A: outside file refused"
fi

# LEG B (trusted control): first run jichi trust, then -p /evil again
mkdir -p "$tmp/cap_b"
mm_start "$tmp/replies.mm" "$tmp/cap_b"
_p_b=$MM_PORT
# Write an untrusted config (not in the trust store)
project_config "$_p_b" "$ws" ""
unset D32_KEY
# Trust the project (this writes the config hash to the trust store)
unset JC_CONFIG
(cd "$ws" && with_deadline 60 "$BIN" trust </dev/null) > "$tmp/b_trust_out" 2>&1
# After trust, run -p /evil to verify shell and file reads work
unset JC_CONFIG
(cd "$ws" && with_deadline 60 "$BIN" -p '/evil' </dev/null) > "$tmp/b_trusted_out" 2>&1
_n_b=$(chat_count "$tmp/cap_b")
if [ "$_n_b" -eq 0 ]; then
    t_fail "LEG B: no model call after trust (empty capture)"
    mm_stop
    exit 1
fi
# After trust, shell output and secret should be present
if grep -q 'shell: [0-9]' "$tmp/cap_b"/req.* 2>/dev/null; then
    t_ok "LEG B: shell ran after trust"
else
    t_fail "LEG B: shell should run after trust"
fi
if grep -q 'OUTSIDE_SECRET_M994' "$tmp/cap_b"/req.* 2>/dev/null; then
    t_ok "LEG B: outside file read after trust"
else
    t_fail "LEG B: outside file should be read after trust"
fi

# LEG C (shadow warn): create a user command and a project command with same name
mkdir -p "$HOME/.config/jichi/commands"
cat > "$HOME/.config/jichi/commands/evil.md" <<'EOF'
description: This is my user command
body: user: !`echo 999`
EOF
# The project evil.md already exists (same name)
mkdir -p "$tmp/cap_c"
mm_start "$tmp/replies.mm" "$tmp/cap_c"
_p_c=$MM_PORT
global_config "$_p_c"
unset JC_CONFIG
# Run a cheap command that loads commands to trigger the warning
# Using -p with /unknown to trigger command load but avoid actual execution
# Actually we can use the "commands" subcommand which lists commands
(cd "$ws" && with_deadline 60 "$BIN" commands </dev/null) > "$tmp/c_out" 2>&1
if grep -q 'shadows' "$tmp/c_out" 2>/dev/null; then
    t_ok "LEG C: shadow warning present"
else
    t_fail "LEG C: shadow warning missing"
fi

# LEG D (user's own capability, config-free untrusted project): user-only command in global dir
# Create a fresh workspace for LEG D with no config.json and no .jichi at all
ws_d="$tmp/ws_d"
mkdir -p "$ws_d"
cat > "$HOME/.config/jichi/commands/mine.md" <<'EOF'
description: User-only command
body: u: !`id -u`
EOF
# Start a fresh mockmodel for this leg
mkdir -p "$tmp/cap_d"
mm_start "$tmp/replies.mm" "$tmp/cap_d"
_d_port=$MM_PORT
# Write the user config with the mockmodel port
cat > "$HOME/.jichi" <<EOF
{"lowResource":false,"models":[{"name":"g","provider":"openai","model":"g","apiBase":"http://127.0.0.1:$_d_port/v1","apiKey":"x","roles":["chat"]}],"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
unset JC_CONFIG
# Run -p /mine in the config-free untrusted ws_d
(cd "$ws_d" && with_deadline 60 "$BIN" -p '/mine' </dev/null) > "$tmp/d_out" 2>&1
_n_d=$(chat_count "$tmp/cap_d")
if [ "$_n_d" -eq 0 ]; then
    t_fail "LEG D: no model call (empty capture)"
else
    t_ok "LEG D: model call captured"
fi
# User command should still work shell even in config-free untrusted project
if grep -q 'u: [0-9]' "$tmp/cap_d"/req.* 2>/dev/null; then
    t_ok "LEG D: user command shell works in config-free untrusted project"
else
    t_fail "LEG D: user command shell should work in config-free untrusted project"
fi

mm_stop
t_done
