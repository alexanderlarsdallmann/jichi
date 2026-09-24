#!/bin/sh
# smoke: an inferred constraint advises and never refuses; its negation ends at its
# sentence (plan D2, option (c), M734).
#
# THE MEASUREMENT (M730). Over 289 stored prompts the scanner inferred nine rules and
# six were wrong: each forbade the gate its own task named, and every one joined a
# negation to a word in the NEXT sentence ("Do not touch build.zig. Verify by
# running" -> "do not run build commands"). A refusal is what turned such a misparse
# into a lost run -- `1d31473d` refused its own builds for 21 minutes and 2,215,762
# tokens. The operator chose both remedies: the scope ends at the sentence, and an
# inferred rule advises. A rule the operator WROTE refuses as before.
#
# THE CHECKS: offline, M730's misparse infers nothing and the prompt it found right
# still infers its rules; live under --auto, a call against an inferred rule RUNS,
# its result names the rule once a turn, the notice says advisory, the journal and
# `runs` count every such call; and, as the control, an authored rule still refuses.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

tool_results() {  # REQUEST -> the tool-role message contents, one per line
    grep -o '"role":"tool","tool_call_id":"[^"]*","content":"[^"]*' "$1" 2>/dev/null
}

# --- 1-2: the scanner, offline ------------------------------------------------
printf 'Do not touch build.zig. Verify by running zig build test.\n' > "$tmp/mis.md"
out=$("$BIN" constraints scan "$tmp/mis.md" 2>&1); rc=$?
if [ "$rc" -eq 0 ] && ! printf '%s' "$out" | grep -q 'would be ADOPTED'; then
    t_ok "a negation does not reach into the next sentence (M730's misparse: none)"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -3)"
fi
printf 'Do not run build. Do not run tests!\n' > "$tmp/right.md"
out=$("$BIN" constraints scan "$tmp/right.md" 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q 'build' &&
   printf '%s' "$out" | grep -q 'run_tests'; then
    t_ok "the prompt M730 found right still infers its rules (build, tests)"
else
    t_fail "rc=$rc: $(printf '%s' "$out" | head -4)"
fi

# --- 3-7: a live --auto run against an inferred rule ---------------------------
# Two run_tests calls against an inferred "do not run tests". Both must run; the
# first result carries the note, the second does not repeat it.
cat > "$tmp/adv.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_tests {}
rule
  count 2
  tool run_tests {}
rule
  text ADV_DONE
EOF
ws=$(smoke_tmp)
mkdir -p "$tmp/ja"
mm_start "$tmp/adv.mm" "$tmp/capA" 8
write_config "$tmp/config.json" "$MM_PORT" '"testCommand":"echo TESTS_RAN_HERE"'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --journal "$tmp/ja/j.jsonl" -p "Do not run tests. Then say ADV_DONE." \
    < /dev/null > "$tmp/outA" 2> "$tmp/errA")
mm_stop
last=$(ls "$tmp"/capA/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)

# 3: the call RAN -- the result is the test's own output, not a refusal.
res=$(tool_results "$last")
if printf '%s' "$res" | grep -q 'TESTS_RAN_HERE' &&
   ! printf '%s' "$res" | grep -q 'blocked by an active constraint'; then
    t_ok "a call against an inferred rule runs (advisory, not refused)"
else
    t_fail "tool results: $(printf '%s' "$res" | head_bytes 300)"
fi

# 4: the result names the rule, once a turn.
n=$(printf '%s\n' "$res" | grep -c 'inferred from your request')
if [ "$n" = "1" ]; then
    t_ok "the result names the inferred rule it went against, once a turn"
else
    t_fail "expected the note once in the two results, found $n"
fi

# 5: the operator's notice says what the rule is -- advice.
if grep -q 'ADVISORY for THIS SESSION' "$tmp/errA" && ! grep -q 'enforced for THIS' "$tmp/errA"; then
    t_ok "the adoption notice says advisory, not enforced"
else
    t_fail "notice: $(grep constraint "$tmp/errA" | head_bytes 300)"
fi

# 6: the journal marks the adoption advisory and counts EVERY call against it.
na=$(grep -c '"event":"constraint_advisory"' "$tmp/ja/j.jsonl" 2>/dev/null)
if [ "$na" = "2" ] && grep '"event":"constraint"' "$tmp/ja/j.jsonl" | grep -q '"advisory":true'; then
    t_ok "the journal records the adoption as advisory and both calls against it"
else
    t_fail "constraint_advisory events: $na; adoption: $(grep '"event":"constraint"' "$tmp/ja/j.jsonl" | head_bytes 200)"
fi

# 7: ...and the count reaches the table a supervisor reads.
if "$BIN" runs "$tmp/ja" --output json < /dev/null 2>/dev/null | grep -q '"advised":2'; then
    t_ok "runs --output json reports advised=2"
else
    t_fail "runs: $("$BIN" runs "$tmp/ja" --output json < /dev/null 2>&1 | head_bytes 200)"
fi

# --- 8: the control -- a rule the operator WROTE still refuses ------------------
cat > "$tmp/auth.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_tests {}
rule
  text AUTH_DONE
EOF
ws2=$(smoke_tmp)
mkdir -p "$ws2/.jichi"
printf '%s\n' "deny-tool run_tests" > "$ws2/.jichi/constraints.md"
mm_start "$tmp/auth.mm" "$tmp/capB" 4
write_config "$tmp/config2.json" "$MM_PORT" '"testCommand":"echo TESTS_RAN_HERE"'
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config2.json" --no-session \
    --auto -p "Then say AUTH_DONE." < /dev/null > /dev/null 2>&1)
mm_stop
res=$(tool_results "$(ls "$tmp"/capB/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)")
if printf '%s' "$res" | grep -q 'blocked by an active constraint' &&
   ! printf '%s' "$res" | grep -q 'TESTS_RAN_HERE'; then
    t_ok "control: an authored rule still refuses the call"
else
    t_fail "an authored deny-tool did not refuse: $(printf '%s' "$res" | head_bytes 300)"
fi

t_done
