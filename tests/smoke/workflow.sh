#!/bin/sh
# smoke: deterministic workflow (M101) -- the spec parser + dispatch, and the
# read-only map fence (M634). Checks 1-3 are offline: a spec with no usable
# stages errors before any model call. Checks 4-5 run ONE scripted-model map,
# because the fence is a runtime gate (app->readonly around the fan-out) and
# proving it holds needs a model that tries to write. Until these checks the
# only gate on that fence was workflow_refute.sh's check 7, which counts
# refusals across both of ITS stages -- a fence held only by a neighbour's
# driver is one refactor away from unheld (DEFERRED row closed 2026-09-16).
# (Checks 1-3 are a port of tests/e2e/workflow.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
write_config "$tmp/config.json" 9

printf '{ "name": "x", "stages": [] }\n' > "$ws/empty.json"
(cd "$ws" && "$BIN" --config "$tmp/config.json" workflow empty.json \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
if [ $rc -ne 0 ] && grep -q "no usable stages" "$tmp/err"; then
    t_ok "empty-stages spec errors before any model call"
else
    t_fail "rc=$rc err=$(head_bytes 150 "$tmp/err")"
fi

printf 'not json at all\n' > "$ws/bad.json"
(cd "$ws" && "$BIN" --config "$tmp/config.json" workflow bad.json \
    < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -ne 0 ]; then
    t_ok "invalid-JSON spec errors"
else
    t_fail "invalid JSON accepted (rc=0)"
fi

(cd "$ws" && "$BIN" --config "$tmp/config.json" workflow \
    < /dev/null > /dev/null 2>"$tmp/err2"); rc=$?
if [ $rc -eq 2 ] && grep -q "usage" "$tmp/err2"; then
    t_ok "no spec -> usage error (exit 2)"
else
    t_fail "rc=$rc (want 2) err=$(head_bytes 150 "$tmp/err2")"
fi

# --- the read-only map fence (M634) ------------------------------------------------
# M634 found the read-only fan-out was read-only by advertisement only:
# include_mutating 0 kept the mutating tools off the MENU, and a model calling
# write_file unadvertised wrote to the tree. The fix sets app->readonly around
# the fan-out (src/main.c), the fence jc_tool_execute consults. The mock plays
# a seat that tries exactly that: first request answered with a write_file
# call, the follow-up (carrying the tool result) answered with text.
printf 'SENTINEL: a reviewer may read this file, never change it\n' > "$ws/victim.txt"
cp "$ws/victim.txt" "$tmp/victim.orig"
cat > "$tmp/ro.mm" <<'EOF2'
wire openai
rule
  match "\"role\":\"tool\""
  text the write was refused; findings reported only.
  usage 20 10
rule
  tool write_file {"path":"victim.txt","content":"OVERWRITTEN by the read-only seat"}
EOF2
mm_start "$tmp/ro.mm" "$tmp/rocap"
write_config "$tmp/ro-config.json" "$MM_PORT"
cat > "$ws/ro.json" <<'EOF2'
{ "name": "ro-map",
  "stages": [
    { "type": "map", "readonly": true, "prompt": "Review $ITEM.", "items": ["victim.txt"] }
  ] }
EOF2
(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/ro-config.json" workflow ro.json \
    < /dev/null > "$tmp/ro-out.txt" 2> "$tmp/ro-err.txt"); rc=$?
mm_stop

# --- 4: the seat's write was refused AT THE GATE, visible on the wire --------------
# A refused call goes back to the model as a tool result naming the fence, so
# the follow-up request carries the sentence. Zero refusals means the write
# went through unopposed -- or the mock never fired; either is a failure here.
if [ $rc -eq 0 ] && grep -q "tool disabled in read-only mode" "$tmp/rocap"/* 2>/dev/null; then
    t_ok "read-only map: write_file refused at the gate, refusal on the wire"
else
    t_fail "rc=$rc, no gate refusal on the wire: $(head_bytes 200 "$tmp/ro-err.txt")"
fi
# --- 5: the tree is untouched -- the effect, not the attempt -----------------------
if cmp -s "$ws/victim.txt" "$tmp/victim.orig"; then
    t_ok "read-only map: the file under review is byte-identical after the run"
else
    t_fail "victim.txt was modified: $(head_bytes 120 "$ws/victim.txt")"
fi

# --- 6-7: M643 -- a stage's `model` field must pick the model, on EVERY stage type.
# THE DEFECT. map and refute resolved s->model into a temporary provider; synthesize
# called jc_oneshot_ex(app->provider, ...) and never looked at s->model, so a spec that
# pinned the synthesis to a reasoning model silently got the ACTIVE model instead. Found
# by dogfooding on the zigodot project, where spending the slow model on the judgement
# was the entire point of the pin. No test asserted a synthesize stage's output at all,
# which is why it survived.
#
# OBSERVED AT THE WIRE, not at the answer. Two mocks; the question is only which one
# RECEIVES the request, so the check does not depend on the reply being accepted --
# an earlier draft of it asserted the answer text and passed whether or not the pin
# worked, because this one-shot path yields no answer from a mock either way.
cat > "$tmp/mA.mm" <<'MMA'
wire openai
rule
  text FROM_A
MMA
cp "$tmp/mA.mm" "$tmp/mB.mm"
mm_start "$tmp/mA.mm" "$tmp/capA"; PORT_A=$MM_PORT; PID_A=$MM_PID
mm_start "$tmp/mB.mm" "$tmp/capB"; PORT_B=$MM_PORT
cat > "$tmp/pin-config.json" <<EOF
{"models":[
 {"name":"deflt","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$PORT_A/v1","apiKey":"x","roles":["chat"]},
 {"name":"pinned","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$PORT_B/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",
 "lowResource":false,"maxRetries":0}
EOF
cat > "$ws/pinned-syn.json" <<'EOF'
{ "name":"pin", "input":"material",
  "stages":[ {"type":"synthesize","model":"pinned","prompt":"summarize"} ] }
EOF
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/pin-config.json" --no-session \
    workflow pinned-syn.json < /dev/null > /dev/null 2>&1)
_nA=$(ls "$tmp/capA" 2>/dev/null | grep -c "^req" || true)
_nB=$(ls "$tmp/capB" 2>/dev/null | grep -c "^req" || true)
if [ "$_nB" -ge 1 ] && [ "$_nA" -eq 0 ]; then
    t_ok "a synthesize stage's \`model\` picks that model (the request reached it, not the active one)"
else
    t_fail "synthesize ignored its \`model\`: requests to the pinned model=$_nB, to the ACTIVE model=$_nA (want >=1 and 0)"
fi
# ...and a model name the config does not define must be NAMED, not silently ignored.
cat > "$ws/bad-syn.json" <<'EOF'
{ "name":"pin", "input":"material",
  "stages":[ {"type":"synthesize","model":"no-such-model","prompt":"summarize"} ] }
EOF
err=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/pin-config.json" --no-session \
      workflow bad-syn.json < /dev/null 2>&1 >/dev/null)
kill "$PID_A" 2>/dev/null
mm_stop
case "$err" in
    *no-such-model*) t_ok "an unknown stage model is NAMED, not silently ignored" ;;
    *) t_fail "an unknown stage model passed silently: $(printf '%s' "$err" | head_bytes 140)" ;;
esac
t_done
