#!/bin/sh
# smoke: learn-on-stop fires on a COMPLETED --auto run and not on one that was cut
# short (M328).
#
# The bug: the gate was `st == JC_OK`, and run_agent_loop returns JC_OK for every
# terminal state -- the envelope's `outcome` is what carries budget exhaustion, and
# the process exit code derives from it, not from `st`. So the mentor ran after a
# run that had just hit its token budget, and its turn spent tokens OUTSIDE the
# envelope's accounting: measured once at 625k past a 1m budget, 61% over, and
# invisible in the journal because its `end` event was already written.
#
# Both directions are checked, because a gate that never fires would also pass a
# one-sided "did not run after a budget stop" test.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A `learn` command is what learn-on-stop invokes; without it the feature no-ops
# and the test would be vacuous.
mkdir -p "$ws/.jichi/commands"
cat > "$ws/.jichi/commands/learn.md" <<'EOF'
---
description: draft lessons (test stand-in)
---
Draft the lessons.
EOF

# The mentor turn runs with zeroed callbacks -- deliberately silent, since its
# artifact is the draft file -- so its own output is not observable. The signal is
# the announcement learn_on_stop() prints before it.
MARK='learn-on-stop: running the mentor'

# A tool call gives the envelope a metering point: budgets are checked around tool
# calls, so a run of one text-only turn can finish without a check ever happening.
# That is why the budget arm below is not simply "one call with a tiny budget".
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path": "a.txt"}
rule
  count 2
  tool read_file {"path": "a.txt"}
rule
  text ALLDONE
EOF
printf 'hello\n' > "$ws/a.txt"

# --- 1. a clean completion: the mentor RUNS -------------------------------
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"testCommand":"true","learnOnStop":true'

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session --auto --verify 'true' \
    --budget-tokens 900k --journal "$tmp/j1.jsonl" \
    -p 'say ALLDONE' < /dev/null > "$tmp/out1" 2>"$tmp/err1")

if grep -q '"outcome":"ok"' "$tmp/j1.jsonl" 2>/dev/null; then
    t_ok "control run completed cleanly (outcome ok)"
else
    t_fail "control run did not complete: $(grep -o '"outcome":"[a-z_]*"' "$tmp/j1.jsonl" | tail -1)"
fi
if grep -q "$MARK" "$tmp/err1" 2>/dev/null; then
    t_ok "learn-on-stop RAN on a clean completion"
else
    t_fail "mentor did not run on a clean completion -- check 4 would pass vacuously: $(tail -c 200 "$tmp/err1")"
fi

# --- 2. a budget stop: the mentor must NOT run ----------------------------
# A TOOL-CALL budget rather than a token one: `--budget-tokens 1` did not stop this
# run at all (it finished at 50 metered tokens and reported outcome ok), whereas
# --max-tool-calls is exact and needs no assumption about the mock's usage numbers.
# Any budget kind yields JC_ENV_BUDGET_EXHAUSTED, which is what the gate reads.
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config2.json" "$MM_PORT" '"testCommand":"true","learnOnStop":true'

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    --no-session --auto --verify 'true' \
    --max-tool-calls 1 --journal "$tmp/j2.jsonl" \
    -p 'say ALLDONE' < /dev/null > "$tmp/out2" 2>"$tmp/err2")

if grep -q '"outcome":"budget_exhausted"' "$tmp/j2.jsonl" 2>/dev/null; then
    t_ok "second run stopped on budget"
else
    t_fail "second run did not hit the budget: $(grep -o '"outcome":"[a-z_]*"' "$tmp/j2.jsonl" | tail -1)"
fi
if ! grep -q "$MARK" "$tmp/err2" 2>/dev/null; then
    t_ok "learn-on-stop did NOT run after a budget stop"
else
    t_fail "mentor ran after a budget stop -- its tokens are outside the envelope"
fi
if grep -q 'learn-on-stop skipped' "$tmp/err2" 2>/dev/null; then
    t_ok "and said why it skipped"
else
    t_fail "skipped silently, which reads as a broken feature: $(tail -c 200 "$tmp/err2")"
fi

t_done
