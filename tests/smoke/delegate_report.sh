#!/bin/sh
# smoke: one structured report for a delegated run (M437).
#
# THE DEFECT. `spawn_subagent` returned the delegate's prose and, on failure, one of
# two fixed strings -- "error: sub-agent interrupted" / "error: sub-agent run failed".
# A parent could not tell an edit-scope DENIAL from a tool error from a refusal, so its
# only moves were to re-delegate identically (paying the whole subtask twice) or give up.
# On a token-metered run both are expensive and neither is informed.
#
# TWO RUNS, because the two halves cannot be observed together:
#   A. the report's content -- a delegate whose write is DENIED, then answers.
#   B. the GRANDCHILD POISON regression: `run_agent_loop` cleared last_run_capped on
#      ENTRY only, so a capped grandchild left the flag set and a child that then
#      finished cleanly was reported to its parent as capped. jc_app.h documented this
#      hazard and M322 fixed it for the top-level turn while leaving every intermediate
#      depth open. M437 fixes it by SAVING and RESTORING the per-run slots around every
#      jc_tool_execute -- restoring, not zeroing, so this run's own earlier failure is
#      not erased.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# ============================ run A: the report ==============================
# The delegate tries to write OUTSIDE the run's edit scope, is refused by the fence,
# then answers. So the delegation SUCCEEDS while containing a denial -- which is exactly
# the case the old two-string failure vocabulary could not express, and the case where
# the parent is the only party who can act (a delegate cannot widen its own fence).
cat > "$tmp/a.mm" <<'EOF'
wire openai
rule
  match "TOP alpha"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP alpha"
  tool spawn_subagent {"task":"CHILD alpha: write the notes"}
rule
  match "CHILD alpha"
  match "\"role\":\"tool\""
  text CHILDANSWER
rule
  match "CHILD alpha"
  tool write_file {"path":"outside/notes.md","content":"x"}
EOF

mkdir -p "$ws/allowed"
echo "seed" > "$ws/allowed/keep.txt"

mm_start "$tmp/a.mm" "$tmp/capa" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 400k \
    --edit-scope 'allowed/**' \
    -p "TOP alpha" < /dev/null > "$tmp/a.out" 2>&1); rca=$?
mm_stop

# The parent's request carrying the delegation's result. The subagent's own requests
# carry its task instead, so keying on the top-level prompt is the sound discriminator
# (a subagent is seeded with its task, never with the parent's conversation).
ptop=""
for f in "$tmp"/capa/req.*; do
    [ -f "$f" ] || continue
    grep -q "TOP alpha" "$f" && ptop="$ptop $f"
done

# --- 1: the fixture produced a parent request after the delegation --------------
if [ -n "$ptop" ] && [ "$rca" -eq 0 ]; then
    t_ok "run A completed and captured $(echo $ptop | wc -w) parent request(s)"
else
    t_fail "rca=$rca ptop='$ptop' out: $(head_bytes 140 "$tmp/a.out")"
fi

# --- 2: the report reaches the parent, with a stop reason ----------------------
if grep -l "\[delegate\] stop=done" $ptop >/dev/null 2>&1; then
    t_ok "the parent is told the delegate's stop reason"
else
    t_fail "no report: $(grep -ho '\[delegate\][^"]\{0,110\}' $ptop 2>/dev/null | head -1)"
fi

# --- 3: the DENIAL is named as policy, and the tool that hit it ---------------
# The decisive field. Reported even though the delegation succeeded: an answer produced
# after a denial is a different thing from one produced cleanly.
if grep -l "last failing call: write_file (denied)" $ptop >/dev/null 2>&1; then
    t_ok "the denial is reported with its tool and its class"
else
    t_fail "denial not classified: $(grep -ho 'last failing call[^"]\{0,90\}' $ptop 2>/dev/null | head -1)"
fi

# --- 4: and it names the parent's move, not just the cause --------------------
# A message stating only a cause is the class M342 measured as a loop amplifier. For a
# denial the parent's move is categorically different from a retry: the delegate cannot
# widen its own fence, so re-delegating is guaranteed to fail again.
if grep -l "refused again" $ptop >/dev/null 2>&1; then
    t_ok "the report says re-delegating will be refused again"
else
    t_fail "no actionable advice on the denial"
fi

