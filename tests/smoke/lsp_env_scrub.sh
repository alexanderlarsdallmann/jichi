#!/bin/sh
# smoke: language servers run with the registered secret env scrubbed (M1025, B-08=C-05).
#
# PROOF PLAN. The product fix is `sr.scrub_env = 1;` in spawn_server, so every LSP
# child (rust-analyzer, tsserver, gopls) inherits an environment with the registered
# secret names dropped (M130: JICHI_API_KEY, JLU_API_KEY, ANTHROPIC_API_KEY, ...).
# This driver proves it:
#   (1) PRECONDITION the env-dump file exists and is non-empty (proof the LSP was
#       spawned; hollow otherwise, because edit_file needs a prior read_file).
#   (2) the canary value (LSPCANARY...) does NOT appear in the dump.
#   (3) JLU_API_KEY= does NOT appear as a line in the dump.
#   (4) the run completed (a done row, so the instrument exercised the whole sequence).
#   (5) a NON-built-in configured name (MOCK_KEY) does NOT appear as a line in the
#       second dump (the registration leg holds, not just the built-in list).
#
# MOCK CONFIGURATION. models[0] with apiKeyEnv:JLU_API_KEY (NOT a literal apiKey -- the
# env form is what makes the variable a registered secret the scrub drops). lspServers
# with mocklsp --env-dump <path>, extensions:[txt]. The edit_file on a .txt triggers
# tu_append_diagnostics -> the LSP spawn.
#
# CORRECTED SEQUENCE (from the rejected attempt). Rule 1: tool read_file {"path":"lspfile.txt"}.
# Rule 2: tool edit_file {"path":"lspfile.txt","old_string":"old","new_string":"new"}.
# Rule 3: text done. The file must exist first (read_file needs the file to exist).

. "$(dirname "$0")/_smoke.sh"

G=/usr/bin/grep
[ -x "$G" ] || G=grep
t_plan 5
smoke_home
tmp=$(smoke_tmp)

# one <capdir> <env-dump-path> <apiKeyEnv-name>
case_run() {
    _cap="$1"; _env_dump="$2"; _mock_key="$3"
    mkdir -p "$_cap"
    cat > "$_cap/r.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"lspfile.txt"}
rule
  count 2
  tool edit_file {"path":"lspfile.txt","old_string":"old","new_string":"new"}
rule
  count 3
  text done
EOF
    mm_start_unbounded "$_cap/r.mm" "$_cap/cap"
    cat > "$_cap/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,
"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKeyEnv":"$_mock_key","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
"lspServers":[{"name":"mocklsp","command":"$SMOKE_TOOLS/mocklsp",
"args":["--env-dump","$_env_dump"],"extensions":["txt"]}]}
EOF
    _ws=$(smoke_tmp)
    printf 'old y\n' > "$_ws/lspfile.txt"
    ( cd "$_ws" && with_deadline 120 "$BIN" --config "$_cap/cfg.json" --no-session \
        --auto --output json -p "fix lspfile.txt" \
        < /dev/null > "$_cap/out" 2> "$_cap/err" )
    mm_stop
}

# --- the run ---------------------------------------------------------
CAP=$(smoke_tmp)
ENV_DUMP="$CAP/dump.txt"
export JLU_API_KEY="LSPCANARY-M1025-TEST-$(date +%s)"
case_run "$CAP" "$ENV_DUMP" JLU_API_KEY

# check 5: NON-built-in configured name (MOCK_KEY) must be absent
CAP2=$(smoke_tmp)
ENV2="$CAP2/dump2.txt"
export MOCK_KEY="LSPLINE-M1034-$(date +%s)"
case_run "$CAP2" "$ENV2" MOCK_KEY

# --- checks ----------------------------------------------------------

# Check 1: PRECONDITION the env-dump file exists and is non-empty.
if [ -f "$ENV_DUMP" ] && [ -s "$ENV_DUMP" ]; then
    t_ok "the env-dump file exists and is non-empty (the LSP was spawned)"
else
    t_fail "the env-dump file does not exist or is empty (the LSP was never spawned)"
fi

# Check 2: the canary value does NOT appear in the dump.
if ! $G -q "LSPCANARY-M1025-TEST" "$ENV_DUMP" 2>/dev/null; then
    t_ok "the canary value does NOT appear in the dump (scrubbed)"
else
    t_fail "the canary value appears in the dump (NOT scrubbed)"
fi

# Check 3: JLU_API_KEY= does NOT appear as a line in the dump.
if ! $G -q "^JLU_API_KEY=" "$ENV_DUMP" 2>/dev/null; then
    t_ok "JLU_API_KEY= does NOT appear as a line in the dump (name dropped)"
else
    t_fail "JLU_API_KEY= appears as a line in the dump (name NOT dropped)"
fi

# Check 4: the run completed (a done row, so the instrument exercised the whole sequence).
if [ -f "$CAP/out" ]; then
    if $G -q '"type":"done"' "$CAP/out" 2>/dev/null; then
        t_ok "the run completed (a done row was produced)"
    else
        t_fail "the run did not complete (no done row)"
    fi
else
    t_fail "the run output file does not exist - run may have failed"
fi

# Check 5: a NON-built-in configured name (MOCK_KEY) does NOT appear as a line in the second dump.
if [ ! -s "$ENV2" ]; then
    t_fail "second dump missing or empty (server not spawned)"
elif ! $G -q "^MOCK_KEY=" "$ENV2" 2>/dev/null; then
    t_ok "MOCK_KEY= does NOT appear in the second dump (the configured non-built-in name is scrubbed: the registration leg holds, not just the built-in list)"
else
    t_fail "MOCK_KEY= appears as a line in the second dump (the configured non-built-in name is NOT scrubbed): $($G '^MOCK_KEY=' "$ENV2" | head -1)"
fi

t_done
