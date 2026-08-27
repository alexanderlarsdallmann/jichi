#!/bin/sh
# smoke: learn-on-stop emits a learn_on_stop journal event with non-zero tokens
# on a clean --auto run with a scaffolded learn command (M330).
#
# The scenario: a clean completion with learnOnStop on and a learn command
# scaffolded. The mentor runs after the run's end event and its token cost
# must appear in the journal as a learn_on_stop event.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A `learn` command is what learn-on-stop invokes; without it the feature no-ops
# and the test would be vacuous.
mkdir -p "$ws/.jichi/commands"
# M598: the stand-in declares an `output:` like the real learn.md, so the
# mentor's narrated answer ("ALLDONE") is persisted there by the M79 fallback --
# a draft with no parseable section, which the journal must now say.
cat > "$ws/.jichi/commands/learn.md" <<'EOF'
---
description: draft lessons (test stand-in)
output: .jichi/lessons.draft.md
---
Draft the lessons.
EOF

# The mentor turn runs with zeroed callbacks -- deliberately silent, since its
# artifact is the draft file -- so its own output is not observable. The signal is
# the announcement learn_on_stop() prints before it.
MARK='learn-on-stop: running the mentor'

# A tool call gives the envelope a metering point: budgets are checked around tool
# calls, so a run of one text-only turn can finish without a check ever happening.
# That is why we use a budget that will be hit.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path": "a.txt"}
rule
  text ALLDONE
EOF
printf 'hello\n' > "$ws/a.txt"

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"testCommand":"true","learnOnStop":true'

# A budget the RUN can finish inside, while the mentor's own calls afterwards spend
# more than it -- the whole point being that the mentor's tokens fall outside the
# envelope's accounting, because it runs after the end event.
#
# M441 forced this number up, and the reason is worth keeping: the mock used to
# report usage on a `text` reply and NOT on a `tool` round, so a tool call cost
# literally nothing and `--budget-tokens 1` sufficed. Now that a tool round reports
# usage like a real provider, the same run exhausts the budget at 25 tokens and ends
# `budget_exhausted` -- which is not a regression in jichi but the removal of a
# fiction this driver was resting on. 500 is comfortably above the run's real cost
# (one tool round + one text reply, 25 tokens each at the mock's defaults) and well
# below the mentor's, so both halves of the property still hold.
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session --auto --verify 'true' \
    --budget-tokens 500 --journal "$tmp/j.jsonl" \
    -p 'say ALLDONE' < /dev/null > "$tmp/out" 2>"$tmp/err")

# The run should complete (the mentor runs after the envelope's end event)
if grep -q '"outcome":"ok"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "run completed cleanly (outcome ok)"
else
    t_fail "run did not complete: $(grep -o '"outcome":"[a-z_]*"' "$tmp/j.jsonl" | tail -1)"
fi

# The mentor must have run
if grep -q "$MARK" "$tmp/err" 2>/dev/null; then
    t_ok "learn-on-stop RAN"
else
    t_fail "mentor did not run: $(tail -c 200 "$tmp/err")"
fi

# The journal must contain a learn_on_stop event with non-zero tokens
if grep -q '"event":"learn_on_stop"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "journal contains learn_on_stop event"
else
    t_fail "journal missing learn_on_stop event"
fi

# The learn_on_stop event must have non-zero tokens
TOKENS=$(grep '"event":"learn_on_stop"' "$tmp/j.jsonl" | sed -n 's/.*"tokens":\([0-9]*\).*/\1/p')
if [ -n "$TOKENS" ] && [ "$TOKENS" -gt 0 ]; then
    t_ok "learn_on_stop event has non-zero tokens ($TOKENS)"
else
    t_fail "learn_on_stop event has zero or missing tokens: $TOKENS"
fi

# 5 (M598): the event says what the draft would commit. The narrated "ALLDONE"
# landed in the output file and parses to nothing, so `draft_parsed_nothing`
# must be 1 -- a supervisor reading the journal learns that this mentor run
# produced no lesson without opening the file.
if grep '"event":"learn_on_stop"' "$tmp/j.jsonl" | grep -q '"draft_parsed_nothing":1'; then
    t_ok "learn_on_stop event reports the draft would apply nothing (draft_parsed_nothing=1)"
else
    t_fail "learn_on_stop event lacks draft_parsed_nothing: $(grep '"event":"learn_on_stop"' "$tmp/j.jsonl" | head -n 1 | cut -c 1-300)"
fi

t_done
