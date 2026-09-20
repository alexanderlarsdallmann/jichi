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

# --- 4-5: with lite OFF, the declaration is used and nothing is said ---------
# `--no-lite`, NOT "omit --lite" (2026-09-19). Lite AUTO-ENABLES below the
# resource tier, so on a small board the absence of the flag does not mean the
# normal profile -- it means jichi decided for itself. These two checks
# therefore failed on both Raspberry Pi rows while jichi was behaving exactly as
# LOW_MEMORY.md documents:
#
#     4 core(s), 415 MB RAM -- tier: minimal (lite)
#     not ok 4 - declared window ignored: ... limit ~16384 tokens
#     not ok 5 - warned when lite is not in play
#
# Measured on that board: the same config reads 16384 with one cap warning when
# the flag is omitted, and 196608 with none under `--no-lite`. So the checks were
# measuring the BENCH'S RAM, not the behaviour in their own names. This is the
# third driver in a week with that shape -- parallel_abort pinned
# maxParallelAgents for the same reason, and a unit test asserting
# `jc_cpu_count_known() == 1` broke the FreeBSD row for it. A driver must PIN the
# condition it depends on rather than inherit it from the machine.
err=$(with_deadline 30 "$BIN" --no-lite --config "$tmp/declared.json" context 2>&1 >"$tmp/out2")
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
