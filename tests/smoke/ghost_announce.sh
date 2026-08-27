#!/bin/sh
# smoke: an inline suggestion is ANNOUNCED, not ghosted, for a listener (M578).
#
# THE DEFECT, reported by the operator running a screen reader. They typed a
# question, pressed Ctrl-G, and their reader spoke the model's suggestion as
# though they had typed it themselves:
#
#     chat > what is the Japanese word for free  ジュウジ (jūji)
#
# The ghost marks itself as "not yours" with ONE thing -- `\x1b[2m`, dim -- and
# dim is COLOUR. By M561's test that makes it a defect rather than a preference:
# information carried by colour VANISHES for a listener, where information
# carried by punctuation merely reads badly. There is no second signal.
#
# AND THE REMEDY WAS ALREADY IN THE HEADER, for the gesture next door. jc_term.h
# describes Ctrl-Q advice as "printed on its own labelled line above a redrawn
# prompt", split from suggestions BECAUSE a model's reply spliced into the line
# "produces garbled input". Under a reader a suggestion has exactly that problem,
# so it gets exactly that rendering -- while Tab still accepts it, because it is
# still a suggestion and not advice.
#
# WHAT CARRIES THE DISTINCTION NOW IS A WORD. The label is a catalog entry
# (JC_MSG_SUGGESTION), so it translates; the announce path emits NO escape
# sequence at all, which is the point -- the signal has to survive without one.
#
# CHECK 5 IS THE ONE THAT KEEPS THIS HONEST: moving where a suggestion is drawn
# must not change what it is. Tab must still splice it into the line.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The suggester makes a one-shot call, so the mock serves it. A distinctive
# token makes the assertions unambiguous.
# TWO DISTINCT TOKENS AND TWO WIRE SHAPES, both learned the hard way.
#
# The tokens: with one token for every reply, check 1 matched the CHAT ANSWER and
# reported "both arms produced a suggestion" when neither had. The suggester
# fires first (Ctrl-G precedes Enter), so request 1 is the suggestion.
#
# The shapes: THE AUTOCOMPLETE CALL IS NON-STREAMING. tui_suggest builds its own
# request without `stream` and parses it with parse_full, while the chat turn
# sends `"stream":true`. A mock that answers both with SSE leaves the suggester
# with nothing to parse -- which looked exactly like "Ctrl-G does nothing", and
# cost a round of diagnosis until tests/smoke/ghost.sh (M9) turned out to have
# solved it already: a `body` rule for the suggester, a `text` rule for the turn.
cat > "$tmp/m.mm" <<'EOF'
wire openai
rule
  count 1
  status 200
  body {"choices":[{"index":0,"message":{"role":"assistant","content":"GHOSTWORD"}}]}
rule
  text CHATREPLY
EOF

# Type, ask for a suggestion (Ctrl-G = \x07), accept it (Tab = \x09), send.
cat > "$tmp/p.pd" <<'EOF'
delay 3000
send "tell me about "
delay 500
send "\x07"
delay 3000
send "\x09"
delay 800
send "\r"
delay 3000
send "/exit\r"
waitexit 20
EOF

run_arm() {
    _n="$1"; shift
    mm_start "$tmp/m.mm" "$tmp/cap.$_n"
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" --deadline 65 \
        --cols 100 --log "$tmp/$_n.log" "$tmp/p.pd" -- \
        "$BIN" --config "$tmp/config.json" --no-session "$@" \
        > /dev/null 2>&1) || true
    mm_stop
    tr '\r' '\n' < "$tmp/$_n.log" > "$tmp/$_n.txt"
}
run_arm acc --accessible
run_arm pln

# A THIRD ARM THAT NEVER ACCEPTS, because check 3 needs a moment when the
# suggestion EXISTS and has not been taken. In the arms above, Tab splices it
# into the buffer and the prompt legitimately redraws as "tell me about
# GHOSTWORD" -- so asserting that string is absent there measures acceptance,
# not rendering, and the first version of check 3 failed for exactly that
# reason. Here Ctrl-U clears the line instead (dismissing the ghost), so any
# occurrence on the typed line can only have come from the inline rendering.
cat > "$tmp/noaccept.pd" <<'EOF'
delay 3000
send "tell me about "
delay 500
send "\x07"
delay 3000
send "\x15"
delay 500
send "/exit\r"
waitexit 20
EOF
mm_start "$tmp/m.mm" "$tmp/cap.noacc"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" --deadline 65 --cols 100 \
    --log "$tmp/noacc.log" "$tmp/noaccept.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1) || true
mm_stop
tr '\r' '\n' < "$tmp/noacc.log" > "$tmp/noacc.txt"

