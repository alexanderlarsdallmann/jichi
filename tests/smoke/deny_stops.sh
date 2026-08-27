#!/bin/sh
# smoke: repeated denial of the same call ends the run (M570).
#
# THE OPERATOR'S REPORT IS THE SPECIFICATION: "And the 0 does not abort, so I
# kept pressing it until this happened" -- followed by a transcript of SEVEN
# approval prompts for one rename: apply_patch, edit_file four times,
# run_terminal_command, and finally ask_user. Each retry read the whole prompt
# and its diff preview aloud.
#
# THE BUG WAS A `goto`. src/chat/jc_agent.c's denial branch jumped straight to
# `next_call`, past jc_toolloop_note -- so the loop detector saw FENCE denials
# (those return through jc_tool_execute's normal path) and never a PERSON's. The
# one case where somebody explicitly said no was the one case the loop-breaker
# could not see.
#
# AND THE DETECTOR ALREADY HAD THE RIGHT ANSWER, which is what makes this a bug
# rather than a missing feature. Its JC_FAIL_DENIED advice reads: "this is
# refused by policy, not failing by accident: it will not succeed by rephrasing.
# Work within what is permitted, or say in your final answer that the task needs
# something you were denied." Thresholds: 3 exact, 4 class. The third denial
# would have ended the transcript above.
#
# WHY BOTH REMEDIES. The note goes into history so the record says why, and so a
# NEXT turn's model sees it; abort_flag stops THIS turn, because telling the
# model to behave still leaves the user waiting on its goodwill -- which is
# precisely what "0 does not abort" means. Ending the turn is safe: the TUI
# clears abort_flag before reading each new line.
#
# M572 REVERSED THIS DRIVER'S CENTRAL JUDGEMENT, and the reversal is the most
# useful thing in the file. M570 asserted that three denials of THREE DIFFERENT
# calls must NOT stop the run, on my reasoning that "refusing unrelated calls and
# allowing the next is ordinary use". The operator then measured what that
# reasoning cost:
#
#   TEN prompts for one rename. Both of jc_toolloop's keys include the TOOL
#   NAME, so a model that rotates tools multiplies its budget -- edit_file,
#   apply_patch, run_terminal_command and write_file each carried their own
#   count, and only edit_file ever reached four.
#
# So the count is now TOOL-INDEPENDENT and lives in jc_agent (JC_DENY_STOP_AT 3),
# and what preserves the legitimate case is not the tool name but an APPROVAL:
# accepting anything clears the streak. That is check 6, and it is the guard that
# replaces the one M572 deleted.
#
# AND THERE IS NOW A DETERMINISTIC WAY OUT, which is better than any threshold:
# Ctrl-C at an approval prompt stops the run instead of answering the question
# (check 4). It used to deny, which is why Ctrl-C could never reach the input
# line and why I told the operator, wrongly, that pressing it twice would
# interrupt the turn. The first refusal of a turn now also prints the hint, once,
# because the prompt itself cannot afford a sixth advertised option (check 5).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# --- arm A: the SAME call, refused over and over --------------------------
# The mock repeats one identical edit_file call, which is what a model does
# when it reads a bare "denied" and tries again.
# `count` IS A REQUEST INDEX, not a repeat count -- mm_core.c reads
# `r->count != req_index`. My first version said `count 6` meaning "serve this
# six times" and it served on request SIX only, so the first five requests fell
# through to the fallback text and no approval was ever asked. Four explicit
# rules, all the SAME call, is how you repeat one.
cat > "$tmp/same.mm" <<'EOF'
wire openai
rule
  count 1
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"two"}
rule
  count 2
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"two"}
rule
  count 3
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"two"}
rule
  count 4
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"two"}
rule
  text GAVE_UP
EOF

# THE FIRST SEND WAS MISSING and check 1 caught it: the script opened with
# `expect "Allow?"`, but a turn only begins when the user says something, so
# nothing was ever asked and all four checks read an empty capture. Note that
# check 3 ("at most three prompts") passed on 0 prompts -- vacuously true, and
# the reason a denominator is not optional.
#
# The anchor is the arrow rather than "] ": these arms run --accessible, whose
# prompt (M562) is `chat >` with no bracket. ASCII ">" because _smoke.sh
# exports LC_ALL=C.
# EXPECT ONCE, THEN DELAYS -- approval_keys.sh's shape, and for a reason this
# driver rediscovered: `expect` scans the accumulated buffer, so a second
# `expect "Allow?"` matches the FIRST prompt still sitting in it and fires
# immediately. Three expects therefore sent three keys against one prompt and
# desynchronised, producing 3 prompts and only 2 denials. Delays are honest here
# because the model is a local mock: a round trip is milliseconds.
cat > "$tmp/deny.pd" <<'EOF'
expect "> " 15
send "edit a.txt\r"
expect "Allow?" 25
delay 600
send "0"
delay 2500
send "0"
delay 2500
send "0"
delay 3000
send "/exit\r"
waitexit 20
EOF

