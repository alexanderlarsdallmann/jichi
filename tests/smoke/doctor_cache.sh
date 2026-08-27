#!/bin/sh
# smoke: doctor reports whether the backend is actually caching the prompt
# (M326w), and agrees with `telemetry --cache-audit` about it.
#
# WHY THIS EXISTS. The audit has answered this since M104 and answered it well
# -- but nothing told anyone to run it. A downstream workload measured 0% over
# 1.24 BILLION input tokens; the absence had been repeated in three anecdotes
# and tested in none. On a cacheless backend the fixed prefix (system prompt +
# tool definitions, ~12,600 tokens there) is re-sent on every call, and a turn
# makes tens of them, so this is the single largest structural cost -- and it
# was invisible unless you already knew the flag.
#
# The third check is the one with teeth for the REFACTOR: doctor and the audit
# must reach the same verdict from the same arithmetic. jc_cacheaudit_totals was
# factored out of the renderer precisely so a second summing loop could not
# appear in main.c and drift -- the failure M310-M313 each had to fix. A check
# that only asserted "doctor warns" would pass while the two disagreed.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A log is scoped to a workspace by its `ws` field, so it must name $ws for
# doctor (run from there) to count it at all.
write_log() {                      # write_log PATH NCALLS CACHE_READ_PER_CALL
    _n=$2; _cr=$3; _i=0
    : > "$1"
    while [ "$_i" -lt "$_n" ]; do
        printf '{"v":1,"ts":%d,"sid":"s%d","ws":"%s","seq":%d,"event":"model_call","depth":0,"turn":1,"model":"m","model_id":"m","ok":true,"status":200,"latency_ms":100,"in_tok":50000,"out_tok":50,"cache_read_in":%d,"cache_write_in":0,"sys_tok":8000,"tools_tok":4600,"hist_tok":37400}\n' \
            $((1786000000 + _i)) "$_i" "$ws" "$_i" "$_cr" >> "$1"
        _i=$((_i + 1))
    done
}

doctor_in_ws() {                   # doctor_in_ws LOGDIR -> $tmp/doc
    HOME="$1" sh -c "cd '$ws' && '$BIN' doctor" < /dev/null > "$tmp/doc" 2>&1
}

# --- cacheless backend --------------------------------------------------------
h1=$(smoke_tmp); mkdir -p "$h1/.jichi.d/telemetry"
write_log "$h1/.jichi.d/telemetry/t.jsonl" 6 0
doctor_in_ws "$h1"

if grep -q 'not caching the prompt' "$tmp/doc"; then
    t_ok "a cacheless backend is reported"
else
    t_fail "no cache warning: $(grep -i cache "$tmp/doc" | head -1)"
fi

# The number that makes it concrete. Without it the warning is a fact with no
# size, and the reader cannot tell whether to act.
if grep -q 'fixed prefix' "$tmp/doc" && grep -q '12600' "$tmp/doc"; then
    t_ok "the warning names the fixed prefix that is re-sent (12600 tokens)"
else
    t_fail "prefix cost missing: $(grep -i 'not caching' -A2 "$tmp/doc" | head -3)"
fi

# --- and it agrees with the audit on the same log -----------------------------
with_deadline 30 env HOME="$h1" "$BIN" telemetry --cache-audit \
    "$h1/.jichi.d/telemetry/t.jsonl" < /dev/null > "$tmp/audit" 2>&1

# Compare the two surfaces to EACH OTHER, not each to a constant. The first
# draft grepped both outputs for a hard-coded 300000 -- and passed with the
# shared totals deliberately doubled, because the audit prints the same number
# on a per-model line too, so the grep matched a line other than the one it
# named (docs/TEST_INTEGRITY.md fm. 9, in a driver written to prevent drift).
# Anchored on `overall:` and on doctor's own phrasing, this cannot.
a_tok=$(sed -n 's/^overall: .* over \([0-9][0-9]*\) input tokens.*/\1/p' "$tmp/audit" | head -1)
d_tok=$(sed -n 's/.*of \([0-9][0-9]*\) input tokens over .* calls.*/\1/p' "$tmp/doc" | head -1)
if [ -n "$a_tok" ] && [ -n "$d_tok" ] && [ "$a_tok" = "$d_tok" ]; then
    t_ok "doctor and the audit agree on the volume ($a_tok tokens, one summing loop)"
else
    t_fail "doctor says '$d_tok', the audit says '$a_tok' -- the totals have drifted apart"
fi

# --- a caching backend is NOT warned about ------------------------------------
# The paired presence/absence: check 1 alone passes on a build that warns
# unconditionally, which would train everyone to ignore the line.
h2=$(smoke_tmp); mkdir -p "$h2/.jichi.d/telemetry"
write_log "$h2/.jichi.d/telemetry/t.jsonl" 6 45000
doctor_in_ws "$h2"
if grep -q 'not caching the prompt' "$tmp/doc"; then
    t_fail "a 90%-hit-rate backend is still reported as not caching"
else
    t_ok "a caching backend produces no warning"
fi

# --- doctor still exits on posture, not on cost -------------------------------
# A cacheless backend is a COST, not a broken posture, so it must not turn
# --unattended red: a loop supervisor gating on the exit code would refuse to
# run at all against a perfectly working cacheless endpoint.
HOME="$h1" sh -c "cd '$ws' && '$BIN' doctor --unattended" < /dev/null \
    > "$tmp/un" 2>&1
if grep -q 'not caching the prompt' "$tmp/un" \
   && ! grep -qE '^✗ backend is not caching|^x backend is not caching' "$tmp/un"; then
    t_ok "--unattended reports it without escalating it to a failure"
else
    t_fail "cache cost escalated under --unattended: $(grep -i 'caching' "$tmp/un" | head -1)"
fi

t_done
