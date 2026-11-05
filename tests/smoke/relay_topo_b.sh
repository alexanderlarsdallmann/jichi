#!/bin/sh
# smoke: topology B -- the relay's next leg runs IN THE SAME PROCESS
# (M952, the relay loop's second slice; design
# docs/internal/2026-10-14-topology-b-design.md; proposal 6.8).
#
# THE RIG: one process, one isolated HOME, relay on. Every model call reports
# usage at 92.5% of the window (E3: the server's count at or above 90%), does
# one tool call, and the round-boundary relay check hands over. Each leg
# writes its baton BEFORE the loop checks the ceiling (the baton is the
# handover record -- written even on the last leg, so a supervisor can pick it
# up). So relayMaxLegs 3 runs THREE legs in ONE process: three batons
# (leg-01.md, leg-02.md, leg-03.md), six model requests (the mock's E3
# continuation reply adds a second call per leg), stop_reason handover on the
# last. The relayMaxLegs 1 control (the pre-M952 behaviour, topology A's
# end-of-run) makes TWO model requests and writes ONE baton.
#
# WHAT IT HOLDS, each against the effect:
#   1. the run ended on handover (not error, not done).
#   2. SIX model requests were served in one process (three legs; the loop
#      continued).
#   3. THREE batons (leg-01.md .. leg-03.md) under one relay dir.
#   4. the control (relayMaxLegs 1) made TWO requests and wrote ONE baton --
#      the loop's exit is the ceiling, not an absence of work.
#   5. (M1128, R12-N2) every in-process leg's FIRST USER MESSAGE begins with
#      the operator's prompt, and the on-disk baton agrees: leg-03.md's
#      "## 1. The task" section carries the prompt's line. Without the fix the
#      reseed reads the per-turn scratch arena after the leg's own turn has
#      reset it, so leg 2's and leg 3's first user message is garbage.
#
# WITHOUT the in-process loop, the main run ends after the first handover --
# two requests and ONE baton, identical to the control: checks 2 and 3 go red,
# check 4 stays green (it is the old behaviour).
# Red-first: run this against the pre-loop binary, watch 2/3 fail, then add
# the loop. (The M951 lesson: do NOT set BIN here -- the smoke lib's BIN
# honours JC_SMOKE_BIN, the mutant sweep's seam.)
. "$(dirname "$0")/_smoke.sh"
t_plan 5

tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# Two HOMEs: one per run, so the relay dirs (and baton leg numbers) are
# independent. A shared HOME would make the control's leg-01.md the main
# run's leg-04.md and the counts wrong. HOME is exported (not `env HOME=`:
# with_deadline is a shell function, which `env` cannot execute).
home_main=$(smoke_tmp)
home_ctl=$(smoke_tmp)
chmod 700 "$home_main" "$home_ctl" 2>/dev/null || true

printf 'a file for the topology-B rig\n' > "$ws/a.txt"

# The per-leg E3 exhaustion pattern: the server's usage is 185000 against a
# 200000 window (92.5%, over the 90% threshold) on every model call; one
# read_file tool call per leg; the round-boundary relay check hands over.
cat > "$tmp/tb.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 185000 5
  tool read_file {"path":"a.txt"}
EOF

