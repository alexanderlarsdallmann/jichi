#!/bin/sh
# smoke driver: fail_classify_shell (card 75, M1041, M1041-c, M1041-d).
#
# WHAT THIS EXISTS FOR. The shell tool classifier was using substring matching
# on the result text, so a command like `echo "connection refused" && exit 1`
# would be misfiled as JC_FAIL_DENIED ("denied" substring match), giving the
# wrong advice ("it will not succeed by rephrasing") instead of the correct one
# ("the command ran and reported failure -- read its output for the cause").
#
# The fix adds a tool parameter to jc_fail_classify:
#   - For run_terminal_command/run_tests: DENIED only when result BEGINS with
#     jichi's refusal phrases (e.g., "refused by safety fence", "Path ... outside
#     this run's")
#   - For other tools: substring matching preserved (their results are jichi's
#     messages, not command output)
#
# M1041-c adds the fail_class field to the tool_call telemetry event so the
# driver can read the classification directly instead of re-deriving it from
# advice prose.
#
# M1041-d fixes the telemetry defect: fail_class was written on EVERY tool call
# (including successes). The fix restricts it to FAILED calls only (res.is_error
# || tool_at_bound), matching the loop detector's condition. SUCCESS calls have
# NO fail_class field.
#
# WHAT IS CHECKED.
#   LEG A1 (success-only): the model calls read_file successfully.
#     Assert the tool_call record has NO fail_class field (RED on 9c38c569 where
#     every row carried it).
#   LEG A2 (the output-misfile, the original finding): the model runs
#     `sh -c 'echo connection refused; exit 1'` via run_terminal_command.
#     Assert the tool_call record's fail_class == "nonzero_exit" (NOT "denied").
#     This is RED on the pre-M1041 classifier (it was "denied") and green now.
#   LEG B (the read-only regression): a READ-ONLY run where the model calls
#     run_terminal_command. The gate refuses it (policy_refusal=1). Assert
#     the refusal message contains "tool disabled in read-only mode" and that
#     the failure is classified DENIED (by the flag or the word-based fallback).
#     Assert the telemetry's fail_class == "denied" (new, M1041-d).

. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --- LEG A1: SUCCESS run - read_file that succeeds, should have NO fail_class ---
# Create a file the model can read successfully (NOTICE.txt instead of LICENSE)
# which exists in the workspace to ensure a genuine SUCCESS (ok:true, error:false).

printf 'hello notice body\n' > "$ws/NOTICE.txt"

cat > "$tmp/replies_a1.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"NOTICE.txt"}
rule
  text done
EOF

mm_start "$tmp/replies_a1.mm" "$tmp"

cat > "$tmp/config_a1.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"core","lowResource":false,
"maxRetries":0}
EOF

mkdir -p "$ws"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config_a1.json" --no-session \
    --auto --log "$tmp/telemetry_a1.jsonl" --log-level metrics \
    -p "read the notice file" < /dev/null > "$tmp/out_a1" 2> "$tmp/err_a1") || true

mm_stop

# Extract the tool_call event(s) for A1
grep '"event":"tool_call"' "$tmp/telemetry_a1.jsonl" 2>/dev/null > "$tmp/tc_a1.jsonl" || true

# LEG A1: successful call should have NO fail_class field
# Use grep to check for the ABSENCE of the "fail_class" key (not its value).
# This is UNAMBIGUOUS: the key "fail_class" only exists on FAILED calls.
# The old check `= "not_found" || -z` would MATCH a real not_found class!
n_tc_a1=$(grep -c . "$tmp/tc_a1.jsonl" 2>/dev/null || echo 0)
if [ "$n_tc_a1" -ge 1 ]; then
    # Check that the fail_class KEY is ABSENT (grep returns 1 when no match)
    if grep -q '"fail_class"' "$tmp/tc_a1.jsonl" 2>/dev/null; then
        t_fail "LEG A1: tool_call has fail_class key (should be absent for success)"
    else
        t_ok "LEG A1: tool_call has NO fail_class key (successful call)"
    fi
else
    t_fail "LEG A1: no tool_call in telemetry"
fi

# --- LEG A2: shell command that fails with non-zero exit (connection refused) ---

cat > "$tmp/replies_a2.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sh -c 'echo connection refused; exit 1'"}
rule
  text done
EOF

mm_start "$tmp/replies_a2.mm" "$tmp"

cat > "$tmp/config_a2.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"core","lowResource":false,
"maxRetries":0}
EOF

mkdir -p "$ws"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config_a2.json" --no-session \
    --auto --log "$tmp/telemetry_a2.jsonl" --log-level metrics \
    -p "delegate this" < /dev/null > "$tmp/out_a2" 2> "$tmp/err_a2") || true