printf 'one\n' > "$ws/a.txt"
mm_start "$tmp/same.mm" "$tmp/capA"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$SMOKE_TOOLS/ptydrive" --deadline 85 --cols 100 \
    --log "$tmp/a.log" "$tmp/deny.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
nprompt_a=$(tr '\r' '\n' < "$tmp/a.log" | $G -c 'Allow?' || true)
[ -n "$nprompt_a" ] || nprompt_a=0

# --- arm B: three DIFFERENT calls, each refused ---------------------------
cat > "$tmp/diff.mm" <<'EOF'
wire openai
rule
  count 1
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"two"}
rule
  count 2
  tool edit_file {"path":"b.txt","old_string":"one","new_string":"two"}
rule
  count 3
  tool edit_file {"path":"c.txt","old_string":"one","new_string":"two"}
rule
  count 4
  tool edit_file {"path":"d.txt","old_string":"one","new_string":"two"}
rule
  text REACHED_FOURTH
EOF
for f in a b c d; do printf 'one\n' > "$ws/$f.txt"; done
mm_start "$tmp/diff.mm" "$tmp/capB"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$SMOKE_TOOLS/ptydrive" --deadline 85 --cols 100 \
    --log "$tmp/b.log" "$tmp/deny.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
nprompt_b=$(tr '\r' '\n' < "$tmp/b.log" | $G -c 'Allow?' || true)
[ -n "$nprompt_b" ] || nprompt_b=0

# --- arm C: Ctrl-C at the FIRST prompt ------------------------------------
# One keypress, and the run must end -- no threshold involved.
cat > "$tmp/ctrlc.pd" <<'EOF'
expect "> " 15
send "edit a.txt\r"
expect "Allow?" 25
delay 600
send "\x03"
delay 2500
send "/exit\r"
waitexit 20
EOF
printf 'one\n' > "$ws/a.txt"
mm_start "$tmp/same.mm" "$tmp/capC"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 90 "$SMOKE_TOOLS/ptydrive" --deadline 85 --cols 100 \
    --log "$tmp/c.log" "$tmp/ctrlc.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
nprompt_c=$(tr '\r' '\n' < "$tmp/c.log" | $G -c 'Allow?' || true)
[ -n "$nprompt_c" ] || nprompt_c=0

# --- arm D: an APPROVAL clears the streak ---------------------------------
# deny, deny, ALLOW, deny, deny. Five refusal-shaped answers but never three in
# a row, so the run must survive. The approved call is `true` through the shell:
# it needs approval (so the sequence is real) and does nothing.
cat > "$tmp/reset.mm" <<'EOF'
wire openai
rule
  count 1
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"two"}
rule
  count 2
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"three"}
rule
  count 3
  tool run_terminal_command {"command":"true"}
rule
  count 4
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"four"}
rule
  count 5
  tool edit_file {"path":"a.txt","old_string":"one","new_string":"five"}
rule
  text SURVIVED_THE_STREAK
EOF
cat > "$tmp/reset.pd" <<'EOF'
expect "> " 15
send "edit a.txt\r"
expect "Allow?" 25
delay 600
send "0"
delay 2500
send "0"
delay 2500
send "1"
delay 2500
send "0"
delay 2500
send "0"
delay 2500
send "/exit\r"
waitexit 20
EOF
printf 'one\n' > "$ws/a.txt"
mm_start "$tmp/reset.mm" "$tmp/capD"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 110 "$SMOKE_TOOLS/ptydrive" --deadline 105 --cols 100 \
    --log "$tmp/d.log" "$tmp/reset.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
nprompt_d=$(tr '\r' '\n' < "$tmp/d.log" | $G -c 'Allow?' || true)
[ -n "$nprompt_d" ] || nprompt_d=0

# ---- 1: the denominator -- both arms actually reached the fence -----------
# Checks 2-4 count prompts and look for a notice; on a capture with no prompt at
# all, "at most three prompts" is trivially true and "no fourth" is vacuous.
if [ "$nprompt_a" -ge 3 ] && [ "$nprompt_b" -ge 3 ] &&
   [ "$nprompt_c" -ge 1 ] && [ "$nprompt_d" -ge 4 ]; then
    t_ok "all four arms reached the fence (A=$nprompt_a B=$nprompt_b \
C=$nprompt_c D=$nprompt_d prompts)"
else
    t_fail "an arm never reached the fence, so its checks test nothing \
(A=$nprompt_a>=3 B=$nprompt_b>=3 C=$nprompt_c>=1 D=$nprompt_d>=4). A mock that \
stopped serving, or a tool needing no approval, produces exactly this. A tail: \
$(tr '\r' '\n' < "$tmp/a.log" | tail -2 | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2: the run says WHY it stopped --------------------------------------
if $G -q 'this run is stopping' "$tmp/a.log"; then
    t_ok "the user is told the run stopped, and why"
else
    t_fail "no stop notice after repeated denials. The operator's report was \
that 0 does not abort -- so the user must be TOLD the refusals ended the run, \
not left guessing why the prompts stopped. Notices seen: \
$(tr '\r' '\n' < "$tmp/a.log" | $G -c 'denied' || echo 0) denial(s)"
fi

