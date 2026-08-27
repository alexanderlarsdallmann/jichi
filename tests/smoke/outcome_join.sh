#!/bin/sh
# smoke: telemetry and the run journal agree about what failed (M536).
#
# THE DEFECT. M420 gave the two richest sinks a shared `run` key for exactly one
# purpose: so a supervisor can JOIN them -- telemetry carries behaviour (tokens,
# latency, tool ok-rates), the journal carries outcome (budget, verify, rollback).
# What it did not reconcile is how each names the result of a tool call. From the
# same `res.is_error`, 200 lines apart in one function:
#
#     telemetry:  cJSON_AddBoolToObject(o,  "ok",    res.is_error ? 0 : 1);
#     journal:    cJSON_AddBoolToObject(jo, "error", res.is_error ? 1 : 0);
#
# Different name, opposite polarity. Join on `run` and filter `.ok` over journal
# rows: undefined -> falsy -> EVERY row reads as a failure. Filter `.error` over
# telemetry rows: undefined -> falsy -> EVERY row reads as a success. Both
# directions are silently wrong and both look right, which is the worst property
# an audit trail can have -- it answers confidently either way.
#
# AND THE JOURNAL COULD NOT TELL A RED GATE FROM A BROKEN TOOL. M168 added `exit`
# to telemetry precisely because `ok` alone cannot: 199 of 294 apparent tool
# failures in real dogfood data were RED GATES, i.e. the agent working correctly
# inside a fix-forward loop. The journal -- the sink an operator reads to judge a
# run, the one the envelope exists to produce -- never carried that column, so a
# correctly-failing test was recorded there as indistinguishable from a
# malfunction. Every one of those 199 would have read as damage.
#
# WHY BOTH NAMES RATHER THAN ONE. Renaming either field breaks whoever already
# reads it, and neither is listed in docs/EMBEDDING.md, so there is no version to
# bump against. Both names now ride on both rows, written by ONE helper from ONE
# argument (stamp_outcome), so they cannot drift apart -- two fields derived from
# one predicate in two places is the exact shape M532 and M535 were about.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# The fixture: one tool call that FAILS as a red gate (a command exiting 1, which
# is the M168 case), then one that SUCCEEDS, then a closing text. Two outcomes in
# one run means the checks below cannot pass by everything having one polarity.
mkdir -p "$ws"
printf 'ok\n' > "$ws/present.txt"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"exit 3"}
rule
  count 2
  tool read_file {"path":"present.txt"}
rule
  text OUTCOME_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 200k \
    --journal "$tmp/j.jsonl" --log "$tmp/telem.jsonl" --log-level metrics \
    -p "run the command then read the file" < /dev/null) > /dev/null 2>&1
mm_stop

# ---- 1: both sinks exist and carry tool_call rows (the denominator) --------
# Nothing below means anything without this, and a silently empty sink is how a
# join check passes vacuously.
tn=$("$G" -c '"event":"tool_call"' "$tmp/telem.jsonl" 2>/dev/null || echo 0)
jn=$("$G" -c '"event":"tool_call"' "$tmp/j.jsonl" 2>/dev/null || echo 0)
if [ "$tn" -ge 2 ] && [ "$jn" -ge 2 ]; then
    t_ok "both sinks recorded the run ($tn telemetry, $jn journal tool_calls)"
else
    t_fail "telemetry=$tn journal=$jn tool_call rows (want >= 2 each) -- \
nothing below can be trusted"
fi

# ---- 2: every telemetry tool_call carries BOTH names ----------------------
# A reader arriving with either vocabulary must get an answer.
tboth=$("$G" '"event":"tool_call"' "$tmp/telem.jsonl" 2>/dev/null \
        | "$G" -c '"ok":' | tr -d ' ')
terr=$("$G" '"event":"tool_call"' "$tmp/telem.jsonl" 2>/dev/null \
       | "$G" -c '"error":' | tr -d ' ')
if [ "$tboth" = "$tn" ] && [ "$terr" = "$tn" ]; then
    t_ok "all $tn telemetry tool_calls carry both ok and error"
else
    t_fail "of $tn telemetry rows, $tboth have ok and $terr have error"
fi

# ---- 3: every journal tool_call carries BOTH names ------------------------
jboth=$("$G" '"event":"tool_call"' "$tmp/j.jsonl" 2>/dev/null \
        | "$G" -c '"ok":' | tr -d ' ')
jerr=$("$G" '"event":"tool_call"' "$tmp/j.jsonl" 2>/dev/null \
       | "$G" -c '"error":' | tr -d ' ')
if [ "$jboth" = "$jn" ] && [ "$jerr" = "$jn" ]; then
    t_ok "all $jn journal tool_calls carry both ok and error"
else
    t_fail "of $jn journal rows, $jboth have ok and $jerr have error"
fi

# ---- 4: the two names never contradict each other ------------------------
# The property that matters. ok:true beside error:true, or ok:false beside
# error:false, is worse than either field alone -- it makes the row unreadable.
# Counted across BOTH files at once, since the invariant is about the pair.
bad=$(cat "$tmp/telem.jsonl" "$tmp/j.jsonl" 2>/dev/null \
      | "$G" '"event":"tool_call"' \
      | "$G" -c -e '"ok":true.*"error":true' -e '"ok":false.*"error":false' \
      || true)
[ -n "$bad" ] || bad=0
if [ "$bad" = "0" ]; then
    t_ok "no row states both outcomes at once (polarity is consistent)"
else
    t_fail "$bad rows contradict themselves: \
$(cat "$tmp/telem.jsonl" "$tmp/j.jsonl" | "$G" -e '"ok":true.*"error":true' \
  -e '"ok":false.*"error":false' | head -1 | head_bytes 200)"
fi

# ---- 5: the journal can tell a red gate from a broken tool (M168) --------
# `exit 3` is a command that FAILED with a known status. The journal must now
# record that status, or an operator reading it still cannot distinguish "the
# test failed, correctly reported" from "the tool broke".
if "$G" '"event":"tool_call"' "$tmp/j.jsonl" 2>/dev/null \
   | "$G" -q '"exit":3'; then
    t_ok "the journal records exit 3, so a red gate is distinguishable"
else
    t_fail "no exit status in the journal's tool_call rows: \
$("$G" '"event":"tool_call"' "$tmp/j.jsonl" 2>/dev/null | head -1 | head_bytes 220)"
fi

# ---- 6: ONE writer, so the two fields cannot drift apart -----------------
# The structural half. The behavioural checks above pass equally well for two
# hand-written pairs that happen to agree today; what keeps them agreeing is
# that only stamp_outcome() writes either name. Floor: 2 lines inside the helper,
# and zero anywhere else in the file.
inside=$("$G" -c 'cJSON_AddBoolToObject(o, "ok"\|cJSON_AddBoolToObject(o, "error"' \
         src/chat/jc_agent.c | tr -d ' ')
calls=$("$G" -c 'stamp_outcome(' src/chat/jc_agent.c | tr -d ' ')
if [ "$inside" = "2" ] && [ "$calls" -ge 5 ]; then
    t_ok "only stamp_outcome writes ok/error ($calls call sites + definition)"
else
    t_fail "hand-written ok/error lines: $inside (want exactly 2, both inside \
stamp_outcome); stamp_outcome mentions: $calls (want >= 5). The first number \
above 2 means a sink is writing the pair itself again; the second below 5 means \
a sink stopped calling the helper."
fi

t_done
