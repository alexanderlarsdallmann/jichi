#!/bin/sh
# smoke: /why says which rule decided the last tool call (M762).
#
# THE GAP, from the operator's twelve questions (plan section 9, for users): "why
# did it do that?" -- or "why did it not?" -- is the most-asked question in an
# agent, and jichi could answer it only from the model's account, which is the
# one account that cannot be trusted to know. The gates know. Each one now writes
# the decision it made (jc_app_why_note), and /why prints the last one: the rule
# that refused the call or the one that let it run, and what the model was told.
#
# One chat-mode session, five turns, each ending at a different gate: a read that
# chat mode runs unasked, a write declined at the prompt, a write approved there,
# a call a constraint refuses, and a read the path fence refuses inside the tool.
# /why after each. Every wait is on text unique to its step: ptydrive's expect
# searches the whole transcript, so a repeated phrase would match at once.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/.jichi"
printf '# jichi constraints -- one per line.\ndeny-tool run_tests\n' > "$ws/.jichi/constraints.md"
printf 'READ_ME_CONTENT\n' > "$ws/a.txt"

# A linear script: `count N` answers only the Nth request, two per turn.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"a.txt"}
rule
  count 2
  text T1_DONE
rule
  count 3
  tool write_file {"path":"c.txt","content":"declined"}
rule
  count 4
  text T2_DONE
rule
  count 5
  tool write_file {"path":"b.txt","content":"approved"}
rule
  count 6
  text T3_DONE
rule
  count 7
  tool run_tests {"command":"make"}
rule
  count 8
  text T4_DONE
rule
  count 9
  tool read_file {"path":"/etc/hostname"}
rule
  text T5_DONE
EOF
mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT" '"pathFence":true'

cat > "$tmp/w.pd" <<'EOF'
expect "[chat:" 15
delay 400
send "/why\r"
expect "no tool call yet" 10
delay 400
send "turn one\r"
expect "T1_DONE" 20
delay 800
send "/why\r"
expect "only reads without asking" 10
delay 400
send "turn two\r"
expect "Allow?" 20
delay 400
send "n"
expect "T2_DONE" 20
delay 800
send "/why\r"
expect "you declined it" 10
delay 400
send "turn three\r"
expect "Allow?" 20
delay 400
send "y"
expect "T3_DONE" 20
delay 800
send "/why\r"
expect "you approved it at the prompt" 10
delay 400
send "turn four\r"
expect "T4_DONE" 20
delay 800
send "/why\r"
expect "a constraint you wrote" 10
delay 400
send "turn five\r"
expect "T5_DONE" 20
delay 800
send "/why\r"
expect "the tool refused it itself" 10
delay 400
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 120 --cols 120 --log "$tmp/w.log" \
    "$tmp/w.pd" -- "$BIN" --config "$tmp/config.json" --no-route --no-session) \
    > "$tmp/w.out" 2>&1; rc=$?
mm_stop
tr '\r' '\n' < "$tmp/w.log" > "$tmp/w.txt"

# 1: before any call, /why says there is none -- and the session completed
if [ "$rc" -eq 0 ] && grep -q '^no tool call yet in this session' "$tmp/w.txt"; then
    t_ok "before any tool call, /why says there has been none"
else
    t_fail "session (ptydrive rc=$rc): $(tail -c 300 "$tmp/w.out")"
fi

# 2: a read in chat mode ran because it only reads
if grep -q '^the last tool call: read_file  a.txt' "$tmp/w.txt" \
   && grep -q '^  it ran: it only reads, and chat mode runs a tool that only reads without asking' "$tmp/w.txt"; then
    t_ok "a read is explained by chat mode's rule for tools that only read"
else
    t_fail "turn one: $(grep -A2 'the last tool call: read_file  a.txt' "$tmp/w.txt" | head -3 | tr '\n' ';')"
fi

# 3: the declined write names the person who declined it, and what the model was told
if grep -q '^the last tool call: write_file  c.txt' "$tmp/w.txt" \
   && grep -q '^  refused: you declined it at the prompt' "$tmp/w.txt" \
   && grep -q '^  the model was told: Tool call denied by the user' "$tmp/w.txt"; then
    t_ok "a declined write says you declined it, and what the model was told"
else
    t_fail "turn two: $(grep -A2 'the last tool call: write_file  c.txt' "$tmp/w.txt" | head -3 | tr '\n' ';')"
fi

# 4: the approved write says so
if grep -q '^the last tool call: write_file  b.txt' "$tmp/w.txt" \
   && grep -q '^  it ran: you approved it at the prompt' "$tmp/w.txt"; then
    t_ok "an approved write says you approved it at the prompt"
else
    t_fail "turn three: $(grep -A2 'the last tool call: write_file  b.txt' "$tmp/w.txt" | head -3 | tr '\n' ';')"
fi

# 5: the constraint's refusal names the constraint
if grep -q '^the last tool call: run_tests' "$tmp/w.txt" \
   && grep -q '^  refused: a constraint you wrote refuses it' "$tmp/w.txt"; then
    t_ok "a call a constraint refused says a constraint you wrote refused it"
else
    t_fail "turn four: $(grep -A2 'the last tool call: run_tests' "$tmp/w.txt" | head -3 | tr '\n' ';')"
fi

# 6: a refusal from inside the tool (the path fence) is named as the tool's own
if grep -q '^the last tool call: read_file  /etc/hostname' "$tmp/w.txt" \
   && grep -q '^  refused: the tool refused it itself' "$tmp/w.txt" \
   && grep -q '^  the model was told: error: refused by safety fence' "$tmp/w.txt"; then
    t_ok "the path fence's refusal inside the tool is named, with its message"
else
    t_fail "turn five: $(grep -A2 'the last tool call: read_file  /etc/hostname' "$tmp/w.txt" | head -3 | tr '\n' ';')"
fi

t_done
