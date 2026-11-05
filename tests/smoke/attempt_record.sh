#!/bin/sh
# smoke: `attempt` records what it cost and keeps a run journal (M898, plan
# 2026-10-03 step 6.0).
#
# THE GAP. The plan's curriculum measurement asks for minutes and calls per point.
# `attempt --output json` said the verdict, the hints and the tokens, and nothing
# about time, calls or which model ran. And `jichi runs` never listed an attempt:
# with no envelope flag none wrote a journal, and with one (`--max-tool-calls 5`) the
# process envelope opened a journal that only ever held `open`, because the turn ran
# under the attempt's own envelope -- a row reading outcome `?` and 0 tokens for an
# attempt that had passed (measured on c7390b68).
#
# THE FIX. The JSON row gains wall_s, model_calls, tool_calls, tool_calls_executed,
# model, model_id and run. The attempt keeps a run journal -- the process envelope's
# when there is one, else ~/.jichi.d/runs/<id>.jsonl -- and appends an `attempt` event
# with the verdict, which `jichi runs` shows as attempt=VERDICT.
#
#   1  the JSON row carries wall_s >= the mock's 1200 ms delay
#   2  ... model_calls 3, tool_calls 2, tool_calls_executed 2 for a three-call attempt
#   3  ... the model's name and wire id, and the run id
#   4  with no envelope flag, `jichi runs` lists the attempt (outcome ok, attempt=PASS)
#      and its run id is the JSON row's
#   5  with --max-tool-calls 5, one journal, outcome ok and attempt=PASS (not `?`)
#   6  `runs --output json` carries "attempt": "PASS"
#   7  a failing attempt is listed attempt=FAIL
#   8  CONTROL: `--journal -` writes no journal
#   9  CONTROL: a bounded -p run is listed without an attempt note
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- jichi snapshots shell out to it"

t_plan 9
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
RUNS="$HOME/.jichi.d/runs"

mkdir -p "$ws/docs/assignments"
printf 'broken\n' > "$ws/code.txt"
cat > "$ws/docs/assignments/fix.md" <<'EOF'
---
title: fix the file
verify: "grep -q fixed code.txt"
points: 1
---
Make code.txt say fixed.
EOF

cat > "$tmp/pass.mm" <<'EOF'
wire openai
rule
  count 1
  delay 1200
  tool read_file {"path":"code.txt"}
rule
  count 2
  tool write_file {"path":"code.txt","content":"fixed\n"}
rule
  text DONE
EOF
cat > "$tmp/fail.mm" <<'EOF'
wire openai
rule
  text I GIVE UP
EOF