# ---- 1: the denominator -- a suggestion was actually produced --------------
# Checks 2-4 are presence/absence pairs over these captures; on a run where
# Ctrl-G produced nothing they are all satisfiable by silence.
if $G -q 'GHOSTWORD' "$tmp/acc.txt" && $G -q 'GHOSTWORD' "$tmp/pln.txt" &&
   $G -q 'CHATREPLY' "$tmp/acc.txt"; then
    t_ok "both arms produced a suggestion, distinct from the chat reply"
else
    t_fail "Ctrl-G produced no suggestion, so nothing below tests anything \
(accessible=$($G -c GHOSTWORD "$tmp/acc.txt"), default=$($G -c GHOSTWORD \
"$tmp/pln.txt")). The suggester needs a reachable model; check the mock. \
Tail: $(tail -2 "$tmp/acc.txt" 2>/dev/null | tr '\n' ' ' | head_bytes 160)"
fi

# ---- 2: accessible -- a WORD announces it ---------------------------------
if $G -q 'Suggestion, press Tab to accept' "$tmp/acc.txt"; then
    t_ok "accessible: the suggestion is announced by name"
else
    t_fail "no spoken label before the suggestion. Dim text is the only thing \
distinguishing a ghost from the user's own words, and dim does not survive \
being read aloud -- the operator's reader spoke a suggested Japanese word as \
though they had typed it. JC_MSG_SUGGESTION must precede it."
fi

# ---- 3: accessible -- and it is NOT on the user's line -------------------
# The property, not the label: a build that printed the label AND still ghosted
# inline would pass check 2 and leave the defect exactly where it was. Measured
# on the arm that never accepts, so the only way the string can reach the typed
# line is the inline rendering.
# EVERY line carrying the suggestion must also carry the label. That is the
# robust form: the first version asserted that "tell me about .*GHOSTWORD" was
# absent, which PASSED with announce disabled -- the redraw and the ghost land in
# different chunks once \r is expanded, so the two never shared a line and the
# check could not fire. Verified by perturbation: disabling announce now reddens
# this.
nbare=$(tr '\r' '\n' < "$tmp/noacc.log" \
        | $G 'GHOSTWORD' | $G -vc 'Suggestion, press Tab' || true)
[ -n "$nbare" ] || nbare=0
if $G -q 'GHOSTWORD' "$tmp/noacc.txt" && [ "$nbare" -eq 0 ]; then
    t_ok "accessible: an unaccepted suggestion never touches the typed line"
else
    t_fail "$nbare line(s) carry the suggestion WITHOUT the label, so it is \
still being drawn into the user's own text: \
$(tr '\r' '\n' < "$tmp/noacc.log" | $G 'GHOSTWORD' | $G -v 'Suggestion, press Tab' \
  | head -1 | head_bytes 120). Announcing must REPLACE the inline ghost, not \
accompany it. (Suggestion seen at all: $($G -c GHOSTWORD "$tmp/noacc.txt"))"
fi

# ---- 4: THE REGRESSION GUARD -- sighted users keep the inline ghost ------
# The dim ghost is good design for someone who can see it: it shows exactly
# where the text would land. This must not become prose for everybody.
if $G -q 'tell me about .*GHOSTWORD' "$tmp/pln.txt" &&
   ! $G -q 'Suggestion, press Tab' "$tmp/pln.txt"
then
    t_ok "default: the inline ghost is unchanged"
else
    t_fail "the DEFAULT rendering changed. --accessible must add an arm, not \
replace the ghost for everyone: a sighted user sees where the text would land, \
which prose cannot show. Inline: $($G -c 'tell me about .*GHOSTWORD' \
"$tmp/pln.txt"), label: $($G -c 'Suggestion, press Tab' "$tmp/pln.txt")"
fi

# ---- 5: and Tab STILL ACCEPTS under announce ----------------------------
# Asserted on the captured REQUEST, not the screen: what matters is that the
# accepted text reached the model as part of the user's message. Moving where a
# suggestion is drawn must not change what it is.
if $G -rq 'tell me about' "$tmp/cap.acc" 2>/dev/null &&
   $G -rq 'GHOSTWORD' "$tmp/cap.acc" 2>/dev/null
then
    t_ok "accessible: Tab still splices the suggestion into the line"
else
    t_fail "Tab no longer accepts an announced suggestion -- the buffer reached \
the model without it. has_ghost must stay set when the suggestion is announced; \
only its rendering changes. Requests mentioning the word: \
$($G -rlc 'GHOSTWORD' "$tmp/cap.acc" 2>/dev/null | head -1)"
fi

t_done
