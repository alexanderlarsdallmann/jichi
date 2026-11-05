#!/bin/sh
# smoke: the relay's account call writes the model's part into the baton (M921,
# sub-slice C; proposal docs/proposals/2026-10-context-relay.md, section 6.5).
#
# THE RIG: the same E3 exhaustion rig as sub-slice B (usage 1000 against
# contextLength 1000, a tool-only default rule). ONE rule sits FIRST and
# matches "[relay]" -- the account instruction the loop appends at the
# round-boundary handover:
#   run 1: the [relay] request gets a TEXT reply -> section 3 of the baton
#          holds that text, the stop stays "handover", the run's answer is
#          untouched (the account does not leak), and under a low
#          relayNotesBytes the finished note is cut and stays at or under
#          the cap.
#   run 2: the [relay] request gets a 400 -> the handover still stands
#          (no crash, no stop_reason change) and the note's section 3 is
#          honest: it says the model's part was not obtained (this rig's leg
#          has no prior text answer to quote, so the placeholder is the
#          correct labelled outcome).
#
# CHECKS (teeth per check):
#   1. section 3 holds the mock's account text (not the placeholder).
#   2. the account call leaked nothing: stop_reason is still "handover" and
#      the JSON answer does not contain the account instruction.
#   3. the cap formula: with relayNotesBytes 1024 the finished note is
#      <= 1024 bytes AND carries the cut marker (the account provably
#      exceeded its room, so the cut path ran).
#   4. a 400 on the [relay] request: the handover still stands and the note
#      exists with a labelled placeholder section 3 (the mock was asked --
#      the capture shows a [relay] request -- and refused).
#
#   run 3 (R01-N1, M1126): the same n1 shape with an EARLIER text answer in
#      the history -- run 3a answers one line of text (usage 500, no E3);
#      run 3b --continue on the same HOME, E3 fires (usage 1000), the
#      [relay] account request is refused 400, and the fallback quotes
#      3a's answer. In its OWN fresh HOME (a shared one would collide with
#      runs 1/2's sessions).
#   5. the refused account call with an earlier text answer: rc 0,
#      stop_reason handover, the baton exists and its section 3 carries the
#      fallback's label ("account call failed") AND the quoted answer. Red
#      on the unfixed tree: rc 139 (the wild-pointer write) and no baton.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
echo hello > "$ws/a.txt"
# _smoke.sh does NOT set JQ globally; every driver that uses jsonq sets
# it itself (the M920 gotcha).
JQ="$SMOKE_TOOLS/jsonq"

store_on="$HOME/.jichi.d/relay"
stop_of() { "$JQ" '.stop_reason' "$1" 2>/dev/null; }
# A single-line account. It must stay under mockmodel's 1024-byte parse line
# (tests/tools/mm_core.c) AND exceed the room the cap formula leaves under
# the 1024 cap (1024 - ~956 finished-with-placeholder - 48 + 102 = ~124), so
# the renderer cuts it.
LONG_ACCOUNT="Done: wrote the baton writer and its driver, both green. In progress: nothing. Next step: the next leg reads this note and continues. Decided: the task source is the history, not the prompt parameter, because the parameter is dangling after the turn resets the scratch. Dead end: none -- the account call must be silent, so its sinks are all NULL."

# --- run 1: the account is obtained (the [relay] request gets a TEXT reply) --
cat > "$tmp/acc.mm" <<EOF
wire openai
rule
  match "[relay]"
  text ${LONG_ACCOUNT}
rule
  match "messages"
  usage 1000 5
  tool read_file {"path":"a.txt"}
EOF
mm_start "$tmp/acc.mm" "$tmp/cap1"
write_config "$tmp/config_on.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true,"relayNotesBytes":1024' '"contextLength":1000'
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_on.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/on.json" 2> "$tmp/on.err")
mm_stop
fb_id_on=$("$JQ" '.session_id' "$tmp/on.json" 2>/dev/null)
n_on="$store_on/$fb_id_on/leg-01.md"
stop_on=$(stop_of "$tmp/on.json")

# 1: the tooth -- section 3 holds the account's text, not the placeholder.
if [ -n "$n_on" ] && grep -q "the baton writer and its driver" "$n_on" 2>/dev/null \
   && ! grep -q "no account: the model" "$n_on"; then
    t_ok "section 3 holds the model's account text"
else
    t_fail "section 3 did not hold the account: stop_on='$stop_on', note='$n_on'; \
placeholder present: $(grep -c 'no account' "$n_on" 2>/dev/null)"
fi

# 2: the account call leaked nothing into the run's result.
if [ "$stop_on" = "handover" ] && \
   ! "$JQ" '.text' "$tmp/on.json" 2>/dev/null | grep -q "This session is handing over"; then
    t_ok "the account call changed nothing about the stop reason or the answer"
else
    t_fail "the account call leaked: stop_on='$stop_on', \
answer=$( "$JQ" '.text' "$tmp/on.json" 2>/dev/null | head_bytes 80)"
fi

# 3: the cap formula -- under relayNotesBytes 1024 the finished note is
# at or under 1024 AND carries the cut marker (the account provably
# exceeded its room).
cap=1024
sz=$(wc -c < "$n_on" 2>/dev/null | tr -d ' ')
if [ -n "$n_on" ] && [ -n "$sz" ] && [ "$sz" -le "$cap" ] && \
   grep -q "account cut:" "$n_on"; then
    t_ok "the finished note is within relayNotesBytes ($sz <= $cap) and the account was cut"
