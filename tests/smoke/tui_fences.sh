#!/bin/sh
# smoke: /fences shows what bounds the next turn, on one screen (M757).
#
# THE DEFECT, from the operator's twelve questions (plan section 9): the posture
# was spread over five places -- /status for the mode and the path fence, the
# config for the edit scope, /constraints for the rules, the envelope for the
# budgets -- and NOWHERE for the shell, which no fence covers. The overnight drive
# (M744) lost its data to one shell command. /fences puts it in one place and
# says, in words, whether the shell is fenced.
#
# A session with a path fence, an edit scope, one authored constraint and
# privileged commands refused prints /fences in chat mode, then in plan mode. The
# checks read each line against the config that set it.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/.jichi"
printf '# jichi constraints -- one per line.\ndeny-tool run_tests\n' > "$ws/.jichi/constraints.md"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text UNUSED
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" \
    '"pathFence":true,"editScope":["docs/**"],"privilegedCommands":"deny"'

# The prompt is awaited as "[chat:", not "] ": the constraint warning printed
# before it, "[jichi warn] [constraint] 1 active", contains "] " too, and a
# command sent before the prompt is discarded by it (M503).
cat > "$tmp/f.pd" <<'EOF'
expect "[chat:" 15
delay 300
send "/fences\r"
expect "without your API keys" 10
delay 500
send "/plan\r"
expect "[plan:" 10
delay 300
send "/fences\r"
expect "not offered while read-only" 10
delay 300
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 --log "$tmp/f.log" \
    "$tmp/f.pd" -- "$BIN" --config "$tmp/config.json" --no-route --no-session) \
    > "$tmp/f.out" 2>&1; rc=$?
mm_stop
tr '\r' '\n' < "$tmp/f.log" > "$tmp/f.txt"
# the first /fences screen: from its header to the prompt after it
awk '/what bounds the next turn:/ {n++} n == 1' "$tmp/f.txt" > "$tmp/chat.txt"

# 1: one screen with every line on it
missing=""
for label in "mode:" "read-only:" "path fence:" "edit scope:" "constraints:" \
             "privileged:" "physical:" "approvals:" "budgets:" "the shell:"; do
    grep -q "^  $label" "$tmp/chat.txt" || missing="$missing [$label]"
done
if [ "$rc" -eq 0 ] && [ -z "$missing" ]; then
    t_ok "/fences prints every line of the posture on one screen"
else
    t_fail "/fences (rc=$rc) is missing:$missing"
fi

# 2: the edit scope is the configured one, and says it fences the file tools only
if grep -q '^  edit scope: *docs/\*\* (file tools only)' "$tmp/chat.txt"; then
    t_ok "the edit scope is the configured glob, marked as the file tools' fence"
else
    t_fail "edit scope line: $(grep 'edit scope:' "$tmp/chat.txt")"
fi

# 3: the authored constraint is counted as one that refuses
if grep -q '^  constraints: *1 yours, which refuse' "$tmp/chat.txt"; then
    t_ok "the authored constraint is counted, as one that refuses"
else
    t_fail "constraints line: $(grep 'constraints:' "$tmp/chat.txt")"
fi

# 4: the privileged policy reads as configured
if grep -q '^  privileged: *sudo and doas are refused' "$tmp/chat.txt"; then
    t_ok "privileged commands read as refused, as configured"
else
    t_fail "privileged line: $(grep 'privileged:' "$tmp/chat.txt")"
fi

# 5: in chat mode the shell is said to be NOT fenced
if grep -q '^  the shell: *NOT fenced' "$tmp/chat.txt"; then
    t_ok "in chat mode /fences says the shell is not fenced"
else
    t_fail "shell line: $(grep 'the shell:' "$tmp/chat.txt")"
fi

# 6: in plan mode the same screen says plan, and that the shell is not offered
awk '/what bounds the next turn:/ {n++} n == 2' "$tmp/f.txt" > "$tmp/plan.txt"
if grep -q '^  mode: *plan' "$tmp/plan.txt" \
   && grep -q '^  the shell: *not offered while read-only' "$tmp/plan.txt"; then
    t_ok "in plan mode /fences says so, and that the shell is not offered"
else
    t_fail "plan-mode screen: $(grep -E 'mode:|the shell:' "$tmp/plan.txt" | tr '\n' ';')"
fi

t_done
