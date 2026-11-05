#!/bin/sh
# smoke: Esc stops a running turn, as Ctrl-C does, and says so (M753).
#
# THE DEFECT, from the operator's twelve questions (plan section 9, decided
# 2026-09-25): "people press Esc; today only Ctrl-C works, and nothing says Esc
# does not." Measured before the fix: a lone Esc during a turn did nothing at
# all. Without type-ahead the terminal was not even held during a turn, so the
# key never reached jichi; with type-ahead it was swallowed as the start of an
# escape sequence and the byte after it was dropped with it.
#
# WHAT THIS HOLDS, each against the effect rather than the attempt:
#   1. an ARROW key during a turn is not the Esc key: the turn completes. A
#      terminal sends ESC [ A as one burst, and the fix must not read its first
#      byte as a stop.
#   2. printable keys typed during a turn without type-ahead are not kept, and
#      the notice says so ONCE -- two keys are typed, so "once" and "per key"
#      differ.
#   3. a lone Esc during a slow model call stops the turn: "[interrupted]", and
#      the model's late answer never reaches the transcript.
#   4. it says why: the Esc notice is printed.
#   5. the same with type-ahead on (a second session), where the old code
#      swallowed the ESC as a sequence start -- and pressed TWICE, since the
#      second ESC arriving would otherwise read as the byte after the first.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The arrow turn answers after 3 s -- long enough that an arrow read as Esc would
# stop it well before its answer (polls come about once a second while a model
# call waits); the Esc turn would answer after 8 s. The
# mock is sequential, so the Esc turn goes last in each session -- and its rule
# is FIRST, because the second request carries the first turn's text too and
# would otherwise match ARROW_TURN.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "ESC_TURN"
  delay 8000
  text LATE_ANSWER
rule
  match "ARROW_TURN"
  delay 3000
  text ARROW_DONE
rule
  text OTHER
EOF

cat > "$tmp/a.pd" <<'EOF'
expect "] " 15
send "ARROW_TURN go\r"
delay 500
send "\e[A"
delay 200
send "x"
delay 150
send "y"
expect "ARROW_DONE" 15
delay 1000
send "ESC_TURN go\r"
delay 800
send "\e"
expect "[interrupted]" 4
delay 300
send "/exit\r"
waitexit 15
assertexit 0
EOF

mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/a.log" "$tmp/a.pd" \
    -- "$BIN" --config "$tmp/config.json" --no-route --no-session) \
    > "$tmp/a.out" 2>&1; rc=$?
mm_stop
tr '\r' '\n' < "$tmp/a.log" > "$tmp/a.txt"

# 1: the arrow did not stop the arrow turn -- ARROW_DONE printed, and neither the
# Esc notice nor "[interrupted]" appears before it. The notice is the direct
# symptom: a stop that fires at the answer's first delta still lets the answer
# print, so "the answer printed" alone cannot see an arrow read as Esc.
first_int=$(grep -nE '\[interrupted\]|Esc pressed' "$tmp/a.txt" | head -1 | cut -d: -f1)
done_at=$(grep -n 'ARROW_DONE' "$tmp/a.txt" | head -1 | cut -d: -f1)
if [ -n "$done_at" ] && { [ -z "$first_int" ] || [ "$first_int" -gt "$done_at" ]; }; then
    t_ok "an arrow key during a turn does not stop it (the turn answered)"
else
    t_fail "the arrow-key turn did not complete (done_at='$done_at' interrupted_at='$first_int' rc=$rc)"
fi

# 2: without type-ahead, the typed key is not kept, and that is said once
nk=$(grep -c 'not kept (type-ahead is off' "$tmp/a.txt")
if [ "$nk" -eq 1 ] && ! grep -q '\[operator\]' "$tmp/cap"/req.* 2>/dev/null; then
    t_ok "a key typed during a turn without type-ahead is not kept, and the notice says so once"
else
    t_fail "not-kept notice printed $nk time(s) (want 1)"
fi

# 3: a lone Esc stopped the slow turn before its answer
if [ $rc -eq 0 ] && grep -q '\[interrupted\]' "$tmp/a.txt" && ! grep -q 'LATE_ANSWER' "$tmp/a.txt"; then
    t_ok "a lone Esc stops a running turn before the model answers"
else
    t_fail "Esc did not stop the turn (rc=$rc; late answer $(grep -c LATE_ANSWER "$tmp/a.txt") time(s))"
fi

# 4: and it says why
if grep -q 'Esc pressed: stopping this turn' "$tmp/a.txt"; then
    t_ok "the stop says Esc caused it"
else
    t_fail "no Esc notice in the transcript"
fi

# 5: with type-ahead on, where the old code swallowed the ESC as a sequence start,
# and Esc pressed twice: two ESC bytes must be the key, not an Alt chord.
ws2=$(smoke_tmp)
tmp2=$(smoke_tmp)
cp "$tmp/replies.mm" "$tmp2/replies.mm"
cat > "$tmp2/b.pd" <<'EOF'
expect "] " 15
send "ESC_TURN go\r"
delay 800
send "\e\e"
expect "[interrupted]" 4
delay 300
send "/exit\r"
waitexit 15
assertexit 0
EOF
mm_start_unbounded "$tmp2/replies.mm" "$tmp2/cap"
write_config "$tmp2/config.json" "$MM_PORT"
(cd "$ws2" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp2/b.log" "$tmp2/b.pd" \
    -- "$BIN" --config "$tmp2/config.json" --no-route --no-session --type-ahead) \
    > "$tmp2/b.out" 2>&1; rc2=$?
mm_stop
if [ $rc2 -eq 0 ] && ! tr '\r' '\n' < "$tmp2/b.log" | grep -q 'LATE_ANSWER'; then
    t_ok "with type-ahead on, Esc pressed twice stops the turn too"
else
    t_fail "type-ahead session: Esc did not stop the turn (rc=$rc2)"
fi

t_done
