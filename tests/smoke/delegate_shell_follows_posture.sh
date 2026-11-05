#!/bin/sh
# smoke: a delegate's shell call follows the user's posture, and a delegate never
# gets a privileged command through the allowlist (M849, the scaffolding review's
# S4, the operator's decisions of 2026-10-01).
#
# THE DEFECT. A delegate -- spawn_subagent, a parallel child, ask_for_help's
# helper, a workflow stage -- runs with the AUTO posture: its ASK verdicts run
# without a prompt. The sysadmin and deploy-runner packs give their profiles the
# shell, so one approval of a spawn_subagent let a delegate run any command on a
# live system with nobody asked. Worse, the privileged allowlist was checked
# before the unattended refusal and matches by word prefix: an entry like
# `sudo systemctl` let a delegate run `sudo systemctl disable --now sshd`.
#
# THE RULE. A delegate's SHELL call (run_terminal_command, run_tests with a
# command -- M846's jc_tool_name_runs_shell) is treated as the main turn treats
# it: asked in chat, refused when the delegate cannot ask, run under --auto. Its
# other tools keep the auto posture (DEFERRED def-286). The privileged allowlist
# is honoured for the user's own turn (the main turn, a subtask command) and a
# top-level --auto run, never for a delegate.
#
#   1  headless chat: a delegate's shell call is refused, with a reason that
#      names the delegate (not "headless mode"), and the command does not run.
#   2  control: under --auto the same delegate's shell call runs.
#   3  the rule is narrow: the delegate's write_file in the workspace still runs
#      without asking in chat (the path fence holds it, M848).
#   4  run_tests with a command is a shell call too: refused from a delegate.
#   5  a delegate's allowlisted sudo is refused, audited `delegate_refused`.
#   6  control: the same allowlisted command at the top level of an --auto run
#      is honoured (`allowlist`) -- the allowlist's documented CI use.
#   7  a subtask command is the user's turn (M848), not a delegate: its
#      allowlisted command is honoured too.
#   8  the TUI: a delegate's shell call shows the ordinary Allow? prompt; declined,
#      it does not run, and /why says the user declined it.
#   9  the TUI: approved, it runs, and /why says the user approved it -- not "a
#      delegated task's auto posture runs it without asking".
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
scripts=$(smoke_tmp)
ALLOW_SPAWN='"permissions":{"allow":["spawn_subagent"]}'
SUDO_OK="sudo -n true"   # harmless either way: runs `true`, or fails asking for a password

# run_case <script> <extra-config> <prompt> <extra-cli> -> WS, CAP, OUT, AUDIT
run_case() {
    WS=$(smoke_tmp)
    mkdir -p "$WS/.jichi/commands"
    cat > "$WS/.jichi/commands/priv.md" <<'EOF'
---
description: a subtask that runs one command
subtask: true
---
Run the command.
EOF
    CAP=$(smoke_tmp)
    _home=$(smoke_tmp)
    mm_start "$1" "$CAP"
    write_config "$CAP/config.json" "$MM_PORT" "$2"
    OUT="$CAP/out"
    # shellcheck disable=SC2086 -- $4 is a list of CLI flags, intentionally split
    (cd "$WS" && with_deadline 60 env HOME="$_home" "$BIN" \
        --config "$CAP/config.json" --no-session $4 -p "$3" \
        < /dev/null > "$OUT" 2>&1)
    mm_stop
    AUDIT="$_home/.jichi.d/audit/privileged.jsonl"
}

# delegate_script <file> <child tool line> -> parent spawns, the child calls one tool
delegate_script() {
    cat > "$1" <<EOF
wire openai
rule
  count 1
  tool spawn_subagent {"task":"do it"}
rule
  count 2
  tool $2
rule
  text done
EOF
}

# --- 1-2: the delegate's shell call follows the posture -----------------------
delegate_script "$scripts/shell.mm" 'run_terminal_command {"command":"touch DMARK"}'
run_case "$scripts/shell.mm" "$ALLOW_SPAWN" "delegate it" ""
if [ ! -e "$WS/DMARK" ] &&
   grep -q "This delegate's shell command needs your approval" "$CAP/req.3" 2>/dev/null; then
    t_ok "headless chat: a delegate's shell call is refused, naming the delegate"
else
    t_fail "a delegate ran the shell with no prompt in a non-auto run, or was
 refused with the wrong reason (DMARK=$([ -e "$WS/DMARK" ] && echo created || echo absent)):
 $(tail -c 240 "$CAP/req.3" 2>/dev/null)"
fi
run_case "$scripts/shell.mm" "$ALLOW_SPAWN" "delegate it" "--auto"
if [ -e "$WS/DMARK" ]; then
    t_ok "control: under --auto the delegate's shell call runs"
else
    t_fail "control failed: --auto did not run the delegate's command: $(tail -c 200 "$OUT")"
fi

# --- 3: narrow -- the delegate's other tools keep the auto posture ------------
delegate_script "$scripts/write.mm" 'write_file {"path":"inside.txt","content":"x"}'
run_case "$scripts/write.mm" "$ALLOW_SPAWN" "delegate it" ""
if [ -e "$WS/inside.txt" ]; then
    t_ok "the rule is narrow: a delegate's in-workspace write still runs in chat"
