#!/bin/sh
# smoke: the type-ahead notices lose their leading glyph for a listener (M569).
#
# THE GAP. queue_notice is the ONE chrome site in src/tui/jc_tui.c that reached
# for a glyph without asking who was listening:
#
#     printf("  %s %s", c->unicode ? "\xe2\x96\xb8" : ">", label);
#
# Every neighbour -- cb_tool_start, cb_tool_result, print_token_line -- tests
# `accessible` first and takes a prose branch. This one did not, so all four
# queue notices (queued / queue full / unsent / dropped) led with U+25B8, or
# with ">" on a non-UTF-8 terminal.
#
# WHAT IS NOT CLAIMED, and this is the M559 discipline. Whether Orca VOICES
# U+25B8 is unmeasured. espeak-ng renders both the glyph and the ">" fallback
# as silence -- but Orca performs punctuation verbalisation BEFORE the
# synthesizer sees the text, so espeak cannot answer the question, and ">"
# under a SOME punctuation style is very likely spoken as "greater". The
# justification here is CONSISTENCY, which is measurable: one site behaved
# differently from its four neighbours. Two spoken words per notice is the
# worst case this removes and zero is the best.
#
# WHY A PTY IS UNAVOIDABLE. A queue notice only exists mid-turn, when
# type-ahead is armed and the terminal is held -- there is no headless
# equivalent and no unit-testable seam. The fixture is typeahead.sh's: rule 1
# stalls 3s (the window to type) then calls a read-only tool, so the run
# reaches a tool boundary without an approval prompt.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  delay 3000
  tool list_files {"path":"."}
rule
  text DONE_HERE
EOF

# THE ANCHOR IS THE ARROW, not the bracket. `expect "] "` -- copied from
# typeahead.sh, which only ever runs the sighted prompt -- matched nothing in
# the accessible arm, because M562 replaced `[chat.chat.2%] >` with `chat >`
# and there is no `]` in it. The script then never typed, the capture was
# empty, and check 2 (an ABSENCE assertion) passed on nothing. Check 1 caught
# it on the first run, which is the entire reason it exists. Both prompts end
# in the arrow, ASCII ">" here because _smoke.sh exports LC_ALL=C.
cat > "$tmp/steer.pd" <<'EOF'
expect "> " 15
send "hello\r"
delay 900
send "also read the docs"
delay 700
send "\r"
expect "DONE_HERE" 30
delay 500
send "/exit\r"
waitexit 10
EOF

# --- arm A: accessible ----------------------------------------------------
mm_start "$tmp/replies.mm" "$tmp/a"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 80 "$SMOKE_TOOLS/ptydrive" --deadline 75 --cols 100 \
    --log "$tmp/acc.log" "$tmp/steer.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route --type-ahead \
       --accessible > /dev/null 2>&1) || true
mm_stop

# --- arm B: default, the regression guard ---------------------------------
mm_start "$tmp/replies.mm" "$tmp/b"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 80 "$SMOKE_TOOLS/ptydrive" --deadline 75 --cols 100 \
    --log "$tmp/pln.log" "$tmp/steer.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route --type-ahead \
       > /dev/null 2>&1) || true
mm_stop

# The notice line from each capture. Extracted rather than grepped in place, so
# check 2 cannot be satisfied by a glyph somewhere else on screen -- the
# spinner and the tool lines have glyphs of their own.
tr '\r' '\n' < "$tmp/acc.log" | $G 'queued for the next step' > "$tmp/acc.line"
tr '\r' '\n' < "$tmp/pln.log" | $G 'queued for the next step' > "$tmp/pln.line"

# ---- 1: the denominator -- both arms produced the notice ------------------
# Every assertion below reads these two one-line files. If type-ahead did not
# arm, or the stall window was missed, they are empty and checks 2-4 are
# vacuous: check 2 asserts an ABSENCE, which holds trivially in nothing.
if [ -s "$tmp/acc.line" ] && [ -s "$tmp/pln.line" ]; then
    t_ok "both arms emitted the queue notice"
else
    t_fail "no queue notice captured (accessible=$(wc -c < "$tmp/acc.line") \
bytes, default=$(wc -c < "$tmp/pln.line") bytes) -- nothing below tests \
anything. The typed line must land inside rule 1's 3s stall. Last lines: \
$(tr '\r' '\n' < "$tmp/acc.log" | tail -2 | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2: accessible: NO leading glyph, in either rendering ----------------
# Both forms asserted absent. Checking only U+25B8 would pass on a non-UTF-8
# terminal, where the fallback ">" is the thing a reader would voice -- and
# _smoke.sh exports LC_ALL=C, so ">" is exactly what this arm would print if
# the fix were absent.
if ! $G -q '\xe2\x96\xb8' "$tmp/acc.line" && ! $G -q '> queued' "$tmp/acc.line"
then
    t_ok "accessible: the notice carries no leading glyph"
else
    t_fail "the queue notice still leads with a glyph under --accessible: \
$(cat "$tmp/acc.line" | head_bytes 120). queue_notice must test c->accessible \
before reaching for one, as every neighbouring chrome site does."
fi

# ---- 3: and the label survived --------------------------------------------
# Suppressing the glyph must not suppress the notice. Without this, deleting
# the whole printf would pass check 2.
if $G -q 'queued for the next step' "$tmp/acc.line"; then
    t_ok "accessible: the notice text is still printed"
else
    t_fail "the notice text vanished with its glyph -- check 2 would pass on a \
build that printed nothing at all"
fi

# ---- 4: THE REGRESSION GUARD -- sighted output keeps its glyph -----------
# LC_ALL=C here, so the expected marker is the ">" fallback rather than U+25B8.
# A fix that simply deleted the glyph for everyone reddens this.
if $G -q '> queued for the next step' "$tmp/pln.line"; then
    t_ok "default: the notice keeps its leading marker"
else
    t_fail "the DEFAULT notice lost its marker. --accessible must add an arm, \
not remove the glyph for everybody -- a sighted user scans for it. Line: \
$(cat "$tmp/pln.line" | head_bytes 120)"
fi

t_done
