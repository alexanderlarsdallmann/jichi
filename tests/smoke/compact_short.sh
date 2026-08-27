#!/bin/sh
# smoke: a mid-turn compaction that cannot reach its target says so (M323).
#
# Measured from a 34,216-event workload
# (docs/analysis/2026-08-06-large-workload-telemetry.md): 1,038 mid-turn
# compactions ran and 3.1% of calls STILL exceeded the configured contextLimit,
# up to 1.36x the model's declared window. The pass's lever is LARGE tool
# results and that history was thousands of small ones (p99 output 14 KB, one
# result above 100 KB in 13,783 calls), so there was nothing to elide.
#
# None of that was visible: the `compact` event recorded only elided/dup/age,
# and -- worse -- was emitted ONLY when something was elided, so the case that
# matters most produced no event at all.
#
# Reproduced in miniature here: a 900-token contextLimit and a tiny file, so the
# pass fires every round, elides nothing, and the request stays over budget.
#
# This is the OBSERVABILITY half only. What jichi should DO when it cannot reach
# the target (drop whole messages? summarize mid-turn? refuse?) is deliberately
# still open -- see docs/DEFERRED.md. So these checks assert that jichi SAYS so,
# not that it fixes it.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'a small line\n' > "$ws/a.txt"

cat > "$tmp/loop.mm" <<'EOF'
wire openai
rule
  tool read_file {"path":"a.txt"}
EOF
mm_start_unbounded "$tmp/loop.mm" "$tmp/cap"
write_config "$tmp/c.json" "$MM_PORT" '"maxToolIters":12,"contextLimit":900'

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session --log "$tmp/t.jsonl" --log-level metrics -p "loop" \
    < /dev/null) >/dev/null 2>"$tmp/err"
mm_stop

if [ ! -s "$tmp/t.jsonl" ]; then
    t_fail "no telemetry written -- nothing below can mean anything"
    t_done
fi

n_compact=$(grep -c '"event":"compact"' "$tmp/t.jsonl" 2>/dev/null || true)
[ -n "$n_compact" ] || n_compact=0
if [ "$n_compact" -ge 2 ]; then
    t_ok "the mid-turn pass ran repeatedly ($n_compact events)"
else
    t_fail "only $n_compact compact event(s) -- the pressure case is not exercised"
fi

# THE point: an event exists even though nothing was elided. Before M323 the
# emit was gated on elided > 0, so this case was entirely silent.
if grep '"event":"compact"' "$tmp/t.jsonl" | grep -q '"elided":0'; then
    t_ok "an event is emitted even when the pass elided nothing"
else
    t_fail "no zero-elision event -- the silent case is still silent"
fi

if grep '"event":"compact"' "$tmp/t.jsonl" | grep -q '"short":true'; then
    t_ok "the short-fall is flagged on the event"
else
    t_fail "no \"short\":true -- a log cannot tell 'worked' from 'gave up'"
fi

# before/after/target/limit must all be present and in calibrated real tokens,
# so a reader can check the decision rather than trust it.
line=$(grep '"event":"compact"' "$tmp/t.jsonl" | head -1)
ok=1
for f in before after target limit; do
    case "$line" in *"\"$f\":"*) ;; *) ok=0 ;; esac
done
if [ "$ok" = 1 ]; then
    t_ok "the event carries before/after/target/limit"
else
    t_fail "missing a budget field: $(printf '%s' "$line" | head_bytes 160)"
fi

# Warned ONCE for the turn, not once per round: the condition persists for every
# remaining round, so per-round warnings would bury the one fact that matters.
w=$(grep -c "cannot reach its target" "$tmp/err" 2>/dev/null || true)
[ -n "$w" ] || w=0
if [ "$w" = "1" ]; then
    t_ok "warned exactly once for the turn (not once per round)"
else
    t_fail "warned $w times for one turn ($n_compact rounds) -- throttle broken"
fi

# And the summarizer says it in one line, so the next such analysis is a command
# rather than an afternoon of jq.
if "$BIN" telemetry "$tmp/t.jsonl" 2>/dev/null | grep -q "Compaction SHORT"; then
    t_ok "the telemetry summary reports the short-fall"
else
    t_fail "the summary is silent about a short-fall"
fi

t_done
