#!/bin/sh
# smoke: when mid-turn compaction falls short AND the server is accepting
# oversized requests, say which of the two is the operator's problem (M459).
#
# THE DEFECT THIS EXISTS FOR. M323's short-fall warning tells the operator that
# "this history is many small tool results rather than a few large ones, so
# there is little left to elide". That describes the history's SHAPE correctly
# and can point at the wrong remedy.
#
# Measured on a real run: contextLength was declared 32000 while the server had
# been ACCEPTING ~160k-token requests all along -- its real max_model_len was
# 256000. jichi compacted seven times toward a target it never needed to reach,
# relieved nothing (7 of 7 `unrelieved`), and advised shrinking tool output when
# the fix was one config number.
#
# An under-declared window is the ordinary case rather than an exotic one:
# doctor's own text says jichi assumes ~32000 when contextLength is absent, and
# a local server typically declares nothing.
#
# The evidence needs no new measurement. `last_prompt_tokens` is what the SERVER
# counted for a request it ACCEPTED, so a request larger than the declared limit
# that did NOT fail proves the limit understates the model. This driver scripts
# exactly that: a tiny contextLimit, many small tool results so eliding cannot
# relieve anything, and a mock reporting a prompt_tokens far above the limit on
# a request it served happily.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Many small tool results: the shape that leaves compaction nothing to elide.
# `usage 90000 5` is the load-bearing part -- the mock SERVES the request and
# reports 90k prompt tokens for it, which is the proof the declared limit is low.
{
    echo "wire openai"
    i=1
    while [ "$i" -le 6 ]; do
        echo "rule"
        echo "  count $i"
        echo "  usage 90000 5"
        echo "  tool read_file {\"path\":\"f$i.txt\"}"
        i=$((i + 1))
    done
    echo "rule"
    echo "  usage 90000 5"
    echo "  text DONE"
} > "$tmp/replies.mm"

i=1
while [ "$i" -le 6 ]; do
    printf 'some small content for file %s\n' "$i" > "$ws/f$i.txt"
    i=$((i + 1))
done

mm_start "$tmp/replies.mm" "$tmp/cap" 10
# contextLimit deliberately tiny: this is the under-declaration being modelled.
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"],
"contextLength":600}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p "read the files" < /dev/null \
    > "$tmp/out" 2> "$tmp/err") || true
mm_stop

# --- 1: the run happened -------------------------------------------------------
if [ -s "$tmp/err" ] || [ -s "$tmp/out" ]; then
    t_ok "the run produced output"
else
    t_fail "no output at all"
fi

# --- 2: compaction actually fell short (the case is live) ----------------------
# Without this the hint could be absent for the boring reason that the condition
# never arose, and checks 3-4 would pass vacuously.
if grep -q "mid-turn cannot reach its target" "$tmp/err"; then
    t_ok "mid-turn compaction fell short (the M323 condition is live)"
else
    t_fail "compaction never fell short -- this driver would test nothing: \
$(grep -c compact "$tmp/err" 2>/dev/null) compact line(s)"
fi

# --- 3: THE DEFECT -- the likelier cause is named ------------------------------
if grep -q "server ACCEPTED" "$tmp/err"; then
    t_ok "the operator is told the server accepted an oversized request"
else
    t_fail "the short-fall was blamed on the history alone, though the server \
had just served a 90k-token request against a 600-token declared limit"
fi

# --- 4: ...and it names the lever, not just the cause --------------------------
# A cause with no way forward is the M342 message class this project avoids.
if grep -q "contextLength" "$tmp/err"; then
    t_ok "the hint names contextLength, the thing to change"
else
    t_fail "the hint does not say what to change: \
$(grep -o 'server ACCEPTED.\{0,80\}' "$tmp/err" | head -1)"
fi

# --- 5: it stays quiet when the limit is NOT under-declared --------------------
# The hint must be evidence-gated, not printed beside every short-fall. Same
# mock and same tiny limit, but the server now reports a SMALL prompt_tokens --
# so there is no evidence the window is bigger, and jichi must not claim it is.
sed 's/usage 90000 5/usage 40 5/' "$tmp/replies.mm" > "$tmp/replies2.mm"
mm_start "$tmp/replies2.mm" "$tmp/cap2" 10
cat > "$tmp/config2.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"],
"contextLength":600}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config2.json" \
    --no-session --auto -p "read the files" < /dev/null \
    > "$tmp/out2" 2> "$tmp/err2") || true
mm_stop

if ! grep -q "server ACCEPTED" "$tmp/err2"; then
    t_ok "no evidence, no claim: the hint is silent on small real requests"
else
    t_fail "the hint fired without evidence -- the server reported 40 tokens"
fi

# --- 6: THE EVIDENCE ARRIVES AFTER THE WARNING LATCHES (M494) ------------------
# Everything above scripts the easy shape: the mock reports 90k on EVERY call, so
# the oversized request is also the latest one at the instant the short-fall
# warning fires. That cannot distinguish "reads the latest sample" from "reads the
# high-water mark", and the real runs do not look like it.
#
# Measured by dogfooding jichi against the HRZ gateway (M494): 60 model calls, the
# FIRST of them 11,598 accepted tokens against a declared 32000 -- under the limit,
# so correctly nothing to say -- then 36 short-falling compactions of which 35
# relieved nothing, and 9 later calls the server ACCEPTED at up to 32,802 tokens.
# The notice printed **0 times**, because it lived inside the once-per-turn
# `warned_short` latch: by the time the evidence existed, its only opportunity had
# been spent. The operator was told to shrink tool output when the fix was one
# config number -- the exact false remedy this check exists to prevent.
#
# So: small first, oversized after. Both halves of the fix are needed for this to
# pass -- a high-water mark (the max, not the latest) AND a separate latch (so the
# notice is still reachable once warned_short has fired).
{
    echo "wire openai"
    echo "rule"
    echo "  count 1"
    echo "  usage 500 5"                 # under the limit: no evidence yet
    echo "  tool read_file {\"path\":\"f1.txt\"}"
    i=2
    while [ "$i" -le 6 ]; do
        echo "rule"
        echo "  count $i"
        echo "  usage 90000 5"           # the server ACCEPTS these, after the latch
        echo "  tool read_file {\"path\":\"f$i.txt\"}"
        i=$((i + 1))
    done
    echo "rule"
    echo "  usage 90000 5"
    echo "  text DONE"
} > "$tmp/late.mm"

mm_start "$tmp/late.mm" "$tmp/cap_late" 10
cat > "$tmp/late.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"],
"contextLength":600}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/late.json" \
    --no-session --auto -p "read the files" < /dev/null \
    > "$tmp/late.out" 2> "$tmp/late.err") || true
mm_stop

if grep -q "mid-turn cannot reach its target" "$tmp/late.err" &&
   grep -q "server ACCEPTED" "$tmp/late.err"; then
    t_ok "the notice still fires when the evidence arrives AFTER the warning latches"
else
    t_fail "evidence after the latch was never reported: short-fall=$(grep -c 'cannot reach its target' "$tmp/late.err") \
accepted-notice=$(grep -c 'server ACCEPTED' "$tmp/late.err") -- the operator keeps \
the false remedy (shrink tool output) for a limit the server has already disproved"
fi

t_done
