#!/bin/sh
# smoke: `check_model` -- the sanctioned live check the model can call (M1198,
# G3, batch G, card 30). RED FIRST: this driver is committed before the tool
# exists. The contract is docs/internal/drive/2026-11-05-harness-port/notes/M1198.md.
#
# WHAT. In a clean environment the trial model cannot verify its configured
# model works from inside a run and goes looking for the key instead. `check_model`
# is a read-only, full-profile-only tool that makes ONE real request -- the
# `doctor --live` probe (jc_oneshot_probe + JC_TOOLPROBE_PROMPT, the single
# `jichi_probe_echo` tool) -- against the run's active model and reports what it
# observed: pass iff the class is `native`, else a fail that names the class
# (text / none / unknown, the M628 convention), its own ms timing and prompt-token
# count, and -- on an incomplete request -- the HTTP status NUMBER.
#
# THE CANARY DISCIPLINE (why bytes, not jsonq). D-G3's invariant: the result
# carries NO text the endpoint or the model wrote -- not the reply, not an
# excerpt, not the server's error body. So every arm plants a canary byte string
# (CHECK_MODEL_CANARY_C<n>) in model-written or server-written text, and each
# cell greps the RAW check_model tool_result line for the canary's ABSENCE and
# for its own verdict words (pass / fail / the class). A byte grep is the only
# honest check here: parsing the result as JSON would silently drop an embedded
# model string we are trying to prove is NOT there. The point is that model/server
# text exists somewhere in the run and must not surface in the result.
#
# THE MOCK PROTOCOL (one headless full-profile run per arm, C1-C5). The run's
# model is scripted to CALL `check_model`; running the tool fires an INTERNAL
# probe request that advertises `jichi_probe_echo`. The .mm separates the two by
# whether the request body names `jichi_probe_echo`:
#   * main request, no tool yet  -> `tool check_model {}`
#   * main request, tool role     -> plain text stop (C1's stop carries its canary)
#   * probe request               -> the arm's reply:
#        C1 native  -> tool jichi_probe_echo {"text":"ping"}
#        C2 text    -> prose naming jichi_probe_echo (the model "described" the call)
#        C3 none    -> prose that calls nothing and names no tool
#        C4 unknown -> an EMPTY reply (a bare `text` rule: no content, no calls)
#        C5         -> status 400 with a canary body (the server's own words)
# C4's empty shape: mockmodel has no "empty body" action, but a bare `text` line
# (no argument) parses and renders an empty-content `stop` chunk -- the closest
# expressible empty, and exactly what jc_oneshot_probe reads as "no answer".
#
# C6 availability: two headless --dump-requests runs (one full-profile, one
# core-profile), each answering a neutral prompt with plain text. The full run's
# first captured request must advertise check_model; the core run's must not
# (the offered-set assertion, chatbot_example's cell-8 shape).
#
# RED ON THIS HEAD (no tool code yet). check_model is not registered, so the
# scripted `tool check_model {}` call is refused as an unknown tool and the
# jsonl tool_result line reads "error: unknown tool 'check_model'": C1-C5 fail on
# the missing pass/fail and class words (C4's "unknown" even matches the refusal,
# but the cell is red on the missing "fail"); C6's full-profile half fails because
# check_model is not advertised. That is the red this commit carries.
#
# This driver is NOT FAULT-gated: it runs in every build.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$ws"

# cm_res OUTFILE -- the run's check_model tool_result line (jsonl: one object
# per line; the result rides in the bounded `preview` string).
cm_res() {
    grep '"type":"tool_result"' "$1" 2>/dev/null |
        grep '"name":"check_model"' | head -n 1
}

# run_jsonl CFG OUT REQS PROMPT -- one headless full-profile --auto turn, jsonl
# to OUT, captured request bodies into REQS. stdin pinned (smoke_lint 18).
run_jsonl() {
    (cd "$ws" && with_deadline 60 "$BIN" --config "$1" --no-session --auto \
        --output jsonl --dump-requests "$3" -p "$4" < /dev/null > "$2" 2> "$2.err")
}

# ---------------------------------------------------------------------------
# C1 native (pass). Stop text carries the canary -- model-written text in the
# run that must not reach the result.
cat > "$tmp/c1.mm" <<MM
wire openai
rule
  nomatch "jichi_probe_echo"
  nomatch "\"role\":\"tool\""
  tool check_model {}
rule
  nomatch "jichi_probe_echo"
  match "\"role\":\"tool\""
  text STOP CHECK_MODEL_CANARY_C1