# --- the main run: relayMaxLegs 3, one process, three in-process legs -------
mm_start "$tmp/tb.mm" "$tmp/cap"
write_config "$tmp/config3.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true,"relayInProcess":true,"relayMaxLegs":3' '"contextLength":200000'
HOME="$home_main"
export HOME
(cd "$ws" && with_deadline 120 "$BIN" \
    --config "$tmp/config3.json" --output json --max-tool-calls 50 \
    -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/run3.json" 2> "$tmp/run3.err"); rc3=$?
mm_stop

stop3=$("$JQ" '.stop_reason' "$tmp/run3.json" 2>/dev/null)
reqs3=$(ls "$tmp/cap" 2>/dev/null | grep -c '^req\.')
rdir3=$(ls -d "$home_main"/.jichi.d/relay/*/ 2>/dev/null | head -1)
batons3=$(ls "$rdir3" 2>/dev/null | grep -c '^leg-[0-9][0-9]\.md$')
b33=$(ls "$rdir3/leg-03.md" 2>/dev/null | wc -l | tr -d ' ')

# 1: the run ended on a handover (the last leg hit the ceiling and ended the
# relay the topology-A way). Not done, not error.
if [ "$rc3" -eq 0 ] && [ "$stop3" = "handover" ]; then
    t_ok "the topology-B run ended on handover (rc=0, stop=handover)"
else
    t_fail "topology-B run: rc=$rc3 stop='$stop3' (want rc=0, stop=handover)"
fi

# 2: SIX model requests served in ONE process -- three legs, two requests each
# (the mock's E3 continuation reply triggers a second call per leg; a mock
# artifact, not the loop's concern). The control makes 2 (one leg), so the
# main run made exactly THREE times the control's requests. Without the
# in-process loop the main run ends after the first handover: 2 requests and
# 1 baton -- identical to the control, so this check and check 3 go red.
if [ "$reqs3" -eq 6 ]; then
    t_ok "six model requests served in one process (three in-process legs)"
else
    t_fail "model requests served: $reqs3 (want 6; two per leg x three legs)"
fi

# 3: THREE batons under one relay dir -- leg 1, leg 2 and the ceiling leg's
# leg-03.md all landed in the same run.
if [ "$b33" -eq 1 ] && [ "$batons3" -eq 3 ]; then
    t_ok "three batons (leg-01.md .. leg-03.md) under one relay dir"
else
    t_fail "batons: leg-03=$b33 total=$batons3 (want 1, 3)"
fi

# 5: (M1128, R12-N2) the later legs reseed from the OPERATOR'S prompt. req.3 is
# leg 2's first model request and req.5 leg 3's (two requests per leg; check 2
# holds the count). The first user message of each MUST BEGIN with the prompt:
# the prefix is pinned by jsonq on .messages[1].content (the first USER message;
# .messages[0] is the system prompt), NOT a grep over the whole body -- the
# baton's fenced notes carry leg 1's task text anyway, so a substring search
# would find the prompt even when the reseed is garbage. The on-disk half:
# leg-03.md's "## 1. The task" section carries the prompt's line (the first
# non-empty line after the header).
PROMPT='read the file and tell me what it says'
# The mockmodel capture is a full HTTP request: headers, a blank line, then the
# JSON body. jsonq wants just the body, so strip to the first '{' line. The
# first USER message is .messages[1] (.messages[0] is the system prompt).
body3=$(sed -n '/^{/,$p' "$tmp/cap/req.3" 2>/dev/null)
body5=$(sed -n '/^{/,$p' "$tmp/cap/req.5" 2>/dev/null)
first3=$(printf '%s' "$body3" | "$JQ" .messages[1].content 2>/dev/null)
first5=$(printf '%s' "$body5" | "$JQ" .messages[1].content 2>/dev/null)
btask3=$(awk '/^## 1\. The task \(verbatim, from leg 1\)$/ { f = 1; next } f && NF { print; exit }' "$rdir3/leg-03.md" 2>/dev/null)
# A non-empty extraction that BEGINS with the prompt: strip the prompt exactly
# and compare with the whole string (the classic POSIX prefix test; the prompt
# is a fixed literal with no glob chars, and the QUOTED strip leaves no pattern
# at all). An empty extraction is a failed read, never a match.
p3=0; p5=0; pb=0
[ -n "$first3" ] && [ "${first3#"$PROMPT"}" != "$first3" ] && p3=1
[ -n "$first5" ] && [ "${first5#"$PROMPT"}" != "$first5" ] && p5=1
[ -n "$btask3" ] && [ "${btask3#"$PROMPT"}" != "$btask3" ] && pb=1
if [ "$p3" -eq 1 ] && [ "$p5" -eq 1 ] && [ "$pb" -eq 1 ]; then
    t_ok "every in-process leg's first user message begins with the operator's prompt (and leg-03's baton task line agrees)"
else
    t_fail "M1128 (R12-N2): the reseed lost the task -- leg2 req.3 prefix=$p3 leg3 req.5 prefix=$p5 baton task line='$(printf %s "$btask3" | tr -c '[:print:]' '?')' (want all three to begin with the prompt)"
fi

# --- the control: relayMaxLegs 1 -- the ceiling is the FIRST handover -------
mm_start "$tmp/tb.mm" "$tmp/capc"
write_config "$tmp/config1.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true,"relayInProcess":true,"relayMaxLegs":1' '"contextLength":200000'
HOME="$home_ctl"
export HOME
(cd "$ws" && with_deadline 120 "$BIN" \
    --config "$tmp/config1.json" --output json --max-tool-calls 50 \
    -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/run1.json" 2> "$tmp/run1.err"); rc1=$?
mm_stop

stop1=$("$JQ" '.stop_reason' "$tmp/run1.json" 2>/dev/null)
reqs1=$(ls "$tmp/capc" 2>/dev/null | grep -c '^req\.')
rdir1=$(ls -d "$home_ctl"/.jichi.d/relay/*/ 2>/dev/null | head -1)
batons1=$(ls "$rdir1" 2>/dev/null | grep -c '^leg-[0-9][0-9]\.md$')

# 4: the control (relayMaxLegs 1) ends at the FIRST handover: one request,
# one baton, stop_reason handover. This is the pre-M952 behaviour (topology A)
# and proves the loop's exit is the ceiling, not an absence of work.
if [ "$rc1" -eq 0 ] && [ "$stop1" = "handover" ] && \
   [ "$reqs1" -eq 2 ] && [ "$batons1" -eq 1 ]; then
    t_ok "control (relayMaxLegs 1): two requests, one baton, handover"
else
    t_fail "control: rc=$rc1 stop='$stop1' reqs=$reqs1 batons=$batons1 (want 0/handover/2/1)"
fi

t_done
