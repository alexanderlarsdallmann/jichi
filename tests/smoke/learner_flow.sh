#!/bin/sh
# smoke: the learner-support surface (M173b). TWO halves over one
# assignment spec:
#   TUI (PTY, no model calls -- all local): /assignment loads the brief +
#     announces the TUTOR stance; /hint walks the ladder in order (rung 1,
#     then rung 2); /grade FAILs the untouched workspace and PASSes after
#     the file is solved out-of-band; /assignment off ends the session.
#   headless: `hint`/`hint N` (range-checked), `grade --output json
#     --record` (the gradebook row + progress JSONL), `assignments
#     --output json`.
# (Port of tests/e2e/learner_flow.py, M217.)
. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
spec="docs/assignments/hello.md"

mkdir -p "$ws/docs/assignments"
cat > "$ws/$spec" <<'EOF'
---
title: E2E hello brief
audience: student
verify: "grep -q 'Hello, Giessen' hello.txt"
points: 1
hints:
  - Rung one names the concept.
  - Rung two shows the approach.
---
Create `hello.txt` so the verify command passes.
EOF

# --- TUI half: ptydrive; solve out-of-band between the two /grade calls ---
# split into two scripts so the sh driver can create hello.txt in between
# A settle `delay` follows each command-output expect before the next
# send: expect returns the instant the marker bytes appear (mid-render),
# so without it the next command lands before the prompt is back in
# readline -- the paste/typed timing lesson, which bites hardest under
# full-suite load (learner_flow flaked here first).
cat > "$tmp/a.pd" <<'EOF'
expect "] " 15
delay 400
send "/assignment docs/assignments/hello.md\r"
expect "E2E hello brief" 15
expect "TUTOR" 15
delay 400
send "/hint\r"
expect "Hint 1 of 2" 15
expect "Rung one names the concept" 15
delay 400
send "/grade\r"
expect "FAIL" 15
delay 400
send "/exit\r"
waitexit 12
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 \
    --log "$tmp/a.log" "$tmp/a.pd" -- "$BIN" --no-route --no-lite); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "/assignment rendered the brief + announced the tutor stance"
else
    t_fail "TUI phase 1 rc=$rc"
fi
if grep -q "Hint 1 of 2" "$tmp/a.log" \
   && grep -q "Rung one names the concept" "$tmp/a.log"; then
    t_ok "/hint revealed rung 1"
else
    t_fail "rung 1 not revealed"
fi
if grep -q "FAIL" "$tmp/a.log"; then
    t_ok "/grade FAILed the untouched workspace"
else
    t_fail "/grade did not FAIL on the untouched workspace"
fi

# solve it, as a learner editing in another window would
printf 'Hello, Giessen\n' > "$ws/hello.txt"

cat > "$tmp/b.pd" <<'EOF'
expect "] " 15
delay 400
send "/assignment docs/assignments/hello.md\r"
expect "E2E hello brief" 15
delay 400
send "/grade\r"
expect "PASS" 15
delay 400
send "/hint\r"
expect "Hint 1 of 2" 15
delay 400
send "/hint\r"
expect "Hint 2 of 2" 15
delay 400
send "/assignment off\r"
expect "assignment closed" 15
delay 400
send "/exit\r"
waitexit 12
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 \
    --log "$tmp/b.log" "$tmp/b.pd" -- "$BIN" --no-route --no-lite); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "/grade PASSed after solving; ladder reached rung 2; /assignment off closed"
else
    t_fail "TUI phase 2 rc=$rc (see transcript)"
fi
# ordering: rung 1 must appear before rung 2 in the transcript
if [ "$(grep -n 'Hint 1 of 2' "$tmp/b.log" | head -1 | cut -d: -f1)" \
     -lt "$(grep -n 'Hint 2 of 2' "$tmp/b.log" | head -1 | cut -d: -f1)" ]; then
    t_ok "the hint ladder was revealed in order"
else
    t_fail "hints revealed out of order"
fi