rule
  match "jichi_probe_echo"
  tool jichi_probe_echo {"text":"ping"}
MM
mm_start "$tmp/c1.mm" "$tmp/c1cap" 4
write_config "$tmp/c1.json" "$MM_PORT"
run_jsonl "$tmp/c1.json" "$tmp/c1.jsonl" "$tmp/c1reqs" "verify the model and stop"
mm_stop
line=$(cm_res "$tmp/c1.jsonl")
if [ -n "$line" ] && printf '%s' "$line" | grep -q 'pass' &&
   printf '%s' "$line" | grep -q 'native' &&
   printf '%s' "$line" | grep -qE '[0-9]' &&
   ! printf '%s' "$line" | grep -q 'CHECK_MODEL_CANARY_C1'; then
    t_ok "C1 native: the check_model result names pass, class native and an ms figure, no canary"
else
    if [ -n "$line" ]; then
        tail=$(printf '%s' "$line" | head_bytes 180)
    else
        tail="<no check_model tool_result line in the jsonl>"
    fi
    t_fail "C1 native: the result does not read as a native pass -- $tail"
fi

# ---------------------------------------------------------------------------
# C2 text (fail, class named). The canary rides in the model's prose reply.
cat > "$tmp/c2.mm" <<MM
wire openai
rule
  nomatch "jichi_probe_echo"
  nomatch "\"role\":\"tool\""
  tool check_model {}
rule
  nomatch "jichi_probe_echo"
  match "\"role\":\"tool\""
  text STOP
rule
  match "jichi_probe_echo"
  text I will call jichi_probe_echo with text set to ping CHECK_MODEL_CANARY_C2
MM
mm_start "$tmp/c2.mm" "$tmp/c2cap" 4
write_config "$tmp/c2.json" "$MM_PORT"
run_jsonl "$tmp/c2.json" "$tmp/c2.jsonl" "$tmp/c2reqs" "verify the model and stop"
mm_stop
line=$(cm_res "$tmp/c2.jsonl")
if [ -n "$line" ] && printf '%s' "$line" | grep -q 'fail' &&
   printf '%s' "$line" | grep -q 'text' &&
   ! printf '%s' "$line" | grep -q 'CHECK_MODEL_CANARY_C2'; then
    t_ok "C2 text: the result is a fail naming the class text, no canary"
else
    if [ -n "$line" ]; then
        tail=$(printf '%s' "$line" | head_bytes 180)
    else
        tail="<no check_model tool_result line in the jsonl>"
    fi
    t_fail "C2 text: the result does not read as a fail of class text -- $tail"
fi

# ---------------------------------------------------------------------------
# C3 none (fail, class named). Prose that neither calls nor describes the call.
cat > "$tmp/c3.mm" <<MM
wire openai
rule
  nomatch "jichi_probe_echo"
  nomatch "\"role\":\"tool\""
  tool check_model {}
rule
  nomatch "jichi_probe_echo"
  match "\"role\":\"tool\""
  text STOP
rule
  match "jichi_probe_echo"
  text Sorry, I cannot run that check right now CHECK_MODEL_CANARY_C3
MM
mm_start "$tmp/c3.mm" "$tmp/c3cap" 4
write_config "$tmp/c3.json" "$MM_PORT"
run_jsonl "$tmp/c3.json" "$tmp/c3.jsonl" "$tmp/c3reqs" "verify the model and stop"
mm_stop
line=$(cm_res "$tmp/c3.jsonl")
if [ -n "$line" ] && printf '%s' "$line" | grep -q 'fail' &&
   printf '%s' "$line" | grep -q 'none' &&
   ! printf '%s' "$line" | grep -q 'CHECK_MODEL_CANARY_C3'; then
    t_ok "C3 none: the result is a fail naming the class none, no canary"
else
    if [ -n "$line" ]; then
        tail=$(printf '%s' "$line" | head_bytes 180)
    else
        tail="<no check_model tool_result line in the jsonl>"
    fi
    t_fail "C3 none: the result does not read as a fail of class none -- $tail"
fi

# ---------------------------------------------------------------------------
# C4 unknown (fail, class named). An EMPTY reply (no text, no calls) -- the
# M628 else-branch-not-a-finding convention. There is no model text to carry a
# canary, so CHECK_MODEL_CANARY_C4 is planted nowhere; its absence from the
# result is asserted for discipline.
cat > "$tmp/c4.mm" <<MM
wire openai
rule
  nomatch "jichi_probe_echo"
  nomatch "\"role\":\"tool\""
  tool check_model {}
