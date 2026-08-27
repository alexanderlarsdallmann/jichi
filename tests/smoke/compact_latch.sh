#!/bin/sh
# smoke: the mid-turn exhaustion latch (M361). A pressured history of MANY
# SMALL results gives the lossy trims nothing to elide (all under the 800-byte
# floor, the rest keep-recent protected) -- the M321 workload shape where 174
# of 593 pressured passes ran 26th-or-later in their turn reclaiming zero.
# After one dry pressed pass the latch skips the lossy scans until the exact
# length at which the sliding window could release a candidate (bounded: at
# most keep+1 appends), so telemetry shows pressed rounds carrying
# latched:true between full scans. Distinct paths on purpose: same-path reads
# would be relieved by the eager dedup, which is not under test.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

i=1
while [ "$i" -le 12 ]; do
    j=0
    : > "$ws/s$i.txt"
    while [ "$j" -lt 8 ]; do
        echo "alpha bravo charlie delta echo foxtrot golf x$i" >> "$ws/s$i.txt"
        j=$((j + 1))
    done
    i=$((i + 1))
done

{
    echo "wire openai"
    i=1
    while [ "$i" -le 12 ]; do
        echo "rule"
        echo "  count $i"
        echo "  tool read_file {\"path\":\"s$i.txt\"}"
        i=$((i + 1))
    done
    echo "rule"
    echo "  text done"
} > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"contextLimit":2500'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --log "$tmp/telem.jsonl" --log-level metrics \
    -p "survey the subjects" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

grep '"event":"compact"' "$tmp/telem.jsonl" 2>/dev/null > "$tmp/compacts" || true
npress=$(grep -c '"pressed":true' "$tmp/compacts"); [ -n "$npress" ] || npress=0
nlatch=$(grep -c '"latched":true' "$tmp/compacts"); [ -n "$nlatch" ] || nlatch=0
nscan=$(grep '"pressed":true' "$tmp/compacts" | grep -cv '"latched":true')
[ -n "$nscan" ] || nscan=0

# --- 1: the workload pressed repeatedly (the denominator) ---------------------
if [ "$npress" -ge 4 ]; then
    t_ok "$npress pressed mid-turn passes (the thrash shape reproduced)"
else
    t_fail "only $npress pressed passes -- the fixture never thrashed"
fi

# --- 2: the latch fired -------------------------------------------------------
if [ "$nlatch" -ge 1 ]; then
    t_ok "$nlatch pressed pass(es) skipped the lossy scans (latched:true)"
else
    t_fail "no latched pass in $npress pressed events"
fi

# --- 3: full scans still happen at the re-arm boundary ------------------------
# The latch is a pause, never a stop: at least the latch-setting scan ran, and
# every latch expires within keep+1 appends.
if [ "$nscan" -ge 1 ] && [ "$nscan" -lt "$npress" ]; then
    t_ok "$nscan full scan(s) among $npress pressed passes (bounded, not zero)"
else
    t_fail "scan/latch split wrong: $nscan scans of $npress pressed"
fi

# --- 4: the run completed -----------------------------------------------------
if grep -q 'done' "$tmp/out"; then
    t_ok "the run completed under the latch"
else
    t_fail "run did not finish: $(head_bytes 150 "$tmp/err")"
fi

t_done
