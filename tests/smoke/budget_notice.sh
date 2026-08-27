#!/bin/sh
# smoke: the envelope tells the AGENT when a budget nears its end (M347).
#
# The human watching the TUI reads ctx% and $ live in the prompt line; the
# agent flying a bounded run was told nothing until the engine stopped -- 0/7
# implementation runs completed in the 2026-08-07 driving session, every one
# budget_exhausted, and M96's starved analysis dies with its report unwritten.
# M347: once per run, at the first ~80% crossing of any armed budget, one
# "[envelope] budget check:" line lands as a user message the next model call
# (M368: the greps pin the COLON -- the notice's structural marker -- because
# the M355 flight plan legitimately teaches the phrase "One [envelope] budget
# check arrives ..." in every armed run's system prompt, which made the
# colon-less absence check red and the presence check hollow from M355 on)
# sees, asking the agent to land the run.
#
# A 5-call cap makes the crossing exact (integer four-fifths: due after call
# 4). The assertions read mockmodel's captured request bodies -- the prompt
# the model actually receives -- and pair presence with absence per M310: the
# notice must be IN request 5 and NOT in request 4, and request 4 must exist
# before its silence means anything.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# count N = "fire on request number N" (see bg.sh): four tool rounds, then text.
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  count 1
  tool list_files {}
rule
  count 2
  tool list_files {}
rule
  count 3
  tool list_files {}
rule
  count 4
  tool list_files {}
rule
  text BUDGET_DONE
EOF
mm_start "$tmp/b.mm" "$tmp/cap" 6
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session --max-tool-calls 5 --journal "$tmp/j.jsonl" -p "go" \
    < /dev/null) >/dev/null 2>"$tmp/err"
rc=$?
mm_stop

# --- 1: the artifact before the crossing exists (M310 pairing) ---------------
if [ -s "$tmp/cap/req.4" ]; then
    t_ok "request 4 captured (3 calls in -- below the four-fifths line)"
else
    t_fail "no request 4 -- the absence check below proves nothing"
fi

# --- 2: ...and carries no notice yet ------------------------------------------
if grep -q "\[envelope\] budget check:" "$tmp/cap/req.4" 2>/dev/null; then
    t_fail "the notice fired before the crossing (request 4)"
else
    t_ok "no notice below the crossing"
fi

# --- 3: the request after call 4 carries the notice ---------------------------
if grep -q "\[envelope\] budget check:" "$tmp/cap/req.5" 2>/dev/null && \
   grep -q "4 of 5 tool calls" "$tmp/cap/req.5" 2>/dev/null; then
    t_ok "the model is told: 4 of 5 tool calls used, land the run"
else
    t_fail "no budget notice in request 5: $(head_bytes 200 "$tmp/cap/req.5" 2>/dev/null)"
fi

# --- 4: the journal records that the notice was given -------------------------
if grep -Eq '"event": ?"budget_notice"' "$tmp/j.jsonl" 2>/dev/null && \
   grep -Eq '"kind": ?"tool_calls"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "journal carries the budget_notice event with its kind"
else
    t_fail "no budget_notice journal event: $(cat "$tmp/j.jsonl" 2>/dev/null | head -3)"
fi

# --- 5: the notice is advisory -- the run still lands cleanly -----------------
if [ "$rc" -eq 0 ]; then
    t_ok "the run finished under the cap (exit 0); the notice changed no outcome"
else
    t_fail "run exited $rc: $(head_bytes 200 "$tmp/err")"
fi

t_done
