#!/bin/sh
# smoke: a turn reports ITS OWN answer, and the journal records what a run was
# and what it answered (M715).
#
# THE DEFECT, found while designing the journal fields plan D6 asks for. The
# structured outputs -- `--output jsonl`'s `done` event and `--output json`'s
# object -- took their `text` from the last non-empty assistant message in the
# WHOLE session history. So in a resumed session (`-c`, `--session`) a turn
# that answered nothing -- capped at the iteration limit after tool calls, say --
# reported the PREVIOUS turn's answer as its own. Reproduced with mockmodel:
# turn 1 answers OLD_ANSWER_MARK, turn 2 is capped with two tool calls and no
# text, and turn 2's done event said `"text":"OLD_ANSWER_MARK"`. Plain-text
# stdout was right (empty): it streams only this turn's text. The structured
# form -- the one a script trusts -- was wrong.
#
# THE FIELDS (plan D6). DEFERRED item 7 asks whether a capped ONE-SHOT usually
# answers anything, and the journal could not say either half: nothing marked a
# run with no session to resume, and nothing recorded what the run answered.
# `one_shot` (start) and `answer_bytes` (end) are facts, not judgements -- a
# capped run's last words ("Let me try with explicit tabs:") are text but not
# an answer, so the byte count is recorded and the reading is left to the
# measurement (tests/measure/capped_oneshot.py).
#
# EVERY CHANGED CALL SITE HAS A CHECK. Four places read "the answer" from a
# multi-turn history, and each is reverted separately to prove its own check:
# the jsonl done event and the json object (checks 2, 3 -- one line in main.c),
# the journal's end event (check 4 -- the boundary in jc_agent_run_turn), and the
# M73 overflow hint (checks 9, 10), which in a resumed session repeated an
# EARLIER turn's overflow hint under a turn that had produced no text at all.
#
# UNDER --auto THE CAP IS 200, NOT maxToolIters. An armed envelope raises the
# iteration cap to at least 200 (jc_agent_run_turn: "the declared budgets are the
# real bound"), so the --auto runs below that answer nothing make 200 requests to
# mockmodel before they stop -- measured: 200 requests, 200 tool calls, then
# `"outcome":"ok","stop_reason":"max_iters"`. That is plan D1's case in miniature.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --- part 1: a session whose second and third turns are capped with no text ----
cat > "$tmp/stale.mm" <<'EOF'
wire openai
rule
  count 1
  text OLD_ANSWER_MARK
rule
  tool list_files {"path":"."}
EOF
mm_start "$tmp/stale.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT" '"maxToolIters":2'

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q \
    --output jsonl -p "first question" < /dev/null) > "$tmp/t1.jsonl" 2>/dev/null
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q \
    --output jsonl -c -p "second question" < /dev/null) > "$tmp/t2.jsonl" 2>/dev/null
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q \
    --output json -c -p "third question" < /dev/null) > "$tmp/t3.json" 2>/dev/null
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q \
    --auto --journal "$tmp/t4.jsonl" -c -p "fourth question" < /dev/null) \
    > /dev/null 2>&1
mm_stop

d1=$(grep '"type":"done"' "$tmp/t1.jsonl" 2>/dev/null | head -1)
d2=$(grep '"type":"done"' "$tmp/t2.jsonl" 2>/dev/null | head -1)

# --- 1: the denominator -- turn 1 answered, turn 2 was capped with no text -----
if [ "$(printf '%s' "$d1" | "$JQ" .text 2>/dev/null)" = "OLD_ANSWER_MARK" ] &&
   [ "$(printf '%s' "$d2" | "$JQ" .stop_reason 2>/dev/null)" = "max_iters" ]; then
    t_ok "turn 1 answered and turn 2 was capped (the case is live)"
else
    t_fail "fixture did not produce the case: d1=$(printf '%s' "$d1" | head_bytes 120) d2=$(printf '%s' "$d2" | head_bytes 120)"
fi

# --- 2: jsonl -- a capped turn does not report the previous turn's answer ------
if ! printf '%s' "$d2" | grep -q 'OLD_ANSWER_MARK'; then
    t_ok "jsonl: turn 2's done event does not carry turn 1's answer"
else
    t_fail "jsonl: turn 2's done event reports turn 1's answer as its own: $(printf '%s' "$d2" | head_bytes 160)"
fi

# --- 3: json -- the same, for the one-object form -----------------------------
if [ -s "$tmp/t3.json" ] && ! grep -q 'OLD_ANSWER_MARK' "$tmp/t3.json"; then
    t_ok "json: turn 3's result does not carry turn 1's answer"
else
    t_fail "json: turn 3 reports turn 1's answer (or produced nothing): $(head_bytes 160 "$tmp/t3.json")"
fi

# --- 4: the journal -- a resumed capped turn's answer_bytes is its own ---------
t4e=$(grep '"event":"end"' "$tmp/t4.jsonl" 2>/dev/null | head -1)
if [ "$(printf '%s' "$t4e" | "$JQ" .stop_reason 2>/dev/null)" = "max_iters" ] &&
   [ "$(printf '%s' "$t4e" | "$JQ" .answer_bytes 2>/dev/null)" = "0" ]; then
    t_ok "journal: a resumed capped turn records answer_bytes 0, not turn 1's 15"
