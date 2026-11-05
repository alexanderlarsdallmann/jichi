#!/bin/sh
# smoke: a subtask command runs under the user's posture, its profile's tools
# fence it, and an auto-posture delegate is path-fenced (M848, the scaffolding
# review's S2, decisions 1c + 1e + 2c).
#
# THE DEFECT. A `subtask: true` command ran its agent through the delegate
# runner, which set the AUTO posture ("run its sandboxed tools without
# prompting") and passed NO allow list -- and the path fence is on only in auto
# mode. So /learn (the mentor, offered every tool) ran the shell with no prompt in
# a chat session, and headless without --auto, where a plain prompt is refused.
# Reproduced 2026-09-29 with the mock and confirmed in the TUI on 2026-09-30.
#
# WHAT EACH CHECK HOLDS (each perturbed red; see ROADMAP M848):
#   1  1c   headless, no --auto: a subtask's shell call is refused, as a plain
#           prompt's is -- the marker is not created, and the refusal is what the
#           model reads back (the captured follow-up request).
#   2  1c   control: the same run under --auto executes it -- auto mode's
#           verdict is ALLOW before posture is consulted, so learn-on-stop and
#           every --auto subtask behave as before.
#   3  2c   a subtask command whose `agent:` profile lists only read_file is
#           OFFERED only that: run_terminal_command is not in its tools array.
#   4  2c   the gate backstop: when the model calls the unoffered shell anyway
#           (under --auto, so posture cannot be what refuses it), the profile's
#           fence refuses it -- and the refusal names the profile's list, which
#           the decision record requires.
#   5  rule a MAIN-TURN command with the same profile is still offered the shell:
#           in the main turn a profile sets persona/style/read-only only, so the
#           prompt cache's tool prefix stays byte-stable (decision 2).
#   6  1e   a delegate (spawn_subagent, allowed by permissions in a CHAT session)
#           runs with the auto posture -- and now with the path fence on: its
#           write outside the workspace is refused.
#   7  1e   control: the same delegate's write INSIDE the workspace lands, so
#           check 6 is the fence and not a child that cannot write at all.
#   8  M848 a profile and a restrict-tools skill that share no tool: before
#           M848 the empty intersection read as "no fence" and the child ran with
#           every tool; spawn_subagent now refuses the pair and names it. This
#           check holds the REFUSAL (it keys on the helper's return value); the
#           helper's own fail-closed sentinel, which guards `attempt`'s turn and
#           any future caller, is held by tests/test_tool.c -- perturbing the
#           sentinel alone leaves this check green, by design.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home

# A workspace with the fixtures every case uses: a narrow profile, a writable
# subtask command, a narrowed subtask command, a narrowed MAIN-turn command, and a
# skill fenced to run_tests.
make_ws() {
    _w=$(smoke_tmp)
    mkdir -p "$_w/.jichi/agents" "$_w/.jichi/commands" \
             "$_w/.jichi/skills/testsonly"
    cat > "$_w/.jichi/agents/narrow.md" <<'EOF'
---
description: may only read
tools:
  - read_file
---
You read files and report what you find.
EOF
    cat > "$_w/.jichi/commands/sub.md" <<'EOF'
---
description: a writable subtask
subtask: true
---
Do the subtask.
EOF
    cat > "$_w/.jichi/commands/peek.md" <<'EOF'
---
description: a narrowed subtask
agent: narrow
subtask: true
---
Peek at the files.
EOF
    cat > "$_w/.jichi/commands/peekmain.md" <<'EOF'
---
description: a narrowed main-turn command
agent: narrow
---
Peek at the files in the main turn.
EOF
    cat > "$_w/.jichi/skills/testsonly/SKILL.md" <<'EOF'
---
name: testsonly
description: runs the tests and nothing else
allowed-tools:
  - run_tests
restrict-tools: true
---
Run the tests.
EOF
    echo "$_w"
}

# run_case <script> <extra-config> <prompt> <extra-cli> -> WS, CAP, OUT
run_case() {
    WS=$(make_ws)
    CAP=$(smoke_tmp)
    mm_start "$1" "$CAP"
    write_config "$CAP/config.json" "$MM_PORT" "$2"
    OUT="$CAP/out"
    # shellcheck disable=SC2086 -- $4 is a list of CLI flags, intentionally split
    (cd "$WS" && with_deadline 60 "$BIN" --config "$CAP/config.json" \
        --no-session $4 -p "$3" < /dev/null > "$OUT" 2>&1)
    mm_stop
}

scripts=$(smoke_tmp)

# --- 1-2: 1c -- the subtask asks like the main turn ---------------------------
cat > "$scripts/shell.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"touch MARK"}
rule
  text done
EOF
run_case "$scripts/shell.mm" "" "/sub" ""
if [ ! -e "$WS/MARK" ] && grep -q 'requires approval' "$CAP/req.2" 2>/dev/null; then
    t_ok "headless without --auto, a subtask's shell call is refused like a plain prompt's"
