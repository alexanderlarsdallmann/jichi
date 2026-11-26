#!/bin/sh
# smoke: fallback on connect failure (M1038, MO-1). A model that answers with
# HTTP 4xx/5xx is REACHABLE and stays in place; a model that never speaks
# (connect failure) re-probes its endpoint, walks the fallback chain, and
# routes the turn to a reachable fallback. Four legs:
#   LEG 1 (the repro, RED before the fix): model A serves exactly one tool call
#     then exits; the follow-up call against A's dead port is a connect failure.
#     The fallback B is live. Pre-fix: run fails after maxRetries (rc!=0), no
#     route event, no model_call row for B. Post-fix: run completes on B (rc=0),
#     telemetry carries a route event with reason=connect-fallback, B's answer
#     reaches stdout. LEG 2 (negative): same but A has NO fallback -> still
#     fails after maxRetries (no routing, no behaviour change). LEG 3 (behaviour
#     preservation): A answers with HTTP 401 (a server that ANSWERS: reachable).
#     No route event, every call stays on A, run fails (401 is not transient).
#   LEG 5 (M1207 DM5, GREEN pre- and post-fix, pins the M1038 re-route on the
#     mock CAPTURES): A serves one model round (its tool call) then dies,
#     B = the live fallback. Counting MODEL rounds (POST req.N files; the
#     GET /v1/models reachability probes burn mock budget too): B served
#     exactly 1 (the post-fallback tool round, naming model b), A exactly 1
#     (its own round, naming model a) -- the fallback did not re-reach A.
#     Guards the stream_once re-sync at jc_agent.c:~762 that the M1207 fix
#     at :~2778 must not disturb.
. "$(dirname "$0")/_smoke.sh"

t_plan 17
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$tmp/capa" "$tmp/capb"

# --- Model A: serves exactly ONE tool call then exits ------------------------
# Rule 1: tool call returns TOOL_CALLED, used once.
# A is started with --max-requests 1, so after one request it exits. The
# follow-up call against A's port is a genuine connect failure (connection
# refused), not a 500. A 500 would mean a reachable server, which the M1038
# fix deliberately does NOT route away from.
cat > "$tmp/a.mm" <<EOF
wire openai
rule
  count 1
  tool write_file {"path":"$tmp/marker","content":"A did tool"}
EOF

# --- Model B: answers with FALLBACK_ANSWER -----------------------------------
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  text FALLBACK_ANSWER
EOF

mm_start "$tmp/a.mm" "$tmp/capa" 1
A_PORT=$MM_PORT
A_PID=$MM_PID
mm_start "$tmp/b.mm" "$tmp/capb"
B_PORT=$MM_PORT

# --- LEG 1: A down, fallback B live (the repro, RED before the fix) ----------
cat > "$tmp/config1.json" <<EOF
{"models":[{"name":"A","provider":"openai","model":"a","apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","fallback":"B"},{"name":"B","provider":"openai","model":"b","apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x"}],"routing":{"enabled":true,"fast":"A","strong":"B"},"snapshots":false,"repoMap":false,"maxRetries":2,"connectTimeout":1,"lowResource":false}
EOF

# A serves one tool call then exits. The model (jichi) then tries to make
# another call to A (which is now dead), and that call fails with connect error.
# The fallback B should be used.
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config1.json" \
      -q --no-session --log "$tmp/telemetry1.jsonl" \
      --log-level metrics -p "run the command, then say done" < /dev/null); rc1=$?

kill "$A_PID" 2>/dev/null
wait "$A_PID" 2>/dev/null
mm_stop

# CHECK 1: rc=0 (the fallback saved the run)
if [ $rc1 -eq 0 ]; then
    t_ok "LEG 1: run completed (exit 0)"
else
    t_fail "LEG 1: rc=$rc1 -- run failed despite fallback"
fi

# CHECK 2: stdout contains FALLBACK_ANSWER
case "$out" in
    *FALLBACK_ANSWER*) t_ok "LEG 1: fallback model's answer reached stdout" ;;
    *) t_fail "LEG 1: fallback answer missing: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# CHECK 3: telemetry has a route event with reason=connect-fallback