else
    t_fail "a delegate's in-workspace write was refused -- the shell rule spread to
 other tools: $(tail -c 200 "$CAP/req.3" 2>/dev/null)"
fi

# --- 4: run_tests with a command is a shell call -------------------------------
delegate_script "$scripts/rt.mm" 'run_tests {"command":"touch RTMARK"}'
run_case "$scripts/rt.mm" "$ALLOW_SPAWN" "delegate it" ""
if [ ! -e "$WS/RTMARK" ] &&
   grep -q "This delegate's shell command needs your approval" "$CAP/req.3" 2>/dev/null; then
    t_ok "run_tests with a command is refused from a delegate in headless chat"
else
    t_fail "a delegate's run_tests command ran unasked (RTMARK=$([ -e "$WS/RTMARK" ] && echo created || echo absent))"
fi

# --- 5-7: the privileged allowlist: never for a delegate -----------------------
PRIV_CFG="$ALLOW_SPAWN"',"privilegedCommandsAllow":["'"$SUDO_OK"'"]'
delegate_script "$scripts/priv.mm" "run_terminal_command {\"command\":\"$SUDO_OK\"}"
run_case "$scripts/priv.mm" "$PRIV_CFG" "delegate it" "--auto"
if grep -q '"decision":"delegate_refused"' "$AUDIT" 2>/dev/null &&
   ! grep -q '"decision":"allowlist"' "$AUDIT" 2>/dev/null; then
    t_ok "a delegate's allowlisted sudo is refused, audited delegate_refused"
else
    t_fail "a delegate's allowlisted sudo was not refused as a delegate's:
 $(head_bytes 300 "$AUDIT" 2>/dev/null)"
fi
cat > "$scripts/top.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"$SUDO_OK"}
rule
  text done
EOF
run_case "$scripts/top.mm" "$PRIV_CFG" "run it" "--auto"
if grep -q '"decision":"allowlist"' "$AUDIT" 2>/dev/null; then
    t_ok "control: a top-level --auto run still honours the allowlist"
else
    t_fail "control failed: the top-level allowlist was not honoured: $(head_bytes 300 "$AUDIT" 2>/dev/null)"
fi
run_case "$scripts/top.mm" "$PRIV_CFG" "/priv" "--auto"
if grep -q '"decision":"allowlist"' "$AUDIT" 2>/dev/null; then
    t_ok "a subtask command is the user's turn: its allowlisted command is honoured"
else
    t_fail "a subtask command was treated as a delegate: $(head_bytes 300 "$AUDIT" 2>/dev/null)"
fi

# --- 8-9: the TUI asks; /why tells the truth either way ------------------------
TW=$(smoke_tmp)
TC=$(smoke_tmp)
cat > "$TC/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"first"}
rule
  count 2
  tool run_terminal_command {"command":"touch DONE_ONE"}
rule
  count 3
  text C1_DONE
rule
  count 4
  text T1_DONE
rule
  count 5
  tool spawn_subagent {"task":"second"}
rule
  count 6
  tool run_terminal_command {"command":"touch DONE_TWO"}
rule
  count 7
  text C2_DONE
rule
  text T2_DONE
EOF
mm_start "$TC/replies.mm" "$TC/cap"
write_config "$TC/config.json" "$MM_PORT" "$ALLOW_SPAWN"
cat > "$TC/t.pd" <<'EOF'
expect "[chat:" 15
delay 600
send "turn one\r"
expect "Allow?" 20
delay 600
send "n"
expect "T1_DONE" 30
delay 800
send "/why\r"
expect "you declined it" 10
delay 400
send "turn two\r"
expect "touch DONE_TWO" 20
delay 800
send "y"
expect "T2_DONE" 30
delay 800
send "/why\r"
expect "you approved it at the prompt" 10
delay 400
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$TW" && "$SMOKE_TOOLS/ptydrive" --deadline 150 --cols 120 --log "$TC/t.log" \
    "$TC/t.pd" -- "$BIN" --config "$TC/config.json" --no-route --no-session) \
    > "$TC/t.out" 2>&1; prc=$?
mm_stop
tr '\r' '\n' < "$TC/t.log" > "$TC/t.txt"
if [ ! -e "$TW/DONE_ONE" ] && grep -q 'touch DONE_ONE' "$TC/t.txt" &&
   grep -q 'you declined it' "$TC/t.txt"; then
    t_ok "the TUI asks before a delegate's shell call; declined, it does not run"
else
    t_fail "the TUI did not ask before a delegate's shell call (ptydrive rc=$prc,
 DONE_ONE=$([ -e "$TW/DONE_ONE" ] && echo created || echo absent)): $(tail -c 300 "$TC/t.out")"
fi
if [ -e "$TW/DONE_TWO" ] && grep -q 'you approved it at the prompt' "$TC/t.txt" &&
   ! grep -q "auto posture runs it without asking" "$TC/t.txt"; then
    t_ok "approved, the delegate's shell call runs and /why says the user approved it"
else
    t_fail "an approved delegate shell call did not run, or /why misreported it
 (DONE_TWO=$([ -e "$TW/DONE_TWO" ] && echo created || echo absent), ptydrive rc=$prc)"
fi

t_done
