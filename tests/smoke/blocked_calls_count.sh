#!/bin/sh
# smoke: a BLOCKED tool call spends the run's --max-tool-calls (M459).
#
# THE DEFECT THIS EXISTS FOR. Measured on chrtext (probe P13, docs/analysis/
# 2026-08-13-edge-case-probes.md): under --strict-scope a model tried the same
# forbidden write five times and spent its ENTIRE 150k token budget on an action
# that could not succeed. blocked_repeat.sh records why no defence caught it --
# a block is journaled `blocked: true` rather than `ok:false`, so the failure
# detectors cannot see it, "and a blocked call does not count toward
# --max-tool-calls, so the tool-call cap never fires either."
#
# M429 fixed the first half by TELLING the model it is repeating. That relies on
# the model then cooperating. HARDENING.md 6b is explicit that the defences worth
# having are the ones that do not: a cap is one of those, and it was not applying.
#
# The operator's flag says "at most N tool calls". It bounded what the agent was
# PERMITTED to do, not what it ATTEMPTED -- so the more forbidden things a run
# tried, the less its cap meant. This driver pins the corrected reading.
#
# Counted at the point of ATTEMPT, before any gate, so every gate present and
# future is covered without each having to remember.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Every call is the same forbidden out-of-scope write, exactly as P13's model did
# it unprompted. None can ever succeed, so under the old reading the cap could
# never fire no matter how many were attempted.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 2
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 3
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 4
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 5
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  text GAVE_UP
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 8
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      --no-session --auto \
      --edit-scope 'allowed.md' --strict-scope \
      --max-tool-calls 2 \
      --journal "$tmp/run.jsonl" \
      -p "create the file" < /dev/null 2>&1); rc=$?
mm_stop

# `grep -c` prints 0 AND exits 1 when nothing matches, so `|| echo 0` would
# append a SECOND zero and every later [ ] test would die on "Illegal number".
# Learned here the hard way, in this driver's own first run.
nblock=$(grep -c '"blocked":true' "$tmp/run.jsonl" 2>/dev/null || true)
[ -n "$nblock" ] || nblock=0

# --- 1: the calls really were blocked ----------------------------------------
# Without this the rest could pass because nothing was attempted at all.
if [ "$nblock" -ge 2 ]; then
    t_ok "the forbidden call was blocked $nblock times (the case is live)"
else
    t_fail "only $nblock blocks journaled -- this driver would be testing \
nothing; rc=$rc out=$(printf '%s' "$out" | tail -c 150)"
fi

# --- 2: THE DEFECT -- the cap must actually bound the attempts ----------------
# This is the whole measurement, and it must be a COUNT, not a grep for the word
# "budget" somewhere in the output. My first draft did the latter and reported
# the cap had fired while check 1 was simultaneously reporting five blocked
# calls under a cap of two -- a check that passed on a run that disproved it.
#
# The mock offers five forbidden calls. Under --max-tool-calls 2 the run must
# stop at the cap; one extra in flight when the check trips is tolerable, five
# is the defect.
if [ "$nblock" -le 3 ]; then
    t_ok "$nblock blocked attempts under a cap of 2 -- the cap bounds attempts"
else
    t_fail "$nblock blocked attempts under --max-tool-calls 2: the cap bounds \
only PERMITTED work, so the more a run is refused the less its cap means"
fi

# --- 3: the run ended on the tool-call budget, and says so -------------------
if grep -q 'tool_calls' "$tmp/run.jsonl" 2>/dev/null && \
   grep -q '"budget"' "$tmp/run.jsonl" 2>/dev/null; then
    t_ok "the journal records a budget stop"
else
    t_fail "no budget stop journaled: $(tail -c 200 "$tmp/run.jsonl")"
fi

# --- 4: the journal reports what it counted ----------------------------------
# A cap that fires but reports a number excluding the calls that tripped it
# would be the M443 reporting problem wearing a budget hat.
if grep -q '"tool_calls"' "$tmp/run.jsonl" 2>/dev/null; then
    t_ok "the run journal carries a tool_calls figure"
else
    t_fail "no tool_calls in the journal: $(tail -c 200 "$tmp/run.jsonl")"
fi

# --- 5: a capped run is not a crash ------------------------------------------
# A budget stop is a circuit breaker. It must exit cleanly, as stop_reason_capped
# already asserts for the iteration cap.
if [ "$rc" -le 1 ]; then
    t_ok "a cap stop exits cleanly (rc=$rc)"
else
    t_fail "rc=$rc -- a budget stop should not look like a crash"
fi

t_done