if grep -q '"event":"route"' "$tmp/telemetry1.jsonl" 2>/dev/null \
   && grep '"event":"route"' "$tmp/telemetry1.jsonl" 2>/dev/null \
      | grep -q '"reason":"connect-fallback"'; then
    t_ok "LEG 1: telemetry carries a route event with reason=connect-fallback"
else
    t_fail "LEG 1: no route/connect-fallback event in telemetry"
fi

# CHECK 4: telemetry has a model_call row with model B
if grep -q '"model":"B"' "$tmp/telemetry1.jsonl" 2>/dev/null; then
    t_ok "LEG 1: telemetry carries a model_call row for model B"
else
    t_fail "LEG 1: no model_call row for model B in telemetry"
fi

# --- LEG 2: A down, NO fallback (negative) ----------------------------------
mm_start "$tmp/a.mm" "$tmp/capa" 1
A_PORT=$MM_PORT
A_PID=$MM_PID
mm_start "$tmp/b.mm" "$tmp/capb"
B_PORT=$MM_PORT

cat > "$tmp/config2.json" <<EOF
{"models":[{"name":"A","provider":"openai","model":"a","apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x"},{"name":"B","provider":"openai","model":"b","apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x"}],"routing":{"enabled":true,"fast":"A","strong":"B"},"snapshots":false,"repoMap":false,"maxRetries":2,"connectTimeout":1,"lowResource":false}
EOF

# A serves one tool call then exits. The model tries to make another call to
# A (dead port) with no fallback configured. The run fails.
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
      -q --no-session --log "$tmp/telemetry2.jsonl" \
      --log-level metrics -p "run the command, then say done" < /dev/null); rc2=$?

kill "$A_PID" 2>/dev/null
wait "$A_PID" 2>/dev/null
mm_stop

# CHECK 5: rc!=0 (the failure is not masked, no fallback)
if [ $rc2 -ne 0 ]; then
    t_ok "LEG 2: run failed as expected (no fallback)"
else
    t_fail "LEG 2: rc=$rc2 -- run unexpectedly succeeded"
fi

# CHECK 5b: telemetry has NO route event (no fallback configured)
if grep -q '"event":"route"' "$tmp/telemetry2.jsonl" 2>/dev/null; then
    t_fail "LEG 2: telemetry has a route event (should be absent)"
else
    t_ok "LEG 2: no route event in telemetry (as expected)"
fi

# --- LEG 3: A answers with HTTP 401 (behaviour preservation) -----------------
mm_start "$tmp/b.mm" "$tmp/capb"
B_PORT=$MM_PORT

# A responds with status 401 + body (a server that ANSWERS: reachable)
cat > "$tmp/a401.mm" <<'EOF'
wire openai
rule
  status 401
  body {"error":{"message":"unauthorized"}}
EOF
# A401 with no --max-requests so it can serve multiple 401 responses
mm_start "$tmp/a401.mm" "$tmp/capa"
A_PORT=$MM_PORT
A_PID=$MM_PID

cat > "$tmp/config3.json" <<EOF
{"models":[{"name":"A","provider":"openai","model":"a","apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","fallback":"B"},{"name":"B","provider":"openai","model":"b","apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x"}],"routing":{"enabled":true,"fast":"A","strong":"B"},"snapshots":false,"repoMap":false,"maxRetries":2,"connectTimeout":1,"lowResource":false}
EOF

# A answers with HTTP 401 (reachable server, not transient). The run must NOT
# route away; every call stays on A, and the run fails (401 is not transient).
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" \
      -q --no-session --log "$tmp/telemetry3.jsonl" \
      --log-level metrics -p "run the command, then say done" < /dev/null); rc3=$?

kill "$A_PID" 2>/dev/null
wait "$A_PID" 2>/dev/null
mm_stop

# CHECK 6: NO route event, every model_call is model A, rc!=0
if grep -q '"event":"route"' "$tmp/telemetry3.jsonl" 2>/dev/null; then
    t_fail "LEG 3: telemetry has a route event (401 is reachable, must not route)"
