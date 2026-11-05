#!/bin/sh
# smoke: the next leg starts from the baton -- `--handoff-in` (M921,
# sub-slice D; proposal docs/proposals/2026-10-context-relay.md, section 6.7).
#
# THE RIG: two processes (topology A), one shared isolated HOME.
#   run 1: the E3 exhaustion rig of sub-slices B/C (usage 1000 against
#          contextLength 1000, relay ON) -> hands over and writes
#          ~/.jichi.d/relay/<id>/leg-01.md. The [relay] account rule (FIRST,
#          mockmodel is first-match-wins) makes section 3 hold a note that
#          deliberately carries the prohibition "never run tests".
#   run 2: a FRESH session started with `--handoff-in <that note>` plus its
#          OWN next-leg task. relay is OFF (the flag does not imply it), so
#          run 2 makes exactly one model call and answers text. The refusals
#          below run while run 2's mock is ALIVE: any model call a refused
#          run would leak lands in cap2 and fails the capture assertion.
#
# WHAT IT HOLDS, each against the effect:
#   1. run 1 handed over and the baton exists, carrying the planted
#      prohibition -- the seed run 2 will read is real.
#   2. the floor: an unreadable file is a clean error BEFORE any model call
#      (usage exit, the flag named, the live mock's capture untouched).
#   3. the flag refuses alongside --session: a baton starts a FRESH session
#      (6.7), so combining them is a usage error, not a silent resume.
#   4. THE TOOTH (fresh history, 10.5): run 2's captured req.1 holds the
#      untrusted-fenced notes AND the new task, under run 2's OWN fresh
#      session id, and NONE of run 1's history (its task, its assistant
#      answer, the account text) -- two processes, not a resume.
#   5. the notes are fenced: the seed carries both the <<< UNTRUSTED relay
#      notes marker and the relay provenance line.
#   6. the PROHIBITION-SCAN EXEMPTION (6.7's sharp edge): in AUTO mode, the
#      "never run tests" sentence inside the notes' section 3 does NOT
#      become an adopted constraint -- the `[constraint] inferred` line is
#      absent from run 2's log. (The fence alone would not have saved this:
#      quote_mask exempts ```/~~~/quotes, and the UNTRUSTED line is none of
#      those -- the guard at the scan site is what exempts.)
#   7. the refusal runs made no model call: cap2 holds exactly req.1 (run 2's
#      one request).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# A baton that carries the scan's favourite prohibition in section 3.
# (Must stay on one line: mockmodel caps a script line at 1024 bytes.)
NOTE_TEXT="Done: read the rig file. In progress: none. Next step: implement the flag. Decided: notes go into a fresh session, never a resume. Dead ends: never run tests yet; the harness is not wired."

# --- run 1: hand over, write the baton (sub-slices B/C's rig) -------------
printf 'a small file for the rig\n' > "$ws/a.txt"
cat > "$tmp/leg1.mm" <<EOF
wire openai
rule
  match "[relay]"
  text ${NOTE_TEXT}
rule
  match "messages"
  usage 1000 5
  tool read_file {"path":"a.txt"}
EOF
mm_start "$tmp/leg1.mm" "$tmp/cap1"
write_config "$tmp/config_on.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true' '"contextLength":1000'
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_on.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/on.json" 2> "$tmp/on.err"); rc_on=$?
mm_stop
stop_on=$("$JQ" '.stop_reason' "$tmp/on.json" 2>/dev/null)
id1=$("$JQ" '.session_id' "$tmp/on.json" 2>/dev/null)
note1="$HOME/.jichi.d/relay/$id1/leg-01.md"

# 1: the floor for everything below -- run 1 handed over and the baton
# exists and carries the planted prohibition.
if [ "$rc_on" -eq 0 ] && [ "$stop_on" = "handover" ] && [ -s "$note1" ] && \
   grep -q "never run tests" "$note1"; then
    t_ok "run 1 handed over and its baton carries the planted prohibition"
else
    t_fail "run 1 rig broken: rc=$rc_on stop='$stop_on' note='$note1' \
prohibition present: $(grep -c 'never run tests' "$note1" 2>/dev/null)"
fi

# --- run 2: the next leg, from the baton ----------------------------------
cat > "$tmp/leg2.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 12 4
  text "continuing from the previous leg"
EOF
mm_start "$tmp/leg2.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" '"maxToolIters":3' \
    '"contextLength":20000'
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    --auto --output json --handoff-in "$note1" \
    -p "finish the remaining work from the previous leg" \
    < /dev/null > "$tmp/leg2.json" 2> "$tmp/leg2.err"); rc2=$?

# 2: an unreadable file is a clean usage error BEFORE any model call
# (the mock is still live; a leaked request would land in cap2 -- check 7).
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config2.json" \
    --handoff-in "$tmp/does-not-exist.md" -p "a prompt" \
    < /dev/null > "$tmp/nf.out" 2> "$tmp/nf.err"); rc_nf=$?