else
    t_fail "cap not proven: size='$sz' cap=$cap; marker present: $(grep -c 'account cut' "$n_on" 2>/dev/null)"
fi

# --- run 2: the [relay] request is refused (400) -> the handover stands --
cat > "$tmp/fb.mm" <<EOF
wire openai
rule
  match "[relay]"
  status 400
rule
  match "messages"
  usage 1000 5
  tool read_file {"path":"a.txt"}
EOF
mm_start "$tmp/fb.mm" "$tmp/cap2"
write_config "$tmp/config_fb.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true,"relayNotesBytes":1024' '"contextLength":1000'
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_fb.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/fb.json" 2> "$tmp/fb.err")
mm_stop
fb_id=$("$JQ" '.session_id' "$tmp/fb.json" 2>/dev/null)
n_fb="$store_on/$fb_id/leg-01.md"
stop_fb=$(stop_of "$tmp/fb.json")

# 4: a refused account call must not break the handover, must not crash, and
# the note's section 3 must be honest (the labelled placeholder -- this rig's
# leg has no prior text answer to quote). The capture proves the [relay]
# request WAS made (the mock refused it, so the code path ran).
if [ "$stop_fb" = "handover" ] && [ -n "$n_fb" ] && \
   grep -q "no account: the model" "$n_fb" && \
   [ -n "$(grep -l '\[relay\]' "$tmp/cap2"/req.* 2>/dev/null)" ]; then
    t_ok "a 400 on the account call leaves an honest handover (placeholder section 3, request was made)"
else
    t_fail "fallback wrong: stop_fb='$stop_fb', note='$n_fb'; \
[relay] requests in capture: $(grep -l '\[relay\]' "$tmp/cap2"/req.* 2>/dev/null | wc -l | tr -d ' ')"
fi

# --- run 3 (R01-N1, M1126): the n1 shape -- an earlier text answer, then a
# 400 on the [relay] request -> the fallback quotes run 3a's answer -------
# Its OWN fresh HOME (relay_topo_b's per-run pattern: HOME exported, because
# with_deadline is a shell function `env HOME=` cannot execute), so the two
# runs' sessions never touch runs 1/2's store, and its own workspace file.
home3=$(smoke_tmp)
ws3=$(smoke_tmp)
chmod 700 "$home3" 2>/dev/null || true
echo hello > "$ws3/a.txt"

# 3a: one line of text, usage 500 against the 1000 window (no E3) -> a plain
# answer that 3b's fallback will quote.
cat > "$tmp/leg3a.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 500 5
  text "leg one answer for the fallback check"
EOF
mm_start "$tmp/leg3a.mm" "$tmp/cap3a"
write_config "$tmp/config3a.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true' '"contextLength":1000'
HOME="$home3"
export HOME
(cd "$ws3" && with_deadline 90 "$BIN" --config "$tmp/config3a.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/leg3a.json" 2> "$tmp/leg3a.err"); rc3a=$?
mm_stop

# 3b: same HOME, --continue: E3 fires (server usage 1000 >= 90% of 1000) at
# the round boundary, the handover asks the account, and the [relay] request
# (FIRST rule: first-match-wins) is refused 400 -> the fallback quotes 3a's
# last real answer.
cat > "$tmp/leg3b.mm" <<'EOF'
wire openai
rule
  match "[relay]"
  status 400
rule
  match "messages"
  usage 1000 5
  tool read_file {"path":"a.txt"}
EOF
mm_start "$tmp/leg3b.mm" "$tmp/cap3b"
write_config "$tmp/config3b.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true' '"contextLength":1000'
(cd "$ws3" && with_deadline 90 "$BIN" --config "$tmp/config3b.json" \
    --continue --output json -p "next step" \
    < /dev/null > "$tmp/leg3b.json" 2> "$tmp/leg3b.err"); rc3b=$?
mm_stop
id3b=$("$JQ" '.session_id' "$tmp/leg3b.json" 2>/dev/null)
n3b="$home3/.jichi.d/relay/$id3b/leg-01.md"
stop3b=$(stop_of "$tmp/leg3b.json")

# 5: the refused account call WITH an earlier text answer must not crash and
# must land a baton whose section 3 quotes the answer under the fallback's
# label. Red on the unfixed tree: rc 139 (the wild-pointer write in the
# fallback) and no baton at all (the leg's turn is lost).
if [ "$rc3b" -eq 0 ] && [ "$stop3b" = "handover" ] && [ -f "$n3b" ] && \
   grep -q "account call failed" "$n3b" && \
   grep -q "leg one answer for the fallback check" "$n3b"; then
    t_ok "a 400 on the account call with an earlier text answer lands a baton that quotes it (the fallback copy is terminated)"
else
    t_fail "R01-N1 fallback: rc3a=$rc3a rc3b=$rc3b stop3b='$stop3b', note='$n3b' exists=$([ -f "$n3b" ] && echo yes || echo no); \
err3b: $(tail -c 200 "$tmp/leg3b.err" 2>/dev/null | tr '\n' ' ')"
fi

t_done
