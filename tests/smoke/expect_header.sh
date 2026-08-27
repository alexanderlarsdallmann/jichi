#!/bin/sh
# smoke: a request body over 1 KB must NOT carry Expect: 100-continue (M273).
# libcurl adds that header past a 1 KB body and then waits up to a second for
# a "100 Continue" many model servers never send -- llama.cpp, LocalAI, a
# simple proxy, or this tier's own mockmodel. jichi suppresses it (an empty
# "Expect:" in the header list) so every supported libcurl behaves like the
# newest one.
#
# Read the two checks together: check 1 establishes the PREMISE (the body
# really did cross curl's threshold, so an absent header means something) and
# check 2 is the assertion. On a modern libcurl (8.x) check 2 passes without
# the fix, because that version does not send the header anyway -- this driver
# earns its keep on an OLD libcurl, which is exactly where the smoke tier
# runs (on-target). Shown red on the V2f guest (Debian 9, libcurl 7.52):
# without the suppression the captured request carries the header and this
# driver fails; with it, green.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text EXPECT_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# A prompt comfortably past curl's 1 KB Expect threshold.
big=$(printf '%*s' 3000 '' | tr ' ' b)
(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    -q --no-session --no-stdin -p "$big" < /dev/null > /dev/null 2>&1)
mm_stop

blen=$(wc -c < "$tmp/req.1" 2>/dev/null || echo 0)
if [ "$blen" -gt 1024 ]; then
    t_ok "the captured request is ${blen} B -- past curl's 1 KB Expect threshold"
else
    t_fail "request too small (${blen} B) -- the premise fails, so check 2 would be vacuous"
fi

if grep -qi '^Expect:[[:space:]]*100-continue' "$tmp/req.1" 2>/dev/null; then
    t_fail "request carries Expect: 100-continue -- every call pays curl's 1s wait"
else
    t_ok "no Expect: 100-continue in the request"
fi

t_done