else
    t_ok "LEG 3: no route event (HTTP 401 is reachable, must not route)"
fi

# Every model_call row must be model A (no fallback on HTTP status)
if grep -q '"model":"B"' "$tmp/telemetry3.jsonl" 2>/dev/null; then
    t_fail "LEG 3: telemetry has a model_call row for model B (must be A only)"
else
    t_ok "LEG 3: every model_call row is model A (no fallback on status)"
fi

if [ $rc3 -ne 0 ]; then
    t_ok "LEG 3: run failed as expected (401 is not transient)"
else
    t_fail "LEG 3: rc=$rc3 -- run unexpectedly succeeded"
fi

# --- LEG 4: A down, fallback Z doesn't resolve (no-chain) --------------------
mm_start "$tmp/a.mm" "$tmp/capa" 1
A_PORT=$MM_PORT
A_PID=$MM_PID
mm_start "$tmp/b.mm" "$tmp/capb"
B_PORT=$MM_PORT

# A has a fallback Z, but no model is named Z (unresolvable fallback)
cat > "$tmp/config4.json" <<EOF
{"models":[{"name":"A","provider":"openai","model":"a","apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","fallback":"Z"},{"name":"B","provider":"openai","model":"b","apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x"}],"routing":{"enabled":true,"fast":"A","strong":"B"},"snapshots":false,"repoMap":false,"maxRetries":2,"connectTimeout":1,"lowResource":false}
EOF

# A serves one tool call then exits. The follow-up call against A's dead port
# is a connect failure. jc_app_effective_model can't find Z, so it returns the
# original idx (A), and no routing occurs. The run fails after maxRetries.
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config4.json" \
      -q --no-session --log "$tmp/telemetry4.jsonl" \
      --log-level metrics -p "run the command, then say done" < /dev/null); rc4=$?

kill "$A_PID" 2>/dev/null
wait "$A_PID" 2>/dev/null
mm_stop

# CHECK 10: rc!=0 (the run fails after maxRetries)
if [ $rc4 -ne 0 ]; then
    t_ok "LEG 4: run failed as expected (unresolvable fallback)"
else
    t_fail "LEG 4: rc=$rc4 -- run unexpectedly succeeded"
fi

# CHECK 11: telemetry has NO route event (no routing when fallback unresolvable)
if grep -q '"event":"route"' "$tmp/telemetry4.jsonl" 2>/dev/null; then
    t_fail "LEG 4: telemetry has a route event (should be absent)"
else
    t_ok "LEG 4: no route event in telemetry (as expected)"
fi

# CHECK 12: every model_call row is model A (no chain walk)
if grep -q '"model":"B"' "$tmp/telemetry4.jsonl" 2>/dev/null; then
    t_fail "LEG 4: telemetry has a model_call row for model B (must be A only)"
else
    t_ok "LEG 4: every model_call row is model A (no chain walk)"
fi

# --- LEG 5: M1038's in-stream connect-fallback re-route, pinned on captures ----
# (M1207 DM5). LEG 1's shape, but the assertions are on the raw CAPTURES the
# mocks recorded (CAPDIR/req.N), never on telemetry. LEG 1 proves the
# fallback fires; LEG 5 proves WHERE each round actually landed on the wire:
# A served EXACTLY one model round (the tool call), then died; B served
# EXACTLY one model round -- the round AFTER A's tool result, carrying a
# role:tool message. This guards the ONE `prov = app->provider;` re-sync
# inside stream_once (jc_agent.c:~762) that the M1207 fix at :~2778 must not
# disturb. GREEN pre-fix and post-fix by design (it pins a neighbour, not the
# defect).
#
# CAPTURE HYGIENE (measured, M1207): jichi sends a GET /v1/models
# reachability probe (jc_net_reachable) to EVERY configured apiBase, at turn
# start and inside the fallback re-probe. Probes have no chat body and carry
# no "model" field, and they BURN the mock's --max-requests budget like any
# served request. Two consequences this leg's assertions account for:
#   * the count assertions count MODEL ROUNDS (req.* files carrying a
#     POST /v1/chat/completions line), not raw capture files;
#   * A is started with --max-requests 2, not 1: probe (req.1) + the one
#     tool round (req.2). With --max-requests 1 the probe alone would kill A
#     and B would serve the FIRST round (no role:tool) -- the wrong shape.
#   * A's script answers the probe (rule: text, catch-all) and the tool
#     round with `count 2`.
cat > "$tmp/a5.mm" <<EOF
wire openai
rule
  count 2
  tool write_file {"path":"$tmp/marker5","content":"A did tool"}
