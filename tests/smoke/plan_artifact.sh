#!/bin/sh
# smoke: the plan artifact -- written in plan mode through one tool into one
# path, refused when incomplete, and reconciled against what the run then
# wrote (M631).
#
# THE GAP. Plan mode was a read-only fence plus a prose request: the plan
# lived in the conversation, where compaction can drop it, and nothing
# compared it with what was then done. Now `write_plan` (the one write plan
# mode allows -- `plan_allowed`, honoured by the permission verdict and by
# the tool advertiser) writes .jichi/PLAN.md with five sections, and an
# incomplete plan comes back as a tool ERROR VALUE naming the missing part.
# At run end the reach footer names any file the run wrote that `## Touches`
# did not predict -- drift, reported not fenced: a plan is a prediction,
# --edit-scope is the fence.
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
echo "v1" > "$ws/a.txt"

# --- 1-3: in plan mode the model writes a complete plan ----------------------------
cat > "$tmp/plan.mm" <<'EOF2'
wire openai
rule
  match "\"role\":\"tool\""
  text PLAN_WRITTEN
  usage 20 5
rule
  tool write_plan {"claim":"Change a.txt to v2 so the reader sees the new version.","rejected":"leave a.txt alone -- the reader would keep seeing v1","falsifier":"a.txt still reads v1 after the run","not_goals":"b.txt","touches":"a.txt"}
EOF2
mm_start "$tmp/plan.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --plan --output jsonl \
    -p "plan the change" < /dev/null > "$tmp/plan.jsonl" 2>/dev/null); rc=$?
mm_stop
if [ -f "$ws/.jichi/PLAN.md" ] && grep -q "^## Falsifier" "$ws/.jichi/PLAN.md"; then
    t_ok "plan mode wrote .jichi/PLAN.md with its sections (a write plan mode allows)"
else
    t_fail "no PLAN.md (rc=$rc): $(ls "$ws/.jichi" 2>/dev/null | tr '\n' ' ')"
fi
if grep -q '"type":"tool_result","name":"write_plan","is_error":false' "$tmp/plan.jsonl"; then
    t_ok "write_plan was accepted in plan mode (not denied as a mutating tool)"
else
    t_fail "write_plan not accepted: $(grep write_plan "$tmp/plan.jsonl" | head_bytes 200)"
fi
if [ "$(cat "$ws/a.txt")" = "v1" ]; then
    t_ok "plan mode still wrote nothing else (a.txt untouched)"
else
    t_fail "plan mode changed a.txt"
fi
# --- 3b: the model was TOLD the tool exists -- the advertiser honours plan_allowed.
# A tool the permission layer allows but the advertiser hides is a tool the
# model cannot call; the mock ignores the tool list, so this must be checked on
# the wire, not inferred from the call above.
# The advertised tool carries its description; the later request's HISTORY
# echoes the call as `"name":"write_plan","arguments"` -- a first cut of this
# check matched that echo and could not go red when the advertiser was broken.
if grep -l '"name":"write_plan","description"' "$tmp/cap1"/* > /dev/null 2>&1; then
    t_ok "the plan-mode request advertised write_plan to the model"
else
    t_fail "write_plan absent from the plan-mode request's tool list"
fi

# --- 4: an incomplete plan is an error VALUE naming the missing section -----------
cat > "$tmp/bad.mm" <<'EOF2'
wire openai
rule
  match "\"role\":\"tool\""
  text GAVE_UP
rule
  tool write_plan {"claim":"Change a.txt.","rejected":"nothing -- fine","falsifier":"","not_goals":"b.txt","touches":"a.txt"}
EOF2
rm -f "$ws/.jichi/PLAN.md"
mm_start "$tmp/bad.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --plan --output jsonl \
    -p "plan it" < /dev/null > "$tmp/bad.jsonl" 2>/dev/null)
mm_stop
if grep -q '"name":"write_plan","is_error":true' "$tmp/bad.jsonl" && \
   grep -q "Falsifier" "$tmp/bad.jsonl" && [ ! -f "$ws/.jichi/PLAN.md" ]; then
    t_ok "a plan with no falsifier is refused, the section named, nothing written"
else
    t_fail "incomplete plan not refused: $(grep write_plan "$tmp/bad.jsonl" | head_bytes 240)"
fi

# --- 5-6: the run is reconciled against Touches ------------------------------------
# Re-plant the good plan (Touches: a.txt), then an --auto run that writes b.txt.
printf '# Plan\n\n## Claim\nChange a.txt.\n\n## Rejected\n- leave it -- stale\n\n## Falsifier\na.txt reads v1.\n\n## Not-goals\nb.txt\n\n## Touches\n- a.txt\n' > "$ws/.jichi/PLAN.md"
cat > "$tmp/drift.mm" <<'EOF2'
wire openai
rule
  match "\"role\":\"tool\""
  text DRIFTED
  usage 20 5
rule
  tool write_file {"path":"b.txt","content":"surprise"}
EOF2
mm_start "$tmp/drift.mm" "$tmp/cap3"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto --budget-tokens 100k \
    -p "do it" < /dev/null > /dev/null 2> "$tmp/drift.err")
mm_stop
if grep -q "plan drift: b.txt" "$tmp/drift.err"; then
    t_ok "the footer names the file the run wrote that the plan did not predict"
else
    t_fail "no drift line: $(grep -iE "plan|checked" "$tmp/drift.err" | head_bytes 240)"
fi
# the same run through JSON
mm_start "$tmp/drift.mm" "$tmp/cap4"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto --budget-tokens 100k \
    --output json -p "do it" < /dev/null > "$tmp/drift.json" 2>/dev/null)
mm_stop
d=$("$JQ" .reach.plan_drift < "$tmp/drift.json" 2>/dev/null)
if [ "$d" = "1" ]; then
    t_ok "the done object's reach carries plan_drift 1"
else
    t_fail "plan_drift in JSON wrong ($d): $(head_bytes 200 "$tmp/drift.json")"
fi

# --- 7: a run that touched only what it predicted reports no drift -----------------
cat > "$tmp/ok.mm" <<'EOF2'
wire openai
rule
  match "\"role\":\"tool\""
  text AS_PLANNED
  usage 20 5
rule
  tool write_file {"path":"a.txt","content":"v2"}
EOF2
mm_start "$tmp/ok.mm" "$tmp/cap5"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto --budget-tokens 100k \
    -p "do it" < /dev/null > /dev/null 2> "$tmp/ok.err")
mm_stop
if grep -q "plan: 1 of 1 predicted file" "$tmp/ok.err" && ! grep -q "plan drift" "$tmp/ok.err"; then
    t_ok "a run inside its own prediction reports the plan honoured, no drift"
else
    t_fail "honoured plan not reported: $(grep -iE "plan|checked" "$tmp/ok.err" | head_bytes 240)"
fi

# --- 8: BOUNDARY -- no plan file, no plan line at all --------------------------------
rm -f "$ws/.jichi/PLAN.md"
mm_start "$tmp/ok.mm" "$tmp/cap6"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto --budget-tokens 100k \
    -p "do it" < /dev/null > /dev/null 2> "$tmp/noplan.err")
mm_stop
if ! grep -qi "plan" "$tmp/noplan.err"; then
    t_ok "without a plan file the footer says nothing about plans (no invented absence)"
else
    t_fail "plan mentioned with no plan file: $(grep -i plan "$tmp/noplan.err" | head_bytes 200)"
fi
t_done
