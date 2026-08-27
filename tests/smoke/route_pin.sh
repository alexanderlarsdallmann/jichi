#!/bin/sh
# smoke: an explicit `--model` PINS the run -- config routing must not override
# an explicit CLI choice (M411).
#
# THE DEFECT THIS EXISTS FOR. Measured while driving jichi on zigodot
# (2026-08-12): `jichi -p "/assign ..." --auto --model zigodot-strong` against a
# config with routing enabled. `status --model zigodot-strong` printed the
# strong model as active -- and the run's first journal event was
# `route ... to: zigodot-fast-coder, reason: turn-start`. The work the operator
# addressed to gemma was silently done by the coder model, and the `[route]`
# stderr line was silenced by -q. Everywhere else in jichi an explicit CLI flag
# beats config; here a config POLICY beat an explicit CLI CHOICE, and `status`
# reported the choice that lost.
#
# Two mock tiers whose wire ids differ; the captured first request's "model"
# field is the ground truth for who actually answered.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$tmp/capf" "$tmp/caps"

# mockmodel numbers requests CUMULATIVELY per instance, so later runs land in
# req.2, req.3, ... -- always inspect the newest capture in a dir.
last_req() {
    ls "$1"/req.* 2>/dev/null | sort -t. -k2 -n | tail -1
}

cat > "$tmp/mock.mm" <<'EOF'
wire openai
rule
  text PINNED_OK
EOF

mm_start "$tmp/mock.mm" "$tmp/capf"
F_PORT=$MM_PORT; F_PID=$MM_PID
mm_start "$tmp/mock.mm" "$tmp/caps"

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"fasttier","provider":"openai","model":"mock-fast",
   "apiBase":"http://127.0.0.1:$F_PORT/v1","apiKey":"x"},
  {"name":"strongtier","provider":"openai","model":"mock-strong",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "routing":{"enabled":true,"fast":"fasttier","strong":"strongtier"},
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

# --- 1+2: --model strongtier answers ON the strong tier, and says why --------
err=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --no-session --model strongtier -p "say the marker" \
      < /dev/null 2>&1 >/dev/null); rc=$?
if [ -n "$(last_req "$tmp/caps")" ] &&
   grep -q '"model":"mock-strong"' "$(last_req "$tmp/caps")" &&
   [ -z "$(last_req "$tmp/capf")" ]; then
    t_ok "--model strongtier: the request went to the strong tier, none to fast"
else
    t_fail "rc=$rc; strong req: $(ls "$tmp/caps" 2>/dev/null | tr '\n' ' '); fast req: $(ls "$tmp/capf" 2>/dev/null | tr '\n' ' ')"
fi
case "$err" in
    *"pins"*) t_ok "stderr says --model pinned the run (routing disabled for it)" ;;
    *) t_fail "no pin note on stderr: $(printf '%s' "$err" | tr '\n' ' ' | head_bytes 140)" ;;
esac

# --- 3: the escape hatch -- explicit routing flags KEEP routing ---------------
# A user who names tiers on the command line wants routing; --model then only
# picks the starting model and the turn-start route to fast still applies.
nf_before=$(ls "$tmp/capf" 2>/dev/null | grep -c .)
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --model strongtier \
      --route-fast fasttier --route-strong strongtier \
      -p "say the marker" < /dev/null >/dev/null 2>&1)
nf_after=$(ls "$tmp/capf" 2>/dev/null | grep -c .)
if [ "$nf_after" -gt "$nf_before" ] &&
   grep -q '"model":"mock-fast"' "$(last_req "$tmp/capf")"; then
    t_ok "--model + explicit --route-fast/--route-strong keeps tiered routing"
else
    t_fail "explicit routing flags did not keep routing (fast reqs: $nf_before -> $nf_after)"
fi

# --- 4: without --model, routing still routes to fast (unchanged default) ----
nf_before=$nf_after
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session -p "say the marker" < /dev/null >/dev/null 2>&1)
nf_after=$(ls "$tmp/capf" 2>/dev/null | grep -c .)
if [ "$nf_after" -gt "$nf_before" ] &&
   grep -q '"model":"mock-fast"' "$(last_req "$tmp/capf")"; then
    t_ok "no --model: turn-start routing to the fast tier is unchanged"
else
    t_fail "default routing broke (fast reqs: $nf_before -> $nf_after)"
fi

kill "$F_PID" 2>/dev/null
wait "$F_PID" 2>/dev/null
mm_stop

t_done