if [ "$rc_nf" -eq 2 ] && grep -q "handoff-in" "$tmp/nf.err"; then
    t_ok "an unreadable --handoff-in file is a clean usage error naming the flag"
else
    t_fail "missing-file refusal wrong: rc=$rc_nf err=$(head_bytes 120 "$tmp/nf.err")"
fi

# 3: the flag refuses alongside --session (a baton starts a FRESH session).
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config2.json" \
    --session "$id1" --handoff-in "$note1" -p "a prompt" \
    < /dev/null > "$tmp/ref.out" 2> "$tmp/ref.err"); rc_ref=$?
if [ "$rc_ref" -eq 2 ] && grep -q "handoff-in" "$tmp/ref.err"; then
    t_ok "--handoff-in refuses alongside --session (usage error)"
else
    t_fail "refusal wrong: rc=$rc_ref err=$(head_bytes 120 "$tmp/ref.err")"
fi
mm_stop
req="$tmp/cap2/req.1"

# 4: THE TOOTH (fresh history, 10.5) -- run 2's outgoing request carries the
# baton as its ONE user message (the baton holds run 1's task in section 1 and
# the account in section 3, so those strings ARE expected in it). What must be
# ABSENT is run 1's CONVERSATION: the assistant tool_call turn, the tool RESULT
# ("a small file for the rig"), any second user message. A fresh single turn
# has exactly one user message and no tool results at all.
id2=$("$JQ" '.session_id' "$tmp/leg2.json" 2>/dev/null)
# The capture is headers + the JSON body; the body escapes nothing, so a raw
# "role":"X" substring counts that role's messages. A PLAIN fresh turn has
# exactly 1 user message and 0 assistant / 0 tool messages (measured on this
# rig), so any assistant or tool message is run 1's conversation leaking.
nuser=$(grep -c '"role":"user"' "$req" 2>/dev/null)
nasst=$(grep -c '"role":"assistant"' "$req" 2>/dev/null)
ntool=$(grep -c '"role":"tool"' "$req" 2>/dev/null)
if [ "$rc2" -eq 0 ] && [ -n "$id2" ] && [ "$id1" != "$id2" ] && \
   [ "$nuser" = "1" ] && [ "$nasst" = "0" ] && [ "$ntool" = "0" ] && \
   grep -q "Relay notes" "$req" && \
   grep -q "finish the remaining work from the previous leg" "$req" && \
   grep -q "Done: read the rig file" "$req"; then
    t_ok "run 2's request is a fresh single turn: the baton as one user message, none of run 1's conversation"
else
    t_fail "fresh-history assertion: rc2=$rc2 id1='$id1' id2='$id2' \
user_msgs='$nuser' assistant_msgs='$nasst' tool_msgs='$ntool' (want 1/0/0); \
baton in req: $(grep -c 'Relay notes' "$req" 2>/dev/null), \
new task: $(grep -c 'finish the remaining work' "$req" 2>/dev/null), \
account: $(grep -c 'Done: read the rig file' "$req" 2>/dev/null)"
fi

# 5: the seed is fenced untrusted content with the relay provenance line.
if grep -q '<<< UNTRUSTED relay notes' "$req" && \
   grep -q '\[relay\] Continuing from the previous leg' "$req"; then
    t_ok "the seed carries the UNTRUSTED fence and the [relay] provenance line"
else
    t_fail "fence missing: UNTRUSTED=$(grep -c '<<< UNTRUSTED relay notes' \
"$req" 2>/dev/null) relay-tag=$(grep -c 'Continuing from the previous leg' \
"$req" 2>/dev/null)"
fi

# 6: THE PROHIBITION-SCAN EXEMPTION -- in AUTO mode, the "do not run tests"
# sentence inside the notes must NOT be adopted as a constraint.
if ! grep -q "\[constraint\] inferred" "$tmp/leg2.err"; then
    t_ok "the notes' prohibition did not become an adopted constraint in AUTO mode"
else
    t_fail "the AUTO scan adopted a constraint from the baton's text: \
$(grep '\[constraint\]' "$tmp/leg2.err" | head_bytes 200)"
fi

# 7: the refusal runs made no model call: cap2 holds exactly run 2's one
# request (req.1). A leaked request from a refused run would be req.2.
nreqs=$(ls "$tmp/cap2"/req.* 2>/dev/null | wc -l | tr -d ' ')
if [ "$nreqs" = "1" ] && [ -s "$req" ]; then
    t_ok "the refused runs made no model call (exactly one request captured)"
else
    t_fail "capture holds $nreqs requests, expected 1 -- a refused run \
reached the model"
fi

t_done