# ---- 3: and it STOPS -- the fourth prompt never comes -------------------
# The property, not the notice. A build that printed the sentence and carried on
# asking would pass check 2 and fail here.
# BOTH ARMS, which is where the M572 reversal is actually pinned: three refusals
# end the turn whether the calls are IDENTICAL (A) or four DIFFERENT tools (B).
# Checking only A would leave the reversal unasserted -- M570's behaviour, where
# B ran on happily, would pass.
if [ "$nprompt_a" -le 3 ] && [ "$nprompt_b" -le 3 ]; then
    t_ok "three refusals end the turn, identical or not (A=$nprompt_a, \
B=$nprompt_b; each mock offers 4)"
else
    t_fail "the run kept asking after three refusals (A=$nprompt_a, \
B=$nprompt_b; want <= 3 each). The count is TOOL-INDEPENDENT now: a model that \
rotates edit_file / apply_patch / run_terminal_command / write_file must not get \
a fresh budget per tool, which is what cost the operator ten prompts."
fi

# ---- 4: CTRL-C STOPS THE RUN, with no threshold involved ----------------
# One keypress at the first prompt. This is the deterministic escape the
# operator did not have: Ctrl-C used to DENY, so the model asked again and the
# next Ctrl-C answered that -- it could never reach the input line.
if [ "$nprompt_c" -le 1 ] && $G -q 'this run is stopping\|denied' "$tmp/c.log"
then
    t_ok "Ctrl-C at the prompt ends the run at once ($nprompt_c prompt)"
else
    t_fail "Ctrl-C did not stop the run: $nprompt_c prompts (want 1). It must \
deny AND abort -- answering one question with it is what left the operator \
out-waiting the model for ten prompts."
fi

# ---- 5: the FIRST refusal teaches THE RULE and the escape, once --------
# Discoverability without cost: the approval prompt is the most-repeated string
# in a session (M562) and cannot carry a sixth advertised option, so the hint
# goes after the first refusal instead -- and only there.
#
# M573 made it state the RULE as well, because the operator's question showed
# nobody knew it: "the user, or agent has to know about the three refusals rule".
# BOTH HALVES are asserted. Grepping only for "Control C" would pass for a hint
# that tells you how to escape while leaving the automatic stop a surprise --
# which is the state that prompted the question.
nhint=$(tr '\r' '\n' < "$tmp/a.log" | $G -c 'refusals in a row' || true)
[ -n "$nhint" ] || nhint=0
nesc=$(tr '\r' '\n' < "$tmp/a.log" | $G -c 'Control C' || true)
[ -n "$nesc" ] || nesc=0
if [ "$nhint" -eq 1 ] && [ "$nesc" -ge 1 ]; then
    t_ok "the hint states the rule and the escape, exactly once"
else
    t_fail "the hint appeared $nhint time(s) stating the rule and $nesc naming \
Control C; want the rule exactly once and the escape at least once. Zero rule \
means the automatic stop stays a surprise; more than one means the hint became \
the nag it was designed to avoid."
fi

# ---- 6: THE MODEL is told its budget, in the tool result ---------------
# The half a person cannot see. Before M573 the model received the bare sentence
# "Tool call denied by the user." and could not know a third refusal ends the
# turn -- the operator's transcript shows it reaching ask_user on its own only
# after ten prompts. Asserted on the CAPTURED REQUEST, which is what the model
# actually received, rather than on anything printed to the screen.
if $G -rq 'the turn ends' "$tmp/capA" 2>/dev/null &&
   $G -rq 'refused .* in a row this turn' "$tmp/capA" 2>/dev/null
then
    t_ok "the denial the model receives names the limit and the count"
else
    t_fail "the model was not told its refusal budget. It gets the tool result \
as history, so this is asserted on the captured request bodies -- if it is \
absent, the model can only discover the limit by hitting it. Captures with a \
denial: $($G -rlc 'denied by the user' "$tmp/capA" 2>/dev/null | head -1)"
fi

# ---- 7: AN APPROVAL CLEARS THE STREAK ---------------------------------
# THE GUARD THAT REPLACES THE ONE M572 DELETED. deny, deny, ALLOW, deny, deny --
# five refusal-shaped answers, never three in a row, so the run must survive.
# Without this, JC_DENY_STOP_AT would make the fence unusable for anyone who
# refuses a couple of proposals and accepts the next.
if ! $G -q 'this run is stopping' "$tmp/d.log" &&
   [ "$nprompt_d" -ge 5 ]; then
    t_ok "an approval resets the refusal streak ($nprompt_d prompts, no stop)"
else
    t_fail "the run stopped despite an approval breaking the streak \
($nprompt_d prompts; stop notice: $($G -c 'this run is stopping' "$tmp/d.log")). \
Refusing two proposals, accepting one, then refusing two more is ordinary use -- \
consec_deny must reset when a call is allowed."
fi

t_done