else
    t_fail "a subtask command ran the shell with no prompt in a non-auto run
 (MARK=$([ -e "$WS/MARK" ] && echo created || echo absent)): $(tail -c 200 "$CAP/req.2" 2>/dev/null)"
fi
run_case "$scripts/shell.mm" "" "/sub" "--auto"
if [ -e "$WS/MARK" ]; then
    t_ok "control: under --auto the subtask's shell call runs (auto mode unchanged)"
else
    t_fail "control failed: --auto did not run the subtask's command: $(tail -c 200 "$OUT")"
fi

# --- 3-4: 2c -- the profile's tools fence the subtask -------------------------
cat > "$scripts/quiet.mm" <<'EOF'
wire openai
rule
  text done
EOF
run_case "$scripts/quiet.mm" "" "/peek" "--auto"
if grep -q '"name":"read_file"' "$CAP/req.1" 2>/dev/null &&
   ! grep -q '"name":"run_terminal_command"' "$CAP/req.1" 2>/dev/null; then
    t_ok "a subtask under a read_file-only profile is offered read_file, not the shell"
else
    t_fail "the subtask's tools ignore its profile's tools: list
 (read_file offered: $(grep -c '"name":"read_file"' "$CAP/req.1" 2>/dev/null),
  shell offered: $(grep -c '"name":"run_terminal_command"' "$CAP/req.1" 2>/dev/null))"
fi
run_case "$scripts/shell.mm" "" "/peek" "--auto"
# The decision record requires the refusal to NAME the profile's list, so a
# user whose own subtask loses a tool can see why.
if [ ! -e "$WS/MARK" ] &&
   grep -q 'not available to this agent. Available tools: read_file' "$CAP/req.2" 2>/dev/null; then
    t_ok "the profile fence refuses an unoffered shell call, naming the profile's list"
else
    t_fail "a shell call the profile does not allow ran, or was refused for
 another reason (MARK=$([ -e "$WS/MARK" ] && echo created || echo absent)):
 $(tail -c 200 "$CAP/req.2" 2>/dev/null)"
fi

# --- 5: the main turn is NOT fenced by a profile (decision 2) -----------------
run_case "$scripts/quiet.mm" "" "/peekmain" "--auto"
if grep -q '"name":"run_terminal_command"' "$CAP/req.1" 2>/dev/null; then
    t_ok "a main-turn command's profile does not change the advertised tools"
else
    t_fail "a main-turn command's profile narrowed the tools array -- the prompt
 cache's prefix would break on every such command (decision 2 keeps it stable)"
fi

# --- 6-7: 1e -- an auto-posture delegate is path-fenced in chat --------------
OUTSIDE=$(smoke_tmp)
cat > "$scripts/deleg_out.mm" <<EOF
wire openai
rule
  count 1
  tool spawn_subagent {"task":"write the file"}
rule
  count 2
  tool write_file {"path":"$OUTSIDE/escaped.txt","content":"x"}
rule
  text done
EOF
run_case "$scripts/deleg_out.mm" '"permissions":{"allow":["spawn_subagent"]}' \
    "delegate it" ""
if [ ! -e "$OUTSIDE/escaped.txt" ] &&
   grep -q 'outside workspace' "$CAP/req.3" 2>/dev/null; then
    t_ok "in chat, an auto-posture delegate's write outside the workspace is refused"
else
    t_fail "a delegate wrote outside the workspace from a chat session
 (escaped.txt=$([ -e "$OUTSIDE/escaped.txt" ] && echo written || echo absent)):
 $(tail -c 200 "$CAP/req.3" 2>/dev/null)"
fi
cat > "$scripts/deleg_in.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"write the file"}
rule
  count 2
  tool write_file {"path":"inside.txt","content":"x"}
rule
  text done
EOF
run_case "$scripts/deleg_in.mm" '"permissions":{"allow":["spawn_subagent"]}' \
    "delegate it" ""
if [ -e "$WS/inside.txt" ]; then
    t_ok "control: the same delegate's write inside the workspace lands"
else
    t_fail "control failed: the delegate could not write inside the workspace:
 $(tail -c 200 "$CAP/req.3" 2>/dev/null)"
fi

# --- 8: a profile and a skill that share no tool are refused, not unfenced ----
cat > "$scripts/disjoint.mm" <<'EOF'
wire openai
rule
  count 1
  tool spawn_subagent {"task":"do it","agent":"narrow","skill":"testsonly"}
rule
  text done
EOF
run_case "$scripts/disjoint.mm" "" "delegate it" "--auto"
if grep -q 'share no tool' "$CAP/req.2" 2>/dev/null; then
    t_ok "a profile and a restrict-tools skill with no common tool are refused"
else
    t_fail "a disjoint profile + skill was not refused -- an empty fence
 intersection reads as no fence: $(tail -c 200 "$CAP/req.2" 2>/dev/null)"
fi

t_done
