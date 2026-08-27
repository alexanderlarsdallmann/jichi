#!/bin/sh
# smoke: a run stopped by a budget still records whether its gate passes (M506).
#
# THE DEFECT THIS EXISTS FOR, measured three times -- twice in one afternoon of
# dogfooding. The envelope runs the verifier at a budget exit ONLY when its
# result could change the rollback decision (rollback armed AND a green
# checkpoint banked). That is correct for a DECISION and wrong for a REPORT: a
# run whose gate was red at the start never banks a green, so the verifier is
# skipped, and the journal ends
#
#     start -> tool_call... -> budget -> end (outcome: budget_exhausted)
#
# with no `verify` event at all. One measured run had done the valuable half of
# its task -- its gate passed when evaluated by hand a minute later -- and the
# operator found the finished work only by running `git status`.
#
# The verdict is ADVISORY BY CONSTRUCTION: journalled, logged, then dropped. It
# must never turn budget exhaustion into a pass, or a stopped run becomes
# indistinguishable from a completed one -- the opposite of what this is for.
# Check 3 is that guarantee.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The model burns its tool-call budget on a read, so the run stops on a budget
# with no verify ever having passed -- the exact state the defect lives in.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  tool read_file {"path":"note.txt"}
EOF
printf 'anything\n' > "$ws/note.txt"
printf 'GOAL_REACHED\n' > "$ws/goal.txt"

mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
"lowResource":false,"toolProfile":"full"}
EOF

# A GOAL gate that is already satisfied: the file exists. So the advisory verdict
# must come back green while the run still stops on the budget.
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" --auto \
    --verify "test -s goal.txt" --verify-kind goal \
    --max-tool-calls 2 --journal "$tmp/j.jsonl" \
    -p "read the note" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

# ---- 1. the run really did stop on a budget (the floor) --------------------
if grep -q '"event":"budget"' "$tmp/j.jsonl" 2>/dev/null &&
   grep -q '"outcome":"budget_exhausted"' "$tmp/j.jsonl"; then
    t_ok "the fixture stopped on a tool-call budget, as the defect requires"
else
    t_fail "no budget stop -- checks 2-4 would be measuring nothing: \
$(cut -c1-120 "$tmp/j.jsonl" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2. THE FIX: a verdict is recorded anyway ------------------------------
if grep -q '"phase":"budget_exit_advisory"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "the gate was evaluated for the record at the budget stop"
else
    t_fail "the run stopped with no verify event at all, so the journal cannot \
say whether the work satisfied its own gate -- the defect, three occurrences: \
$(grep -o '\"event\":\"[a-z_]*\"' "$tmp/j.jsonl" 2>/dev/null | tr '\n' ' ' \
| head_bytes 200)"
fi

# ---- 3. and it did NOT change the outcome ---------------------------------
# The load-bearing guarantee. A green advisory verdict on a stopped run must stay
# `budget_exhausted`; anything else makes a stop look like a completion.
if grep -q '"outcome":"budget_exhausted"' "$tmp/j.jsonl" &&
   ! grep -q '"outcome":"ok"' "$tmp/j.jsonl"; then
    t_ok "a green advisory verdict leaves the outcome budget_exhausted"
else
    t_fail "the advisory verdict changed the run's outcome -- a stopped run now \
reads as a completed one"
fi

# ---- 4. the operator is told, not just the journal ------------------------
# "budget_exhausted" plus a passing gate is exactly the case where a reader most
# needs the second sentence.
if grep -q 'verifier PASSES on the tree as it stands' "$tmp/err"; then
    t_ok "the operator is told the gate passes despite the stop"
else
    t_fail "only the journal knows: $(head_bytes 220 "$tmp/err")"
fi

t_done