rule
  text PROBE_ANSWER
EOF

mm_start "$tmp/a5.mm" "$tmp/capA5" 2
A_PORT=$MM_PORT
A_PID=$MM_PID
mm_start "$tmp/b.mm" "$tmp/capB5"
B_PORT=$MM_PORT

cat > "$tmp/config5.json" <<EOF
{"models":[{"name":"A","provider":"openai","model":"a","apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","fallback":"B"},{"name":"B","provider":"openai","model":"b","apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x"}],"routing":{"enabled":true,"fast":"A","strong":"B"},"snapshots":false,"repoMap":false,"maxRetries":2,"connectTimeout":1,"lowResource":false}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config5.json" \
      -q --no-session --log "$tmp/telemetry5.jsonl" \
      --log-level metrics -p "run the command, then say done" < /dev/null); rc5=$?

kill "$A_PID" 2>/dev/null
wait "$A_PID" 2>/dev/null
mm_stop

# CHECK 13: the run completes on B (as in LEG 1)
if [ $rc5 -eq 0 ]; then
    t_ok "LEG 5: run completed on B (exit 0)"
else
    t_fail "LEG 5: rc=$rc5 -- run did not complete on B"
fi

# CHECK 14: B served EXACTLY 1 model round (POST) -- the post-fallback
# round reached B, and no other model round reached B (probes excluded).
nb=$(grep -l 'POST /v1/chat/completions' "$tmp/capB5"/req.* 2>/dev/null | wc -l)
if [ "$nb" -eq 1 ]; then
    t_ok "LEG 5: B served exactly 1 model round"
else
    t_fail "LEG 5: B served $nb model round(s) (expected exactly 1)"
fi

# CHECK 15: that one B round is the round AFTER the tool call A served
# (its body carries a role:tool message) and names B's model id -- the wire
# really reached B's mock, not A's.
if [ "$nb" -eq 1 ] \
   && grep -q '"role":"tool"' "$tmp/capB5"/req.* 2>/dev/null \
   && grep -q '"model":"b"' "$tmp/capB5"/req.* 2>/dev/null; then
    t_ok "LEG 5: B's round is the post-fallback tool round, naming model b"
else
    t_fail "LEG 5: B's round missing role:tool or model b: $(head_bytes 200 "$tmp/capB5/req.2")"
fi

# CHECK 16: A served EXACTLY 1 model round (the tool round) then died --
# the fallback round did NOT re-reach A (probes excluded).
na=$(grep -l 'POST /v1/chat/completions' "$tmp/capA5"/req.* 2>/dev/null | wc -l)
if [ "$na" -eq 1 ]; then
    t_ok "LEG 5: A served exactly 1 model round"
else
    t_fail "LEG 5: A served $na model round(s) (expected exactly 1)"
fi

# CHECK 17: A's round names A's model id (a), not B's (b) -- confirming the
# re-route went A -> B exactly once.
if [ "$na" -eq 1 ] \
   && grep -q '"model":"a"' "$tmp/capA5"/req.* 2>/dev/null \
   && ! grep -q '"model":"b"' "$tmp/capA5"/req.* 2>/dev/null; then
    t_ok "LEG 5: A's round names model a (the fallback round did not re-reach A)"
else
    t_fail "LEG 5: A's round missing model a or naming model b: $(head_bytes 200 "$tmp/capA5/req.2")"
fi

t_done