rule
  nomatch "jichi_probe_echo"
  match "\"role\":\"tool\""
  text STOP
rule
  match "jichi_probe_echo"
  text
MM
mm_start "$tmp/c4.mm" "$tmp/c4cap" 4
write_config "$tmp/c4.json" "$MM_PORT"
run_jsonl "$tmp/c4.json" "$tmp/c4.jsonl" "$tmp/c4reqs" "verify the model and stop"
mm_stop
line=$(cm_res "$tmp/c4.jsonl")
if [ -n "$line" ] && printf '%s' "$line" | grep -q 'fail' &&
   printf '%s' "$line" | grep -q 'unknown' &&
   ! printf '%s' "$line" | grep -q 'CHECK_MODEL_CANARY_C4'; then
    t_ok "C4 unknown: the result is a fail naming the class unknown (the M628 convention)"
else
    if [ -n "$line" ]; then
        tail=$(printf '%s' "$line" | head_bytes 180)
    else
        tail="<no check_model tool_result line in the jsonl>"
    fi
    t_fail "C4 unknown: the result does not read as a fail of class unknown -- $tail"
fi

# ---------------------------------------------------------------------------
# C5 the request did not complete. The probe's 400 body carries the server's
# own words (the canary); the result must name the status NUMBER and nothing of
# the body.
cat > "$tmp/c5.mm" <<MM
wire openai
rule
  nomatch "jichi_probe_echo"
  nomatch "\"role\":\"tool\""
  tool check_model {}
rule
  nomatch "jichi_probe_echo"
  match "\"role\":\"tool\""
  text STOP
rule
  match "jichi_probe_echo"
  status 400
  body CHECK_MODEL_CANARY_C5
MM
mm_start "$tmp/c5.mm" "$tmp/c5cap" 4
write_config "$tmp/c5.json" "$MM_PORT"
run_jsonl "$tmp/c5.json" "$tmp/c5.jsonl" "$tmp/c5reqs" "verify the model and stop"
mm_stop
line=$(cm_res "$tmp/c5.jsonl")
if [ -n "$line" ] && printf '%s' "$line" | grep -q '400' &&
   ! printf '%s' "$line" | grep -q 'CHECK_MODEL_CANARY_C5'; then
    t_ok "C5 incomplete: the result names HTTP 400 and carries no body prose"
else
    if [ -n "$line" ]; then
        tail=$(printf '%s' "$line" | head_bytes 180)
    else
        tail="<no check_model tool_result line in the jsonl>"
    fi
    t_fail "C5 incomplete: the result does not name the 400 or leaked the body -- $tail"
fi

# ---------------------------------------------------------------------------
# C6 availability: full-profile advertises check_model, core-profile does not.
cat > "$tmp/c6.mm" <<MM
wire openai
rule
  text STOP
MM
mkdir -p "$tmp/c6reqs_full" "$tmp/c6reqs_core"

# Full-profile run (write_config bakes toolProfile full + lowResource false).
mm_start "$tmp/c6.mm" "$tmp/c6cap_full" 2
write_config "$tmp/c6_full.json" "$MM_PORT"
run_jsonl "$tmp/c6_full.json" "$tmp/c6_full.jsonl" "$tmp/c6reqs_full" "say hello"
full_first=$(ls "$tmp/c6reqs_full"/req-*.json 2>/dev/null | head -n 1)
full_hits=$(grep -c 'check_model' "$full_first" 2>/dev/null)
mm_stop

# Core-profile run: the same config with the profile switched to core.
sed 's#"toolProfile":"full"#"toolProfile":"core"#' "$tmp/c6_full.json" > "$tmp/c6_core.json"
mm_start "$tmp/c6.mm" "$tmp/c6cap_core" 2
run_jsonl "$tmp/c6_core.json" "$tmp/c6_core.jsonl" "$tmp/c6reqs_core" "say hello"
core_first=$(ls "$tmp/c6reqs_core"/req-*.json 2>/dev/null | head -n 1)
core_hits=$(grep -c 'check_model' "$core_first" 2>/dev/null)
mm_stop

full_hits=${full_hits:-0}
core_hits=${core_hits:-0}
if [ "$full_hits" -ge 1 ] && [ "$core_hits" -eq 0 ]; then
    t_ok "C6 availability: the full profile advertises check_model, the core profile does not"
else
    t_fail "C6 availability: offered set wrong (full-profile check_model hits=$full_hits, core-profile hits=$core_hits; expected >=1 and 0)"
fi

t_done