else
    t_fail "journal: resumed capped turn's end event is not max_iters/answer_bytes 0: $(printf '%s' "$t4e" | head_bytes 240)"
fi

# --- part 2: the journal, for a one-shot and for a resumable run ---------------
cat > "$tmp/answer.mm" <<'EOF'
wire openai
rule
  text ANSWER_12345
EOF
# Only tool calls, so the one-shot runs into the envelope's 200-iteration floor.
# (The first draft reused stale.mm, whose FIRST reply is text: the "capped" run
# simply answered, and its check went red for the fixture's reason rather than
# the product's. The second set maxToolIters 2, which --auto overrides.)
cat > "$tmp/loop.mm" <<'EOF'
wire openai
rule
  tool list_files {"path":"."}
EOF
mm_start "$tmp/loop.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" -q --no-session \
    --auto --journal "$tmp/capped.jsonl" -p "loop" < /dev/null) > /dev/null 2>&1
mm_stop
mm_start "$tmp/answer.mm" "$tmp/cap3"
write_config "$tmp/config3.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" -q \
    --auto --journal "$tmp/kept.jsonl" -p "answer me" < /dev/null) > /dev/null 2>&1
mm_stop

cs=$(grep '"event":"start"' "$tmp/capped.jsonl" 2>/dev/null | head -1)
ce=$(grep '"event":"end"' "$tmp/capped.jsonl" 2>/dev/null | head -1)
ks=$(grep '"event":"start"' "$tmp/kept.jsonl" 2>/dev/null | head -1)
ke=$(grep '"event":"end"' "$tmp/kept.jsonl" 2>/dev/null | head -1)

# --- 5: a --no-session headless run is a one-shot ------------------------------
if [ "$(printf '%s' "$cs" | "$JQ" .one_shot 2>/dev/null)" = "true" ]; then
    t_ok "a --no-session headless run is journalled one_shot:true"
else
    t_fail "no one_shot:true on the one-shot's start event: $(printf '%s' "$cs" | head_bytes 200)"
fi

# --- 6: ...and a capped one that answered nothing records 0 bytes --------------
if [ "$(printf '%s' "$ce" | "$JQ" .stop_reason 2>/dev/null)" = "max_iters" ] &&
   [ "$(printf '%s' "$ce" | "$JQ" .answer_bytes 2>/dev/null)" = "0" ]; then
    t_ok "the capped one-shot records stop_reason max_iters and answer_bytes 0"
else
    t_fail "capped one-shot end event lacks max_iters / answer_bytes 0: $(printf '%s' "$ce" | head_bytes 240)"
fi

# --- 7: a run with a session to resume is NOT a one-shot -----------------------
if [ "$(printf '%s' "$ks" | "$JQ" .one_shot 2>/dev/null)" = "false" ]; then
    t_ok "a run that keeps its session is journalled one_shot:false"
else
    t_fail "a resumable run is not marked one_shot:false: $(printf '%s' "$ks" | head_bytes 200)"
fi

# --- 8: ...and an answer's size is its size ------------------------------------
if [ "$(printf '%s' "$ke" | "$JQ" .answer_bytes 2>/dev/null)" = "12" ]; then
    t_ok "an answering run records answer_bytes 12 for ANSWER_12345"
else
    t_fail "answer_bytes is not 12 for a 12-byte answer: $(printf '%s' "$ke" | head_bytes 240)"
fi

# --- part 3: the M73 overflow hint belongs to the turn that got the overflow ----
# Not quiet: the hint is suppressed by -q. A fresh workspace, so -c resumes THIS
# session whichever way the session store scopes "most recent".
ws2=$(smoke_tmp)
cat > "$tmp/overflow.mm" <<'EOF'
wire openai
rule
  count 1
  text upstream: the request exceeds n_ctx for this model
rule
  tool list_files {"path":"."}
EOF
mm_start "$tmp/overflow.mm" "$tmp/cap4"
write_config "$tmp/config4.json" "$MM_PORT" '"maxToolIters":2'
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config4.json" \
    --output jsonl -p "big question" < /dev/null) > "$tmp/o1.jsonl" 2> "$tmp/o1.err"
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config4.json" \
    --output jsonl -c -p "follow-up" < /dev/null) > "$tmp/o2.jsonl" 2> "$tmp/o2.err"
mm_stop
HINT='context-window overflow'

# --- 9: the instrument can fire -- the overflow turn gets the hint -------------
if grep -q "$HINT" "$tmp/o1.err"; then
    t_ok "the turn whose answer is an overflow message prints the M73 hint"
else
    t_fail "no overflow hint for an n_ctx answer (the check below would be vacuous): $(head_bytes 200 "$tmp/o1.err")"
fi

# --- 10: ...and the capped turn after it does not repeat it --------------------
o2=$(grep '"type":"done"' "$tmp/o2.jsonl" 2>/dev/null | head -1)
if [ "$(printf '%s' "$o2" | "$JQ" .stop_reason 2>/dev/null)" = "max_iters" ] &&
   ! grep -q "$HINT" "$tmp/o2.err"; then
    t_ok "a capped turn with no text does not repeat the previous turn's overflow hint"
else
    t_fail "the follow-up was not capped, or repeated turn 1's hint: $(printf '%s' "$o2" | head_bytes 120) / $(head_bytes 200 "$tmp/o2.err")"
fi

t_done