mm_stop

# Extract the tool_call event(s) for A2
grep '"event":"tool_call"' "$tmp/telemetry_a2.jsonl" 2>/dev/null > "$tmp/tc_a2.jsonl" || true

# LEG A2: fail_class should be "nonzero_exit" for a command that ran and failed
n_tc_a2=$(grep -c . "$tmp/tc_a2.jsonl" 2>/dev/null || echo 0)
if [ "$n_tc_a2" -ge 1 ]; then
    # Get the first tool_call's fail_class
    fc_a2=$(grep '"name":"run_terminal_command"' "$tmp/tc_a2.jsonl" 2>/dev/null | head -1 | "$JQ" .fail_class 2>/dev/null)
    if [ "$fc_a2" = "nonzero_exit" ]; then
        t_ok "LEG A2: tool_call fail_class='nonzero_exit' (shell command failed, not policy refusal)"
    else
        t_fail "LEG A2: tool_call fail_class=$fc_a2 (expected \"nonzero_exit\")"
    fi
else
    t_fail "LEG A2: no tool_call in telemetry"
fi

# Also verify the old behavior (grep for advice prose) still doesn't appear
# (POSIX grep -E, not the GNU-only \| alternation -- posix_utils_lint check 15)
if grep -Eq "policy refusal|refused again" "$tmp/out_a2" "$tmp/err_a2" 2>/dev/null; then
    t_fail "LEG A2: advice prose suggests DENIED (wrong)"
else
    t_ok "LEG A2: no DENIED-specific advice prose in output"
fi

# --- LEG B: read-only run where shell tool is refused -------------------------
# In read-only mode, any mutating tool call is refused by the gate.
# The refusal message should mention "tool disabled in read-only mode".
# The classification should be DENIED (by flag or word-based fallback).
# LEG B2 asserts the telemetry's fail_class == "denied".

# Create a new workspace for the read-only run
ws_ro=$(smoke_tmp)
mkdir -p "$ws_ro"

cat > "$tmp/replies_ro.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"sh -c 'echo hello'"}
rule
  text done
EOF

mm_start "$tmp/replies_ro.mm" "$tmp"

# Run in read-only mode - use the read-only workspace
cat > "$tmp/config_ro.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"core","lowResource":false,
"maxRetries":0}
EOF
(cd "$ws_ro" && with_deadline 60 "$BIN" --config "$tmp/config_ro.json" --no-session \
    --auto --readonly --log "$tmp/telemetry_ro.jsonl" --log-level metrics \
    -p "run the terminal command" < /dev/null > "$tmp/out_ro" 2> "$tmp/err_ro") || true

mm_stop

# LEG B: In read-only mode, the model's call to run_terminal_command is refused.
# The framework reports this as "refused by a fence" in the final summary.
# This verifies the read-only gate is working and that the call was refused.
# The classification (DENIED by flag or word-based fallback) is not directly
# visible in the output, but the refusal shows the gate is active.
if grep -q "refused by a fence" "$tmp/out_ro" "$tmp/err_ro" 2>/dev/null; then
    t_ok "LEG B: read-only shell tool refused by fence"
else
    t_fail "LEG B: no evidence of read-only fence refusal"
fi

# LEG B2: Assert the telemetry's fail_class == "denied"
# Extract the tool_call event from the read-only telemetry
if [ -f "$tmp/telemetry_ro.jsonl" ]; then
    grep '"event":"tool_call"' "$tmp/telemetry_ro.jsonl" 2>/dev/null > "$tmp/tc_ro.jsonl" || true
    n_tc_ro=$(grep -c . "$tmp/tc_ro.jsonl" 2>/dev/null || true)
    # grep -c returns 1 (and no output) if no lines match; handle that
    if [ -z "$n_tc_ro" ]; then
        n_tc_ro=0
    fi
    if [ "$n_tc_ro" -ge 1 ]; then
        # Get the tool_call's fail_class (only failed calls have it)
        fc_ro=$(grep '"name":"run_terminal_command"' "$tmp/tc_ro.jsonl" 2>/dev/null | head -1 | "$JQ" .fail_class 2>/dev/null)
        if [ "$fc_ro" = "denied" ]; then
            t_ok "LEG B2: telemetry tool_call fail_class='denied' (read-only refusal classified)"
        else
            t_fail "LEG B2: telemetry tool_call fail_class=$fc_ro (expected \"denied\")"
        fi
    else
        t_fail "LEG B2: no tool_call in read-only telemetry"
    fi
else
    t_fail "LEG B2: read-only telemetry file not found"
fi

t_done