# --- 10 (M614, seam A2): the TUI pulls above are RECORDED --------------------------
# /hint was the one hint writer that recorded nothing -- while the CLI (M502)
# and the tool (M536) both append to .jichi/hints.jsonl, and the docs promise
# "recorded, never penalised". Phases a+b pulled three rungs (1; then 1 and 2),
# so the log must exist with >=3 lines and rung 2 present.
hlog="$ws/.jichi/hints.jsonl"
if [ -f "$hlog" ] && [ "$(grep -c '"rung"' "$hlog")" -ge 3 ] \
   && grep -q '"rung":2' "$hlog"; then
    t_ok "the TUI /hint pulls are in .jichi/hints.jsonl (3 rungs, deepest 2)"
else
    t_fail "TUI hint pulls unrecorded: $(wc -l < "$hlog" 2>/dev/null || echo 0) line(s) -- \
the ladder's real path (M319/M320: models pull no hints) left no teacher record"
fi

# --- 11+12 (M614, seam A3): a /grade that CANNOT RUN is not a grade ----------------
# The TUI re-implemented grading without the M502 guard and recorded
# unconditionally: a verify whose PROGRAM does not resolve from here wrote
# FAIL 0% into progress.jsonl as if the work were wrong. Through the shared
# mechanic it must say "NOT a grade" and record nothing.
cat > "$ws/docs/assignments/broken.md" <<'BEOF'
---
title: Broken harness
audience: student
verify: "sh ./missing/grader.sh"
points: 1
---
The grader script is not reachable from this directory.
BEOF
rows_before=$(grep -c . "$ws/.jichi/progress.jsonl" 2>/dev/null || echo 0)
cat > "$tmp/c.pd" <<'CEOF'
expect "] " 15
delay 400
send "/assignment docs/assignments/broken.md\r"
expect "Broken harness" 15
delay 400
send "/grade\r"
expect "NOT a grade" 15
delay 400
send "/exit\r"
waitexit 12
CEOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 40 --cols 100 \
    --log "$tmp/c.log" "$tmp/c.pd" -- "$BIN" --no-route --no-lite); rc=$?
if [ $rc -eq 0 ] && grep -q "NOT a grade" "$tmp/c.log"; then
    t_ok "/grade on an unreachable verify says NOT a grade (M502, now in the TUI)"
else
    t_fail "TUI phase 3 rc=$rc -- no refusal wording (see $tmp/c.log)"
fi
rows_after=$(grep -c . "$ws/.jichi/progress.jsonl" 2>/dev/null || echo 0)
if [ "$rows_after" -eq "$rows_before" ]; then
    t_ok "the refusal recorded nothing (progress rows: $rows_before unchanged)"
else
    t_fail "a non-grade was recorded: progress rows $rows_before -> $rows_after"
fi

# --- headless half ---
out=$(cd "$ws" && "$BIN" hint "$spec" < /dev/null 2>&1)
case "$out" in *"Hint 1 of 2"*) t_ok "headless hint prints rung 1" ;;
    *) t_fail "hint rung 1 missing: $(printf '%s' "$out" | head_bytes 100)" ;; esac
out=$(cd "$ws" && "$BIN" hint "$spec" 2 < /dev/null 2>&1)
case "$out" in *"Hint 2 of 2"*) : ;; *) t_fail "hint 2 missing" ;; esac
(cd "$ws" && "$BIN" hint "$spec" 9 < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    t_ok "an out-of-range rung is an error"
else
    t_fail "out-of-range rung returned rc=0"
fi

(cd "$ws" && "$BIN" grade "$spec" --output json --record \
    < /dev/null > "$tmp/row.json" 2>/dev/null)
if [ "$("$JQ" '.passed' "$tmp/row.json" 2>/dev/null)" = "true" ] \
   && [ "$("$JQ" '.pct' "$tmp/row.json" 2>/dev/null)" = "100" ]; then
    t_ok "grade --output json: passed=true pct=100"
else
    t_fail "grade json wrong: $(cat "$tmp/row.json")"
fi
prog="$ws/.jichi/progress.jsonl"
if [ -f "$prog" ] \
   && [ "$(tail -1 "$prog" | "$JQ" '.spec')" = "$spec" ] \
   && [ "$(tail -1 "$prog" | "$JQ" '.passed')" = "true" ]; then
    t_ok "--record appended a correct progress.jsonl row"
else
    t_fail "progress record wrong/missing"
fi

t_done