cfg() {
    cat <<EOF
{"models":[{"name":"m-att","provider":"openai","model":"mock-wire",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"assignments":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
}

# attempt NAME MOCK [FLAGS...] -- stdout to $tmp/NAME.json, stderr to $tmp/NAME.err
attempt() {
    _n="$1"; _mm="$2"; shift 2
    rm -rf "$RUNS"
    mm_start_unbounded "$_mm" "$tmp/$_n.cap"
    cfg > "$tmp/$_n.cfg"
    ( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/$_n.cfg" attempt \
        docs/assignments/fix.md --output json "$@" < /dev/null \
        > "$tmp/$_n.json" 2> "$tmp/$_n.err" )
    mm_stop
    "$BIN" runs < /dev/null > "$tmp/$_n.runs" 2>&1
    "$BIN" runs --output json < /dev/null > "$tmp/$_n.runsj" 2>&1
    ls "$RUNS" 2>/dev/null | grep -c '\.jsonl$' > "$tmp/$_n.n"
}
field() { sed -n "s/.*\"$2\":\\([^,}]*\\).*/\\1/p" "$tmp/$1.json" | head -n 1; }

# --- 1-4, 6: a passing attempt with no envelope flag -----------------------------------
attempt pass "$tmp/pass.mm"
w=$(field pass wall_s)
if [ -n "$w" ] && awk -v w="$w" 'BEGIN{exit !(w >= 1.2)}'; then
    t_ok "the JSON row carries wall_s >= the mock's 1200 ms delay ($w)"
else
    t_fail "wall_s=[$w]: $(head_bytes 300 < "$tmp/pass.json")"
fi
if [ "$(field pass model_calls)" = "3" ] && [ "$(field pass tool_calls)" = "2" ] &&
   [ "$(field pass tool_calls_executed)" = "2" ]; then
    t_ok "the JSON row carries model_calls 3, tool_calls 2, tool_calls_executed 2"
else
    t_fail "calls: model=[$(field pass model_calls)] tool=[$(field pass tool_calls)] exec=[$(field pass tool_calls_executed)]"
fi
run=$(field pass run | tr -d '"')
if [ "$(field pass model)" = '"m-att"' ] && [ "$(field pass model_id)" = '"mock-wire"' ] &&
   [ -n "$run" ]; then
    t_ok "the JSON row names the model, its wire id and the run"
else
    t_fail "model=[$(field pass model)] model_id=[$(field pass model_id)] run=[$run]"
fi
if [ "$(cat "$tmp/pass.n")" = "1" ] && [ -n "$run" ] && [ -f "$RUNS/$run.jsonl" ] &&
   grep -q ' ok .*attempt=PASS' "$tmp/pass.runs"; then
    t_ok "with no envelope flag, jichi runs lists the attempt under the row's run id"
else
    t_fail "journals=$(cat "$tmp/pass.n") run=[$run]: $(grep -v '^$' "$tmp/pass.runs" | tail -n 2 | head_bytes 200)"
fi
pass_runsj="$tmp/pass.runsj"

# --- 5: with an envelope flag, one journal and a real row ------------------------------
attempt capped "$tmp/pass.mm" --max-tool-calls 5
if [ "$(cat "$tmp/capped.n")" = "1" ] && grep -q ' ok .*attempt=PASS' "$tmp/capped.runs"; then
    t_ok "with --max-tool-calls 5, one journal, outcome ok and attempt=PASS"
else
    t_fail "journals=$(cat "$tmp/capped.n"): $(grep -v '^$' "$tmp/capped.runs" | tail -n 2 | head_bytes 200)"
fi

# --- 6: the JSON listing --------------------------------------------------------------
if grep -q '"attempt":"PASS"' "$pass_runsj"; then
    t_ok "runs --output json carries \"attempt\": \"PASS\""
else
    t_fail "runs json: $(head_bytes 300 < "$pass_runsj")"
fi

# --- 7: a failing attempt -------------------------------------------------------------
attempt fail "$tmp/fail.mm"
if grep -q 'attempt=FAIL' "$tmp/fail.runs"; then
    t_ok "a failing attempt is listed attempt=FAIL"
else
    t_fail "fail: $(grep -v '^$' "$tmp/fail.runs" | tail -n 2 | head_bytes 200)"
fi

# --- 8: CONTROL -- --journal - -------------------------------------------------------
attempt nojournal "$tmp/pass.mm" --journal -
if [ "$(cat "$tmp/nojournal.n")" = "0" ] && grep -q '"verdict":"PASS"' "$tmp/nojournal.json"; then
    t_ok "CONTROL: --journal - writes no journal"
else
    t_fail "--journal -: journals=$(cat "$tmp/nojournal.n") $(head_bytes 160 < "$tmp/nojournal.json")"
fi

# --- 9: CONTROL -- a bounded -p run is not an attempt ---------------------------------
rm -rf "$RUNS"
mm_start_unbounded "$tmp/fail.mm" "$tmp/plain.cap"
cfg > "$tmp/plain.cfg"
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/plain.cfg" -q --no-session \
    --max-tool-calls 5 -p "say something" < /dev/null > /dev/null 2>&1 )
mm_stop
"$BIN" runs < /dev/null > "$tmp/plain.runs" 2>&1
if grep -q ' ok ' "$tmp/plain.runs" && ! grep -q 'attempt=' "$tmp/plain.runs"; then
    t_ok "CONTROL: a bounded -p run is listed without an attempt note"
else
    t_fail "plain: $(grep -v '^$' "$tmp/plain.runs" | tail -n 2 | head_bytes 200)"
fi

t_done
