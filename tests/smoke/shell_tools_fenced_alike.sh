#!/bin/sh
# smoke: run_tests carries a shell command, so every shell gate fences it the
# same as run_terminal_command (M846, PAR-2).
#
# THE DEFECT. run_tests runs its `command` argument through the shell, which
# M501 already counts as a shell for the shell_ran signal. But the --strict-scope
# refusal matched the tool NAME `run_terminal_command` only, so
# `run_tests {"command":"...>outside"}` wrote outside an edit scope and the
# envelope still reported the run verified. The privileged and kinetic gates
# matched the same single name. M846 routes all of them through one predicate,
# jc_tool_name_runs_shell, so the two tools are fenced alike.
#
# HOW THIS SEES IT. For each gate, the mock model issues the SAME command once
# as run_terminal_command (the control, already fenced) and once as run_tests,
# and the driver asserts the two outcomes match:
#   - strict-scope: a write outside the scope is refused for both (no file);
#   - privileged:   a sudo-shaped command is refused + audited for both;
#   - kinetic:      a command matching a kinetic prefix is gated for both.
# A positive control (a plain run_tests command, no sudo/kinetic/scope) still
# runs, so the refusals are not a binary that refuses everything.
#
# UNIVERSE (last check): every gate in src/chat/jc_agent.c that selects a shell
# tool goes through jc_tool_name_runs_shell -- no gate may match
# `run_terminal_command` or `run_tests` by a raw strcmp, which is how the five
# sites drifted apart.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
SUDO="sudo apt-get update"   # refused + audited; never executes

# run_one <tool> <argsjson> <extra-config> <prompt> <extra-cli> -> CASE_WS, AUDIT, OUT
run_one() {
    CASE_WS=$(smoke_tmp)
    _d=$(smoke_tmp)
    _home=$(smoke_tmp)
    cat > "$_d/replies.mm" <<EOF
wire openai
rule
  count 1
  tool $1 $2
rule
  text done
EOF
    mm_start "$_d/replies.mm" "$_d/cap"
    write_config "$_d/config.json" "$MM_PORT" "$3"
    OUT="$_d/out"
    # shellcheck disable=SC2086 -- $5 is a list of CLI flags, intentionally split
    (cd "$CASE_WS" && with_deadline 60 env HOME="$_home" "$BIN" \
        --config "$_d/config.json" --no-session --auto $5 -p "$4" \
        < /dev/null > "$OUT" 2>&1)
    mm_stop
    AUDIT="$_home/.jichi.d/audit/privileged.jsonl"
}

# --- 1-2: --strict-scope refuses a write through either tool --------------------
run_one run_terminal_command '{"command":"echo x > out.txt"}' \
    '' "write outside" '--strict-scope --edit-scope src/**'
RTC_FILE=$([ -f "$CASE_WS/out.txt" ] && echo yes || echo no)
run_one run_tests '{"command":"echo x > out.txt"}' \
    '' "write outside" '--strict-scope --edit-scope src/**'
RT_FILE=$([ -f "$CASE_WS/out.txt" ] && echo yes || echo no)
if [ "$RTC_FILE" = no ]; then
    t_ok "control: --strict-scope refuses run_terminal_command's out-of-scope write"
else
    t_fail "control failed: --strict-scope let run_terminal_command write out.txt"
fi
if [ "$RT_FILE" = no ]; then
    t_ok "--strict-scope refuses run_tests's out-of-scope write too (PAR-2)"
else
    t_fail "run_tests {command: echo > out.txt} WROTE outside the edit scope under
 --strict-scope: the scope gate matched run_terminal_command only (PAR-2)"
fi

# --- 3-4: the privileged gate refuses a sudo command through either tool --------
run_one run_terminal_command "{\"command\":\"$SUDO\"}" \
    '"privilegedCommands":"deny"' "update" ''
RTC_AUD=$(grep -c '"decision":"deny"' "$AUDIT" 2>/dev/null || echo 0)
run_one run_tests "{\"command\":\"$SUDO\"}" \
    '"privilegedCommands":"deny"' "update" ''
RT_AUD=$(grep -c '"decision":"deny"' "$AUDIT" 2>/dev/null || echo 0)
if [ "$RTC_AUD" -ge 1 ]; then
    t_ok "control: the privileged gate denies + audits a sudo run_terminal_command"
else
    t_fail "control failed: no deny audit for a sudo run_terminal_command"
fi
if [ "$RT_AUD" -ge 1 ]; then
    t_ok "the privileged gate denies + audits a sudo run_tests too"
else
    t_fail "a sudo command via run_tests reached the shell ungated: no privileged
 deny audit (the gate matched run_terminal_command only)"
fi

# --- 5-6: the kinetic gate fences a matching command through either tool --------
run_one run_terminal_command '{"command":"motor-run --go"}' \
    '"kineticCommands":"deny","kineticCommandsAllow":["motor-run"]' "go" ''
RTC_KIN=$(grep -qiE 'kinetic|refus|denied|not allowed' "$OUT" && echo yes || echo no)
run_one run_tests '{"command":"motor-run --go"}' \
    '"kineticCommands":"deny","kineticCommandsAllow":["motor-run"]' "go" ''
RT_KIN=$(grep -qiE 'kinetic|refus|denied|not allowed' "$OUT" && echo yes || echo no)
if [ "$RTC_KIN" = yes ]; then
    t_ok "control: the kinetic gate recognises a matching run_terminal_command"
else
    t_fail "control failed: the kinetic gate did not fire for run_terminal_command,
 so the pair check below proves nothing: $(tail -c 200 "$OUT")"
fi
if [ "$RT_KIN" = yes ]; then
    t_ok "the kinetic gate treats run_tests like run_terminal_command"
else
    t_fail "a kinetic-prefixed command via run_tests was not gated, while the same
 command via run_terminal_command was"
fi

# --- 7: the positive control -- a plain run_tests command still runs ------------
run_one run_tests '{"command":"echo RAN > ran.txt"}' \
    '"editScope":["*.txt"]' "run the tests" ''
if [ -f "$CASE_WS/ran.txt" ]; then
    t_ok "control: a plain in-scope run_tests command still runs"
else
    t_fail "a harmless in-scope run_tests command did NOT run, so the refusals
 above may be a binary that refuses everything: $(tail -c 200 "$OUT")"
fi

# --- 8: the source universe -- every shell gate uses the predicate --------------
raw=$(grep -nE 'strcmp\(gate_name, "run_(terminal_command|tests)"\)' \
      "$SMOKE_ROOT/src/chat/jc_agent.c" 2>/dev/null)
if [ -z "$raw" ]; then
    t_ok "no shell gate in jc_agent.c matches a shell tool by raw name"
else
    t_fail "a gate matches a shell tool by raw strcmp, not jc_tool_name_runs_shell
 -- the drift that caused PAR-2:
$raw"
fi

t_done
