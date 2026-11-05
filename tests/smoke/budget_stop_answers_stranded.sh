#!/bin/sh
# smoke: a budget stop mid-round answers the unrun calls, so resume is wire-valid (M919, R01-F1).
#
# THE DEFECT (whole-tree review 2026-10-08, R01-agent-core.md F1; verified
# there by exp1): the budget check runs before EVERY tool call of a round and
# returned immediately -- so a round whose model asked for c1, c2, c3 with a
# two-call budget answered c1 and c2, then stopped, STRANDING c3: an assistant
# message carrying a tool call with no result. The review's exp1 measured the
# consequence: `--continue` printed
#     [history] wire-shape check: 1 violation(s) ... unanswered tool call 'c3'
# and the captured request carried all three call ids with results for two
# only -- a request OpenAI-compatible and Anthropic endpoints reject with
# HTTP 400, so the "kept for you to resume" run died at its first model call.
#
# THE FIX: before returning from that mid-round stop, append an ERROR tool
# result for every remaining call of the round ("not run: this run's
# <kind> budget stopped before this call..."). An error, not a success: the
# tool was asked for and refused to run, so the result must say it did not
# happen. The round-top budget check exits with no calls in flight, so only
# the mid-round path strands.
#
# WHAT IT HOLDS, each against the effect:
#   1. the rig is real: one round, three read_file calls, a two-call
#      tool-call budget -- the run stops on that budget mid-round.
#   2. THE FIX: resuming the saved session prints no wire-shape violation
#      (pre-fix: "1 violation(s) ... unanswered tool call").
#   3. the stranded call was answered, and as an ERROR that names the
#      budget (pre-fix: nothing answers it).
#   4. the RESUME REQUEST sent to the provider carries the repaired result
#      (the mock accepts any body, so the provider-side 400 the review noted
#      is not measurable here; this asserts the shape the 400 was about: the
#      resumed message history, as built, no longer has an unanswered call).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# ONE rule: every request answers with three read_file calls in a single
# round (the mock emits them as one tool_calls array). The budget of two
# lets c1 and c2 run and stops before c3 -- mid-round, the defect's shape.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  tool read_file {"path":"a.txt"}
  tool read_file {"path":"b.txt"}
  tool read_file {"path":"c.txt"}
EOF
printf 'anything\n' > "$ws/a.txt"
printf 'anything\n' > "$ws/b.txt"
printf 'anything\n' > "$ws/c.txt"

mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
"lowResource":false,"toolProfile":"full"}
EOF

# Run 1: the budget stop, mid-round. Saves its session (NO --no-session).
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --max-tool-calls 2 --journal "$tmp/j.jsonl" \
    -p "read the three files" < /dev/null > "$tmp/out1" 2> "$tmp/err1") || true

# The session run 1 saved: the newest .json in the smoke HOME's session dir.
sess=$(ls -1t "$HOME/.jichi.d/sessions/"*.json 2>/dev/null | head -1)

# Run 2: resume that session. Pre-fix the history check trips on the stranded
# call and the first resumed request carries an unanswered call id.
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --session "$(basename "$sess" .json)" --max-tool-calls 3 \
    -p "finish" < /dev/null > "$tmp/out2" 2> "$tmp/err2") || true
mm_stop

# 1: the floor -- the run really did stop on the tool-call budget mid-round
if grep -q 'budget' "$tmp/j.jsonl" 2>/dev/null &&
   grep -q '"outcome":"budget_exhausted"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "one round, three calls, a two-call budget: stopped mid-round on it"
else
    t_fail "no mid-round budget stop -- the rig is not measuring the defect: \
$(grep -o '\"event\":\"[a-z_]*\"' "$tmp/j.jsonl" 2>/dev/null | tr '\n' ' ' | head_bytes 160)"
fi

# 2: THE FIX -- the resume's history is wire-valid
if [ -n "$sess" ] && ! grep -q 'wire-shape check' "$tmp/err2"; then
    t_ok "resuming the stopped session prints no wire-shape violation"
else
    t_fail "the saved session resumed into a malformed history (sess='$sess'): \
$(grep -A1 'wire-shape' "$tmp/err2" 2>/dev/null | head_bytes 200)"
fi

# 3: the stranded call was answered, as an error, naming the budget
if [ -n "$sess" ] && grep -q 'not run' "$sess" 2>/dev/null && \
   grep -q 'tool_calls budget stopped' "$sess" 2>/dev/null; then
    t_ok "the unrun call carries an error result naming the budget that stopped it"
else
    t_fail "no error result for the unrun call (session '$sess'): \
$(grep -o 'not run[^\"]*' "$sess" 2>/dev/null | head -1 | head_bytes 120)"
fi

# 4: the resumed request, as built for the provider, carries the repaired
# result -- the wire shape the 400 was about, measured on the bytes sent
if [ -n "$sess" ] && grep -q 'not run' "$tmp/cap"/req.* 2>/dev/null; then
    t_ok "the resume request sent to the provider carries the repaired result"
else
    t_fail "the resumed request has no repaired result (cap=$(ls "$tmp/cap"/req.* 2>/dev/null | wc -l) request(s)): \
$(head_bytes 160 "$tmp/out2" 2>/dev/null)"
fi

t_done
