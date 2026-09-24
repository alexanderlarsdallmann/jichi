#!/bin/sh
# smoke: a shell command's stderr goes to the MODEL -- from every part of a list
# or a pipeline -- and NOT onto jichi's own stderr (M721).
#
# THE DEFECT. The popen path of `run_terminal_command` (the default: no per-call
# timeout, no memory budget) ran `sh -c "<command> 2>&1"`, and a trailing `2>&1`
# binds to the LAST simple command only. So in `A; B` or `A | B` the stderr of A
# went to the stderr the child inherited -- jichi's own, which headless wrappers
# read for the reach footer and the [envelope] verdict and which in the TUI is the
# screen -- and the model never saw it. `make test | tail -20` handed the model the
# tail of stdout and none of the compiler's errors. `append_shell`, the `!` shell
# interpolation of custom command templates, built the same string.
#
# HOW IT WAS MISREAD FIRST, kept because the misreading was plausible. The corpus
# pilot (docs/analysis/2026-09-23-the-corpus-pilot.md §6, Appendix A) saw the
# leak, and its five-check reproduction "refuted" exactly this mechanism. Two of
# its checks were vacuous: they grepped the NEXT request for the probe PATH, and
# the path is in that request anyway -- as the command itself, echoed back in the
# assistant's tool call -- so "the model saw it" passed while the model saw only
# the leaked line's absence. And its "plain -p" comparison never executed a
# command at all: headless without --auto refuses run_terminal_command ("Tool
# requires approval, unavailable in headless mode"). Hence the markers below:
# each is printed by arithmetic, `E1_$((40+2))`, so `E1_42` exists ONLY in the
# command's output and never in its text.
#
# The watched path (a timeout or a memory budget) was never affected: its child
# dup2()s the pipe onto fd 2, so the trailing `2>&1` there was redundant. Check 4
# holds that path as the control.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo OUT_$((40+2)); echo E1_$((40+2)) >&2 | cat"}
rule
  count 2
  tool run_terminal_command {"command":"echo E2_$((40+2)) >&2; echo LAST_$((40+2))"}
rule
  count 3
  tool run_terminal_command {"command":"echo E3_$((40+2)) >&2; echo WATCHED_$((40+2))","timeout":30}
rule
  text STDERR_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

# --auto so the commands EXECUTE: headless without it refuses the tool.
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto -p "run the commands" < /dev/null) > "$tmp/out" 2> "$tmp/err"; rc=$?
mm_stop

# in REQ MARKER -- the marker appears in that request (the tool result it carries)
in_req() { grep -q "$2" "$tmp/cap/req.$1" 2>/dev/null; }

# --- 1: the denominator -- the commands really ran --------------------------------
# OUT_42 is output, not text: a refused tool (the approval gate) produces none.
if [ "$rc" -eq 0 ] && in_req 2 OUT_42 && in_req 3 LAST_42 && in_req 4 WATCHED_42 &&
   grep -q STDERR_DONE "$tmp/out"; then
    t_ok "the three commands executed and the run finished (rc=0)"
else
    t_fail "the fixture did not execute the commands (rc=$rc): \
req.2 $(grep -c OUT_42 "$tmp/cap/req.2" 2>/dev/null) OUT_42"
fi

# --- 2: `A; B | C` -- a pipeline member's stderr reaches the model ------------------
if in_req 2 E1_42; then
    t_ok "\`A; B | C\`: B's stderr is in the tool result the model reads"
else
    t_fail "B's stderr never reached the model (the trailing 2>&1 bound to C only)"
fi

# --- 3: `A; B` -- the first command's stderr reaches the model ----------------------
if in_req 3 E2_42; then
    t_ok "\`A; B\`: A's stderr is in the tool result the model reads"
else
    t_fail "A's stderr never reached the model (the trailing 2>&1 bound to B only)"
fi

# --- 4: the control -- the watched path always captured -----------------------------
if in_req 4 E3_42; then
    t_ok "the watched path (timeout set) captures stderr, as it always did"
else
    t_fail "the watched path lost stderr -- the fix broke the path that worked"
fi

# --- 5: and none of it lands on jichi's own stderr ----------------------------------
if grep -q -e E1_42 -e E2_42 -e E3_42 "$tmp/err" 2>/dev/null; then
    t_fail "command stderr leaked onto jichi's stderr: $(grep -m1 -e E1_42 -e E2_42 -e E3_42 "$tmp/err" | head_bytes 160)"
else
    t_ok "no command stderr on jichi's own stderr"
fi

# --- 6: a custom command's `!` interpolation captures every part too ----------------
# `append_shell` (src/command/jc_command.c) built the same "<cmd> 2>&1". A command
# template is expanded by `jichi commands expand`-style rendering only inside a
# turn, so the probe is a project command run headless: its expansion is the
# prompt the mock receives, which is the ground truth for what it produced.
mkdir -p "$ws/.jichi/commands"
cat > "$ws/.jichi/commands/probe.md" <<'EOF'
---
description: stderr probe
---
Output: !`echo C1_$((40+2)) >&2; echo C2_$((40+2))`
EOF
cat > "$tmp/replies2.mm" <<'EOF'
wire openai
rule
  text CMD_DONE
EOF
mm_start "$tmp/replies2.mm" "$tmp/cap2" 2
write_config "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    -q --no-session --auto -p "/probe" < /dev/null) > "$tmp/out2" 2> "$tmp/err2"
mm_stop
if grep -q C2_42 "$tmp/cap2/req.1" 2>/dev/null && grep -q C1_42 "$tmp/cap2/req.1" 2>/dev/null &&
   ! grep -q C1_42 "$tmp/err2" 2>/dev/null; then
    t_ok "a command template's \`!\` interpolation captures the first command's stderr"
elif ! grep -q C2_42 "$tmp/cap2/req.1" 2>/dev/null; then
    t_fail "the template was not expanded at all (no C2_42 in the prompt) -- check 6 proves nothing: $(head_bytes 200 < "$tmp/err2" | tr '\n' ' ')"
else
    t_fail "the template's first command's stderr went to jichi's stderr, not the prompt"
fi

t_done
