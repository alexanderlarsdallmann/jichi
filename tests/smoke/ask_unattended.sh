#!/bin/sh
# smoke: the unanswered question is part of the run's record (M359) -- the
# dual of M161's steered=N. When ask_user runs unattended (headless --auto:
# no app->ask delegate), the MODEL is told honestly that nobody answered
# (that text predates M359 and is pinned here through the wire), and the
# bounded run's journal now gains an `ask` event with answered:false, which
# `runs` renders as unanswered=N and `runs --output json` carries -- so a
# reviewer learns the task was under-specified without replaying the
# transcript.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

{
    echo "wire openai"
    echo "rule"
    echo "  count 1"
    echo '  tool ask_user {"question":"Overwrite the existing export file?"}'
    echo "rule"
    echo "  text done"
} > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --budget-tokens 100k --dump-requests "$tmp/reqs" \
    -p "export the data" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

journal=$(ls "$HOME/.jichi.d/runs"/*.jsonl 2>/dev/null | head -1)

# --- 1: the bounded run wrote a journal --------------------------------------
if [ -n "$journal" ] && [ -s "$journal" ]; then
    t_ok "the bounded run wrote a journal"
else
    t_fail "no journal under \$HOME/.jichi.d/runs -- nothing below can be trusted"
fi

# --- 2: the ask event, unanswered, with the question -------------------------
if grep '"event":"ask"' "$journal" 2>/dev/null \
   | grep '"answered":false' | grep -q 'Overwrite the existing export'; then
    t_ok "journal carries the ask event: answered:false + the question text"
else
    t_fail "no unanswered ask event: $(grep -c '"event":"ask"' "$journal" 2>/dev/null) ask event(s)"
fi

# --- 3: the model was told honestly (pinned through the wire) ----------------
last=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | tail -1)
if [ -n "$last" ] && grep -q 'No interactive user is available' "$last"; then
    t_ok "the tool result told the model nobody answered"
else
    t_fail "the honest no-answer text never reached the wire"
fi

# --- 4: `runs` renders the count ----------------------------------------------
runsout=$("$BIN" runs 2>/dev/null)
if printf '%s' "$runsout" | grep -q 'unanswered=1'; then
    t_ok "runs shows unanswered=1"
else
    t_fail "runs output lacks unanswered=1: $(printf '%s' "$runsout" | head -2)"
fi

# --- 5: and the JSON projection carries it ------------------------------------
runsjson=$("$BIN" runs --output json 2>/dev/null)
if printf '%s' "$runsjson" | grep -q '"unanswered":[[:space:]]*1'; then
    t_ok "runs --output json carries unanswered:1"
else
    t_fail "json projection lacks unanswered: $(printf '%s' "$runsjson" | head_bytes 200)"
fi

# --- 6: the run itself completed (the no-op never hangs an unattended run) ---
if grep -q 'done' "$tmp/out"; then
    t_ok "the unattended run completed despite the question"
else
    t_fail "the run did not finish: $(head_bytes 150 "$tmp/err")"
fi

t_done
