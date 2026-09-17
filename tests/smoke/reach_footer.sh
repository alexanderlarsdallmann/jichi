#!/bin/sh
# smoke: the reach footer -- a headless answer is footed with what the run's
# RECORD checked and what it did not, derived from counters, never from the
# model (M630).
#
# WHY. tsuiseki-04: a run's summary is a summary of what was TRIED, and the
# final sentence is the one artifact nothing checked. The journal knows
# whether a verifier ran and what colour it was, how many tool results were
# errors, whether a write left the scope -- and a user reads the sentence and
# never the journal. So the envelope prints two lines after the answer (on
# stderr: stdout stays the raw answer for scripts, the M73 rule) and puts the
# same facts in the `done` object as `reach`. A zero is a claim and is printed;
# an absence is stated, never left blank (M316).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
echo "hello" > "$ws/note.txt"

# the model reads a file that does not exist (one tool error) then answers
# First matching rule wins and `count` is a LIFETIME request index, so the
# answer rule (a tool result is in the request) comes first and the tool-call
# rule is a count-less catch-all -- four separate runs then share one mock.
cat > "$tmp/replies.mm" <<'EOF2'
wire openai
rule
  match "\"role\":\"tool\""
  text REACH_ANSWER
  usage 20 5
rule
  tool read_file {"path":"missing.txt"}
EOF2
mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"

# --- 1-2: no envelope -- the footer says so, and counts the error ------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -p "read it" \
    < /dev/null > "$tmp/out1.txt" 2> "$tmp/err1.txt"); rc=$?
if grep -q "not checked: no envelope armed" "$tmp/err1.txt"; then
    t_ok "a plain -p run says no envelope was armed (nothing about the result was tested)"
else
    t_fail "no footer (rc=$rc): $(tr '\n' ' ' < "$tmp/err1.txt" | head_bytes 200)"
fi
if grep -q "1 tool call, 1 error" "$tmp/err1.txt"; then
    t_ok "the footer counts the tool error the model was handed"
else
    t_fail "error not counted: $(grep checked "$tmp/err1.txt" | head_bytes 200)"
fi
if grep -q "checked:" "$tmp/out1.txt"; then
    t_fail "the footer leaked onto stdout -- scripts read the raw answer there"
else
    t_ok "the footer stays on stderr; stdout is the raw answer"
fi

# --- 4: --auto with a verifier: the colour is stated -----------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto \
    --verify "true" --budget-tokens 100k -p "read it" \
    < /dev/null > /dev/null 2> "$tmp/err2.txt"); rc=$?
if grep -q "checked: verify green" "$tmp/err2.txt"; then
    t_ok "with --verify the footer states the verifier's colour"
else
    t_fail "no verify colour (rc=$rc): $(grep checked "$tmp/err2.txt" | head_bytes 200)"
fi
if grep -q "no edit scope -- writes were not fenced" "$tmp/err2.txt"; then
    t_ok "the unarmed scope is stated as not checked"
else
    t_fail "scope absence not stated: $(grep "not checked" "$tmp/err2.txt" | head_bytes 200)"
fi

# --- 6: -q suppresses the footer ------------------------------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q -p "read it" \
    < /dev/null > /dev/null 2> "$tmp/err3.txt")
if grep -q "checked:" "$tmp/err3.txt"; then
    t_fail "-q did not silence the footer"
else
    t_ok "-q silences the footer like every other diagnostic"
fi

# --- 7: --output json carries the same facts as `reach` ---------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto \
    --verify "true" --budget-tokens 100k --output json -p "read it" \
    < /dev/null > "$tmp/out4.json" 2>/dev/null)
mm_stop
v=$("$JQ" .reach.verify < "$tmp/out4.json" 2>/dev/null | tr -d '"')
e=$("$JQ" .reach.tool_errors < "$tmp/out4.json" 2>/dev/null)
if [ "$v" = "green" ] && [ "$e" = "1" ]; then
    t_ok "the done object's reach says verify green and one tool error"
else
    t_fail "reach in JSON wrong (verify=$v errors=$e): $(head_bytes 200 "$tmp/out4.json")"
fi
t_done
