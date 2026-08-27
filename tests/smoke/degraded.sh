#!/bin/sh
# smoke: the terminal object reports decisions made in the operator's absence (M443).
#
# THE DEFECT. A headless run makes decisions ON THE OPERATOR'S BEHALF and reported them
# only obliquely:
#   * `ask_user` with no delegate -> "no interactive user is available, proceed on your
#     own judgment". The journal records `answered:false`, but only the OFFLINE `runs`
#     reader counts those, so a supervisor reading the run's own result learned nothing.
#   * an ASK verdict with no `confirm_tool` -> a synthetic tool error. The model sees it
#     and may work around it; the operator sees nothing.
#   * a privileged or kinetic gate with nobody to ask -> refused. Same asymmetry.
# So "ran clean" and "ran having answered three of its own questions" were the same
# `stop_reason: done`.
#
# WHAT IS DELIBERATELY NOT COUNTED: a tool auto-approved under `--auto`. That is the
# operator's explicit instruction, not a decision taken in their absence -- counting it
# would make every --auto run degraded and the flag worthless. Check 5 pins that.
#
# WHY PRESENCE IS THE FLAG. The object is emitted only when a count is non-zero, so a
# supervisor tests `if ("degraded" in done)`. An always-present `degraded:false` trains a
# reader to skip the field on the one run where it matters -- the M420 argument that gave
# `goalposts=N` its non-zero gate.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# The model asks two questions nobody can answer, then finishes. Headless, so app->ask
# is NULL and both are answered by jichi on its own behalf.
cat > "$tmp/ask.mm" <<'EOF'
wire openai
rule
  count 1
  tool ask_user {"question":"Should I use tabs or spaces?"}
rule
  count 2
  tool ask_user {"question":"Which branch should I target?"}
rule
  text ASKED_DONE
EOF

mm_start "$tmp/ask.mm" "$tmp/capa" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --output json \
    -p "ask me things" < /dev/null > "$tmp/a.json" 2>/dev/null); rca=$?
mm_stop

# --- 1: the run completes, and the degraded object is present -------------------
if [ "$rca" -eq 0 ] && "$JQ" -q '.degraded' "$tmp/a.json" >/dev/null 2>&1; then
    t_ok "the terminal object carries a degraded block"
else
    t_fail "rca=$rca no degraded block: $(head_bytes 200 "$tmp/a.json")"
fi

# --- 2: it counts BOTH unanswered questions -----------------------------------
# Two, not one: a boolean would not distinguish a run that asked once from one that
# asked repeatedly, and repetition is what tells an operator the brief was unclear.
u=$("$JQ" '.degraded.unanswered' "$tmp/a.json" 2>/dev/null)
if [ "$u" = "2" ]; then
    t_ok "both unanswered questions are counted (unanswered=2)"
else
    t_fail "unanswered=$u, expected 2"
fi

# --- 3: stop_reason is still `done` -------------------------------------------
# The flag reports, it does not judge. The run DID finish; a supervisor decides whether
# two unanswered questions make the result untrustworthy, and conflating the two would
# take that decision away.
sr=$("$JQ" '.stop_reason' "$tmp/a.json" 2>/dev/null)
if [ "$sr" = "done" ]; then
    t_ok "stop_reason stays 'done' -- degraded reports, it does not judge"
else
    t_fail "stop_reason=$sr -- the flag changed the outcome"
fi

# --- 4: an ASK-verdict tool refused for want of an approver is counted ---------
# A DIFFERENT class from the questions: here a tool the run wanted was refused only
# because nobody could approve it. Not under --auto, so the gate really fires.
cat > "$tmp/appr.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text APPR_DONE
rule
  tool write_file {"path":"new.txt","content":"x"}
EOF

mm_start "$tmp/appr.mm" "$tmp/capb" 9
sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config2.json"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    --no-session --output json \
    -p "write a file" < /dev/null > "$tmp/b.json" 2>/dev/null); rcb=$?
mm_stop

a=$("$JQ" '.degraded.approval_unavailable' "$tmp/b.json" 2>/dev/null)
if [ "$a" = "1" ]; then
    t_ok "a tool refused for want of an approver is counted separately"
else
    t_fail "approval_unavailable=$a (rcb=$rcb): $(head_bytes 200 "$tmp/b.json")"
fi

# --- 5: --auto is NOT degraded ------------------------------------------------
# The load-bearing negative. Under --auto the same write is auto-approved because the
# operator asked for that; if this counted, every --auto run would be degraded and the
# flag would mean nothing. A field that is always set is a field nobody reads.
mm_start "$tmp/appr.mm" "$tmp/capc" 9
sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config3.json"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" \
    --no-session --auto --budget-tokens 100k --output json \
    -p "write a file" < /dev/null > "$tmp/c.json" 2>/dev/null); rcc=$?
mm_stop

if [ "$rcc" -eq 0 ] && ! "$JQ" -q '.degraded' "$tmp/c.json" >/dev/null 2>&1; then
    t_ok "an --auto run that auto-approves is NOT reported degraded"
else
    t_fail "rcc=$rcc --auto marked degraded: $("$JQ" '.degraded.approval_unavailable' "$tmp/c.json" 2>/dev/null)"
fi

# --- 6: a clean run carries no degraded key at all ----------------------------
# Presence is the flag, so a clean run must omit it entirely rather than report zeros.
cat > "$tmp/clean.mm" <<'EOF'
wire openai
rule
  text CLEAN_DONE
EOF
mm_start "$tmp/clean.mm" "$tmp/capd" 9
sed "s/127.0.0.1:[0-9]*/127.0.0.1:$MM_PORT/" "$tmp/config.json" > "$tmp/config4.json"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config4.json" \
    --no-session --output json -p "just answer" < /dev/null \
    > "$tmp/d.json" 2>/dev/null)
mm_stop
if ! grep -q '"degraded"' "$tmp/d.json"; then
    t_ok "a clean run omits the key entirely (presence is the flag)"
else
    t_fail "a clean run reported a degraded block"
fi

t_done
