#!/bin/sh
# smoke: a mid-turn compaction reports whether it was under PRESSURE, and only
# claims to have fallen short when it was (M326x).
#
# THE DEFECT. jc_compact_midturn runs an eager, zero-loss dedup on every round
# that saw a new read result, then returns early if the high-water trigger has
# not fired. That early return leaves `target` and `reached` at 0 from the
# memset -- and the emitter wrote `short` as `!reached` without consulting
# `pressed`. So every routine dedup logged short:true with target:0, and the
# summarizer rendered "requests went out over the configured contextLimit"
# about requests that had done nothing of the kind.
#
# In a measured 36,925-event workload ALL 19 such events were false positives:
# zero true ones. The metric built to detect this exact failure mode carried no
# information at all -- docs/TEST_INTEGRITY.md fm. 1, an instrument reporting a
# defect in the thing it measures.
#
# What is pinned here is the INVARIANT, not a particular run's numbers: a
# compaction that was not under pressure had no target to miss, so `short` can
# never be true while `pressed` is false. That holds for any workload, which is
# what makes it worth a driver rather than an assertion about one fixture.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# Repeated reads of the SAME file in one turn: each supersedes the last, which
# is what the eager dedup collects -- and it runs well below the high-water, so
# the pass takes the early return. That is the miscounted case.
#
# Two constants decide whether the dedup can fire at all, and the first draft of
# this fixture cleared neither: ELIDE_MIN_BYTES (800) means a small result is
# never elided, and MIDTURN_KEEP_RECENT (6) protects the last six messages. A
# 39-byte file read twice produced no compaction event and the driver tested
# nothing. So: a file over the byte floor, read enough times that the early
# reads age out of the keep-recent window.
i=0
: > "$ws/subject.txt"
while [ "$i" -lt 60 ]; do
    echo "alpha bravo charlie delta echo foxtrot golf hotel india juliet" \
        >> "$ws/subject.txt"
    i=$((i + 1))
done

{
    echo "wire openai"
    i=1
    while [ "$i" -le 8 ]; do
        echo "rule"
        echo "  count $i"
        echo '  tool read_file {"path":"subject.txt"}'
        i=$((i + 1))
    done
    echo "rule"
    echo "  text done"
} > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --log "$tmp/telem.jsonl" --log-level metrics \
    -p "read it repeatedly" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

# --- 1: the run logged telemetry (the denominator) ---------------------------
if [ -s "$tmp/telem.jsonl" ]; then
    t_ok "the run wrote telemetry ($(grep -c . "$tmp/telem.jsonl") events)"
else
    t_fail "no telemetry -- nothing below can be trusted"
fi

# --- 2: a mid-turn compaction happened at all --------------------------------
grep '"event":"compact"' "$tmp/telem.jsonl" > "$tmp/compacts" 2>/dev/null || true
ncomp=$(grep -c . "$tmp/compacts" 2>/dev/null); [ -n "$ncomp" ] || ncomp=0
if [ "$ncomp" -ge 1 ]; then
    t_ok "$ncomp compaction event(s) emitted (the eager dedup fired)"
else
    t_fail "no compaction event -- the dedup never ran, so the invariant is untested"
fi

# --- 3: every compaction carries `pressed` -----------------------------------
# Without the field a reader cannot separate routine housekeeping from a pass
# that was the last thing between the request and the limit -- which is how 44%
# of that workload's mid-turn events came to read as alarm.
missing=0
while IFS= read -r line; do
    p=$(printf '%s' "$line" | "$JQ" .pressed 2>/dev/null)
    [ "$p" = "true" ] || [ "$p" = "false" ] || missing=$((missing + 1))
done < "$tmp/compacts"
if [ "$missing" -eq 0 ] && [ "$ncomp" -ge 1 ]; then
    t_ok "every compaction event reports whether it was under pressure"
else
    t_fail "$missing of $ncomp compaction events carry no \`pressed\` field"
fi

# --- 4: THE INVARIANT --------------------------------------------------------
# short => pressed. A pass that never crossed the high-water had no target.
bad=0
while IFS= read -r line; do
    p=$(printf '%s' "$line" | "$JQ" .pressed 2>/dev/null)
    sh=$(printf '%s' "$line" | "$JQ" .short 2>/dev/null)
    if [ "$sh" = "true" ] && [ "$p" != "true" ]; then
        bad=$((bad + 1))
    fi
done < "$tmp/compacts"
if [ "$bad" -eq 0 ]; then
    t_ok "no compaction claims to have fallen short without being under pressure"
else
    t_fail "$bad event(s) report short:true with pressed:false -- the M323 metric is lying again"
fi


# --- 5-6: a PRESSURED pass, and the thrash signal -----------------------------
# The checks above exercise the unpressured path (the miscounted one). Pressure
# needs the estimate over 80% of the limit, so squeeze contextLimit until the
# same reads cross it. `unrelieved` -- did the pass get back under the
# high-water, or will it re-trigger next round -- only has meaning here.
mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config2.json" "$MM_PORT" '"contextLimit":9000'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" --no-session \
    --auto --log "$tmp/telem2.jsonl" --log-level metrics \
    -p "read it repeatedly" < /dev/null > "$tmp/out2" 2> "$tmp/err2") || true
mm_stop

grep '"event":"compact"' "$tmp/telem2.jsonl" > "$tmp/compacts2" 2>/dev/null || true
npress=0; nunrel=0; nmiss=0
while IFS= read -r line; do
    p=$(printf '%s' "$line" | "$JQ" .pressed 2>/dev/null)
    u=$(printf '%s' "$line" | "$JQ" .unrelieved 2>/dev/null)
    [ "$p" = "true" ] && npress=$((npress + 1))
    [ "$u" = "true" ] && nunrel=$((nunrel + 1))
    [ "$u" = "true" ] || [ "$u" = "false" ] || nmiss=$((nmiss + 1))
done < "$tmp/compacts2"

if [ "$npress" -ge 1 ]; then
    t_ok "a small contextLimit produces $npress pressured pass(es)"
else
    t_fail "no pressured pass even at contextLimit 9000 -- checks 5-6 test nothing"
fi

# `unrelieved` must be REPORTED on every event, true or false. Asserting only
# that it is sometimes true would pass on a build that hard-codes it.
if [ "$nmiss" -eq 0 ] && [ "$npress" -ge 1 ]; then
    t_ok "every compaction reports whether it relieved the pressure ($nunrel unrelieved)"
else
    t_fail "$nmiss compaction event(s) carry no \`unrelieved\` field"
fi

t_done
