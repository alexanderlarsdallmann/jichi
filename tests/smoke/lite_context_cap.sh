#!/bin/sh
# smoke: lite CAPS the context budget, and now SAYS SO.
#
# LOW_MEMORY.md publishes `contextLimit 16384` as part of the lean profile, and
# effective_limit() (jc_compact.c) prefers the top-level limit over the active
# model's contextLength. So under lite a model DECLARING a bigger window is
# budgeted small. The cap is deliberate -- the 965 MB Archos row and the
# Pi-class boards depend on it -- but it was applied in SILENCE, which is a
# DEFAULT outranking an EXPLICIT declaration with nothing said (M458).
#
# Measured on the HRZ gateway's jlu/qwen3.8-27b, whose real window the gateway
# publishes as max_input_tokens 196608: under lite the same conversation reads
# 53% of limit instead of 7%, and jichi would compact toward a target it does
# not need -- the failure jc_agent.c's under-declared-window warning describes.
#
# No mockmodel: `context` resolves the budget and prints it without a call.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)
smoke_home "$tmp/home"

decl='"contextLength": 196608'
mdl='"provider": "openai", "model": "jlu/qwen3.8-27b", "apiKey": "k"'
printf '{ "model": { %s, %s } }\n' "$mdl" "$decl" > "$tmp/declared.json"
printf '{ "contextLimit": 200000, "model": { %s, %s } }\n' "$mdl" "$decl" > "$tmp/explicit.json"

# --- 1-2: under lite the cap applies, and it is stated ------------------------
err=$(with_deadline 30 "$BIN" --lite --config "$tmp/declared.json" context 2>&1 >"$tmp/out1")
if grep -q "limit ~16384 tokens" "$tmp/out1"; then
    t_ok "lite budgets the capped 16384, not the declared 196608"
else
    t_fail "lite did not cap: $(grep -i 'limit' "$tmp/out1" | head -1)"
fi
if printf '%s' "$err" | grep -q "lite caps the context budget at 16384" &&
   printf '%s' "$err" | grep -q "declares contextLength 196608"; then
    t_ok "the cap is AUDIBLE and names both numbers"
else
    t_fail "silent cap (M458): $(printf '%s' "$err" | tail -c 200)"
fi

# --- 3: an explicit contextLimit is the operator's choice -- stay quiet -------
err=$(with_deadline 30 "$BIN" --lite --config "$tmp/explicit.json" context 2>&1 >/dev/null)
if printf '%s' "$err" | grep -q "lite caps the context budget"; then
    t_fail "warned about a contextLimit the operator set themselves"
else
    t_ok "an explicit contextLimit is not warned about"
fi

# --- 4-5: without lite, the declaration is used and nothing is said ----------
err=$(with_deadline 30 "$BIN" --config "$tmp/declared.json" context 2>&1 >"$tmp/out2")
if grep -q "limit ~196608 tokens" "$tmp/out2"; then
    t_ok "without lite the declared 196608 is budgeted"
else
    t_fail "declared window ignored: $(grep -i 'limit' "$tmp/out2" | head -1)"
fi
if printf '%s' "$err" | grep -q "lite caps the context budget"; then
    t_fail "warned when lite is not in play"
else
    t_ok "no warning when lite is off"
fi

t_done
