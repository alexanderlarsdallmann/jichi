#!/bin/sh
# smoke: /predict -- record what you expect BEFORE you look, resolve it after,
# read the tally; a learner-owned sink that never reads as an attempt (M635).
#
# THE GAP. Task 73 was the one calibration exercise (estimate vs actual). The
# code-reading skill asks the learner to predict before it reveals -- and then
# the prediction was gone. Now `/predict <text>` appends to
# .jichi/predictions.jsonl, `/predict right|wrong` resolves the last open one,
# `/predict` alone prints the tally. Its own file, for the reason hints.jsonl
# is its own file: every reader of progress.jsonl treats a line as AN ATTEMPT
# WITH A VERDICT, and the course's promise -- a self-learner is never punished
# for learning -- has to be true by construction, not by everyone remembering
# not to score it.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text never asked
EOF
cat > "$tmp/run.pd" <<'EOF'
delay 1500
send "/predict\r"
delay 600
send "/predict wrong\r"
delay 600
send "/predict the CRLF test will fail before the fix\r"
delay 600
send "/predict right\r"
delay 600
send "/predict\r"
delay 800
send "/exit\r"
waitexit 15
assertexit 0
EOF
mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
    --deadline 60 --cols 120 --log "$tmp/tui.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session \
    > /dev/null 2>&1); rc=$?
mm_stop

# --- 1: an empty tally says so, and how to start -----------------------------------
if grep -q "Predictions: none yet" "$tmp/tui.log"; then
    t_ok "/predict with nothing recorded says none yet and how to make one"
else
    t_fail "rc=$rc: $(head_bytes 300 "$tmp/tui.log")"
fi
# --- 2: resolving with nothing open is refused, not invented -------------------------
if grep -q "no open prediction to resolve" "$tmp/tui.log"; then
    t_ok "/predict wrong with nothing open is refused (no invented prediction)"
else
    t_fail "resolve-with-nothing-open not refused"
fi
# --- 3: the prediction is recorded, verbatim, in its own file ---------------------------
if [ -f "$ws/.jichi/predictions.jsonl" ] && \
   grep -q '"kind":"predict".*"text":"the CRLF test will fail before the fix"' "$ws/.jichi/predictions.jsonl"; then
    t_ok "the prediction landed in .jichi/predictions.jsonl with its text"
else
    t_fail "prediction not recorded: $(cat "$ws/.jichi/predictions.jsonl" 2>/dev/null | head_bytes 200)"
fi
# --- 4: the resolution is a second line, right:true ---------------------------------------
if grep -q '"kind":"resolve".*"right":true' "$ws/.jichi/predictions.jsonl"; then
    t_ok "/predict right appended a resolve line for the open prediction"
else
    t_fail "no resolve line: $(cat "$ws/.jichi/predictions.jsonl" | head_bytes 200)"
fi
# --- 5: the tally folds both ------------------------------------------------------------------
if grep -q "Predictions: 1 made, 1 resolved, 1 right (hit rate 100%), 0 open" "$tmp/tui.log"; then
    t_ok "the tally reads 1 made, 1 resolved, 1 right, hit rate 100%, 0 open"
else
    t_fail "tally wrong: $(grep "Predictions:" "$tmp/tui.log" | tail -1 | head_bytes 200)"
fi
# --- 6: THE SEPARATION -- nothing reached the attempt or hint records ----------------------------
if [ ! -f "$ws/.jichi/progress.jsonl" ] && [ ! -f "$ws/.jichi/hints.jsonl" ]; then
    t_ok "no progress.jsonl and no hints.jsonl were written: a prediction is not an attempt"
else
    t_fail "a prediction leaked into the graded record: $(ls "$ws/.jichi")"
fi
t_done
