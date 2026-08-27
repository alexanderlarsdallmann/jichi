#!/bin/sh
# smoke: a PERIODIC verify that keeps failing the same way is journaled (M422).
#
# THE DEFECT THIS EXISTS FOR. M89 detects a run thrashing on one error by
# comparing the verify output's failure signature. It has TWO call sites -- the
# completion fix-forward path and M81's periodic (--verify-every) path -- and only
# the completion one emitted the `verify_stuck` journal event. The periodic path
# appended the NOTE to the model's message and logged a WARN to stderr, so the
# MODEL was told it was stuck while the operator's table stayed blank. M420 then
# wired `runs`' `stuck=N` to that event, which made the gap load-bearing: mid-turn
# thrashing -- exactly what --verify-every exists to catch -- could never show.
#
# Measured on a real run before the fix: stderr carried "verify stuck on the same
# error (2x)", "(3x)", "(4x)"; the journal carried zero verify_stuck events; `runs`
# printed `0/4` with no stuck= note.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The model edits a file every round, so tool calls accrue and --verify-every 1
# fires after each round. The gate always fails with the SAME `error:` line, which
# is what the signature comparison keys on.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"work.txt","content":"attempt one\n"}
rule
  count 2
  tool write_file {"path":"work.txt","content":"attempt two\n"}
rule
  count 3
  tool write_file {"path":"work.txt","content":"attempt three\n"}
rule
  count 4
  tool write_file {"path":"work.txt","content":"attempt four\n"}
rule
  text GAVE_UP
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 6
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto \
      --verify "echo 'error: stuck probe -- always the same failure' >&2; exit 1" \
      --verify-every 1 --max-tool-calls 4 \
      --journal "$tmp/run.jsonl" \
      -p "edit the file" < /dev/null 2>&1); rc=$?
mm_stop

# --- 1: the periodic gate ran and stayed red ---------------------------------
nver=$(grep -c '"event":"verify"' "$tmp/run.jsonl" 2>/dev/null || true)
if [ "$nver" -ge 2 ]; then
    t_ok "the periodic verifier ran $nver times"
else
    t_fail "rc=$rc only $nver verify events -- the periodic gate did not fire; events=$(grep -o '\"event\":\"[a-z_]*\"' "$tmp/run.jsonl" | sort | uniq -c | tr '\n' ' '); out=$(printf '%s' "$out" | tail -c 100)"
fi

# --- 2: the repeat was JOURNALED, not only warned to stderr ------------------
nstuck=$(grep -c '"event":"verify_stuck"' "$tmp/run.jsonl" 2>/dev/null || true)
if [ "$nstuck" -ge 1 ]; then
    t_ok "the repeated failure is journaled ($nstuck verify_stuck events)"
else
    t_fail "$nstuck verify_stuck events in the journal, though the same error repeated $nver times"
fi

# --- 3: and it reaches the operator's table ---------------------------------
# The whole point of the event: `runs` renders stuck=N (M420). A journal entry no
# reader surfaces is the same defect one step later.
if "$BIN" runs "$tmp" 2>/dev/null | grep -q 'stuck='; then
    t_ok "runs shows stuck= for the thrashing run"
else
    t_fail "runs printed no stuck= note: $("$BIN" runs "$tmp" 2>/dev/null | tail -2 | tr '\n' ' ')"
fi

t_done
