#!/bin/sh
# smoke: the prompt-cache hit-rate is reported PER SESSION, because the aggregate
# is a trap on any log that spans a server-side change (M592).
#
# THE MEASUREMENT THIS EXISTS FOR. A 2026-08-25 drive on one model, one
# workspace, one day summarised as `hit-rate=8.0%`. Its five sessions were
#
#   03:20  0.0%    08:41  0.0%    10:03  92.4%
#   03:22  0.0%    08:52  0.0%
#
# The deployment's prefix caching was switched on between the fourth and the
# fifth. 8.0% describes no session that ever ran -- it is the average of a
# before and an after, and the "before" is not recoverable from any number the
# summary printed.
#
# jc_telemetry.h already warns about this SHAPE for tool ok-rates: a 34 MB log
# "crossed M168 ... and read as a live defect that had in fact been fixed weeks
# earlier", which is why `--since` exists. The hazard is worse for cache,
# because the thing that changed is a SERVER setting -- it leaves no trace in
# this repository's history at all, so a reader cannot even date it.
#
# Two hypotheses were tested and both were WRONG before this one was right:
# that mid-turn compaction destroys the cached prefix (calls after a compaction
# were not worse), and that concurrent sessions evict each other (the five
# sessions did not overlap in time). See docs/ROADMAP.md M592.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

mkdir -p "$HOME/.jichi.d/telemetry"
log="$HOME/.jichi.d/telemetry/cache.jsonl"

# Session A: nothing cached. Session B: 90% cached. Session C: below the volume
# floor, so no figure may be printed for it at all.
cat > "$log" <<'EOF'
{"event":"model_call","ts":1000,"sid":"aaaaaaaa","turn":1,"model":"m","model_id":"m","ok":true,"in_tok":10000,"cache_read_in":0,"out_tok":10,"latency_ms":1}
{"event":"model_call","ts":1001,"sid":"aaaaaaaa","turn":1,"model":"m","model_id":"m","ok":true,"in_tok":10000,"cache_read_in":0,"out_tok":10,"latency_ms":1}
{"event":"model_call","ts":1002,"sid":"bbbbbbbb","turn":1,"model":"m","model_id":"m","ok":true,"in_tok":1000,"cache_read_in":9000,"out_tok":10,"latency_ms":1}
{"event":"model_call","ts":1003,"sid":"bbbbbbbb","turn":1,"model":"m","model_id":"m","ok":true,"in_tok":1000,"cache_read_in":9000,"out_tok":10,"latency_ms":1}
{"event":"model_call","ts":1004,"sid":"cccccccc","turn":1,"model":"m","model_id":"m","ok":true,"in_tok":100,"cache_read_in":0,"out_tok":10,"latency_ms":1}
EOF

out="$tmp/out.txt"
"$BIN" telemetry > "$out" 2>&1

if grep -q '^Sessions (timeline):' "$out"; then
    t_ok "the report has a Sessions timeline (the fixture was read)"
else
    t_fail "no Sessions section -- the fixture was not read, and every check below would pass on an empty report"
fi

if grep -qE '^  aaaaaaaa .*cache=0%' "$out"; then
    t_ok "a session with nothing cached reads cache=0%"
else
    t_fail "expected aaaaaaaa cache=0%; got: $(grep -E '^  aaaaaaaa' "$out")"
fi

if grep -qE '^  bbbbbbbb .*cache=90%' "$out"; then
    t_ok "a session with 9000 of 10000 cached reads cache=90%"
else
    t_fail "expected bbbbbbbb cache=90%; got: $(grep -E '^  bbbbbbbb' "$out")"
fi

# The two sessions must be judged INDEPENDENTLY. If the code fell back to the
# aggregate, both would print the same number -- 60% for this fixture -- and the
# two checks above would fail together, so this check names the failure mode.
if grep -qE '^  aaaaaaaa .*cache=0%' "$out" && grep -qE '^  bbbbbbbb .*cache=90%' "$out"; then
    t_ok "the two sessions are judged independently, not against one aggregate"
else
    t_fail "the per-session figures collapsed toward one number: $(grep -E '^  (aaaaaaaa|bbbbbbbb)' "$out" | tr '\n' '|')"
fi

# CONTROL: below the volume floor there is no figure to print. A hit-rate over
# 100 tokens is noise, and printing it invites a reader to trust it.
if grep -qE '^  cccccccc .*cache=' "$out"; then
    t_fail "a cache figure was printed for a session with 100 tokens of input: $(grep -E '^  cccccccc' "$out")"
else
    t_ok "a session below the volume floor gets no cache figure at all"
fi

t_done