# --- 5: a BLOCKED call is not counted as an executed one ----------------------
# The count here is 0 and that is correct: the write was refused by the fence before
# jc_tool_execute ran, so no tool executed. It is worth pinning rather than glossing,
# because the report says both things at once -- 0 executed calls AND a denied write --
# and a reader has to be able to trust that pairing. (A bare "is a count present?"
# check would have passed on a broken delta, since 0 is also what broken looks like;
# the non-zero case is pinned in check 6, where calls really ran.)
#
# The `.+` stands in for the U+00B7 separator on purpose: matching a multi-byte
# character through three layers of quoting is how the first cut of this check failed --
# it asserted a mojibake'd pattern and reported the product broken. The separator is
# presentation; the numbers are the property.
if grep -lE "stop=done .+ 0 tool calls" $ptop >/dev/null 2>&1 &&
   grep -l "write_file (denied)" $ptop >/dev/null 2>&1; then
    t_ok "a fence-blocked call counts as 0 executed calls and is reported as denied"
else
    t_fail "count/denial pairing wrong: $(grep -ho '\[delegate\] stop=[^"]\{0,40\}' $ptop 2>/dev/null | head -1)"
fi

# ================== run B: the grandchild-poison regression ==================
# TOP -> CHILD -> GRAND. GRAND loops on a tool until its own iteration cap; CHILD then
# answers cleanly. CHILD's report to TOP must say done, not max_iters.
#
# Rule order matters: CHILD's request after GRAND returns contains BOTH "CHILD beta"
# and (inside the spawn_subagent arguments) "GRAND beta", so the CHILD+tool-role rule
# has to be matched before the GRAND rule.
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  match "TOP beta"
  match "\"role\":\"tool\""
  text TOPDONE
rule
  match "TOP beta"
  tool spawn_subagent {"task":"CHILD beta: delegate once then answer"}
rule
  match "CHILD beta"
  match "\"role\":\"tool\""
  text CHILDCLEAN
rule
  match "CHILD beta"
  tool spawn_subagent {"task":"GRAND beta: loop"}
rule
  match "GRAND beta"
  tool read_file {"path":"allowed/keep.txt"}
EOF

mm_start "$tmp/b.mm" "$tmp/capb" 40
write_config "$tmp/config2.json" "$MM_PORT" '"maxSubagentIters":6,"maxSubagentDepth":2'

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    -q --no-session --auto --budget-tokens 900k \
    -p "TOP beta" < /dev/null > "$tmp/b.out" 2>&1); rcb=$?
mm_stop

btop=""
for f in "$tmp"/capb/req.*; do
    [ -f "$f" ] || continue
    grep -q "TOP beta" "$f" && btop="$btop $f"
done
# The CHILD's own requests: its task text, minus the parent's (which quotes it).
bchild=""
for f in "$tmp"/capb/req.*; do
    [ -f "$f" ] || continue
    grep -q "TOP beta" "$f" && continue
    grep -q "CHILD beta" "$f" && bchild="$bchild $f"
done

# --- 6: the fixture really nested three deep and the grandchild really capped ---
# The extraction floor. Without it, checks 7 and 8 could both pass over a run where no
# grandchild ever capped -- proving nothing about the poison.
# It must also carry a NON-ZERO tool-call count: the grandchild looped on read_file,
# so a zero here would mean the envelope delta never worked and check 5's zero was
# broken rather than correct.
if [ -n "$btop" ] && [ -n "$bchild" ] &&
   grep -l "stop=max_iters" $bchild >/dev/null 2>&1 &&
   grep -lE "stop=max_iters .+ [1-9][0-9]* tool call" $bchild >/dev/null 2>&1; then
    t_ok "the grandchild capped, with a non-zero call count, and the child was told"
else
    t_fail "fixture did not reproduce: top='$(echo $btop|wc -w)' child='$(echo $bchild|wc -w)' grand_capped=$(grep -lc 'stop=max_iters' $bchild 2>/dev/null | head -1)"
fi

# --- 7: the CHILD is not reported as capped ----------------------------------
# The regression itself. Pre-fix the child's report said stop=max_iters, inheriting the
# grandchild's cap through a slot the loop cleared on entry only.
if grep -l "stop=done" $btop >/dev/null 2>&1 &&
   ! grep -l "stop=max_iters" $btop >/dev/null 2>&1; then
    t_ok "the child reports done -- a capped grandchild no longer poisons it"
else
    t_fail "poisoned: $(grep -ho '\[delegate\] stop=[a-z_]*' $btop 2>/dev/null | sort -u | tr '\n' ' ')"
fi

# --- 8: the run still completes ----------------------------------------------
if [ "$rcb" -eq 0 ] && grep -q "TOPDONE" "$tmp/b.out"; then
    t_ok "the three-deep run completes unchanged (rc=0)"
else
    t_fail "rcb=$rcb out: $(head_bytes 160 "$tmp/b.out")"
fi

t_done
