#!/bin/sh
# smoke: the history wire-shape validator (M364) stays SILENT on the most
# mutation-heavy real flow jichi has. The fixture is the pressure recipe:
# eight distinct reads under a tiny contextLimit drive the eager dedup, the
# lossy mid-turn elision, the M358 [context] note injection, and a fresh
# streaming placeholder every round -- more history mutators firing in one
# turn than any other driver exercises. If any of them broke pairing or
# boundaries, the validator (checked at the loop chokepoint before every
# request) would warn on stderr and emit a history_check telemetry event;
# both must be ABSENT. The presence half of the M310 pair lives in the unit
# tests, where every corruption class is planted and named
# (tests/test_message.c); the teeth for THIS driver force a spurious
# violation and watch these absence checks go red.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

i=1
while [ "$i" -le 8 ]; do
    j=0
    : > "$ws/subject$i.txt"
    while [ "$j" -lt 60 ]; do
        echo "alpha bravo charlie delta echo foxtrot golf hotel india x$i" \
            >> "$ws/subject$i.txt"
        j=$((j + 1))
    done
    i=$((i + 1))
done

{
    echo "wire openai"
    i=1
    while [ "$i" -le 8 ]; do
        echo "rule"
        echo "  count $i"
        echo "  tool read_file {\"path\":\"subject$i.txt\"}"
        i=$((i + 1))
    done
    echo "rule"
    echo "  text done"
} > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT" '"contextLimit":2500'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --log "$tmp/telem.jsonl" --log-level metrics \
    -p "survey the subjects" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

# --- 1: the mutators actually fired (the denominator) -------------------------
ncomp=$(grep -c '"event":"compact"' "$tmp/telem.jsonl" 2>/dev/null); [ -n "$ncomp" ] || ncomp=0
if [ "$ncomp" -ge 4 ] && grep -q 'done' "$tmp/out"; then
    t_ok "$ncomp compaction passes fired and the run completed"
else
    t_fail "fixture too quiet ($ncomp compact events) -- the check saw no stress"
fi

# --- 2: no wire-shape warning on stderr ----------------------------------------
if ! grep -q '\[history\] wire-shape check' "$tmp/err"; then
    t_ok "no [history] warning across the whole run"
else
    t_fail "the validator flagged a real flow: $(grep 'history' "$tmp/err" | head -1)"
fi

# --- 3: no history_check telemetry event ---------------------------------------
nhc=$(grep -c '"event":"history_check"' "$tmp/telem.jsonl" 2>/dev/null); [ -n "$nhc" ] || nhc=0
if [ "$nhc" -eq 0 ]; then
    t_ok "zero history_check telemetry events"
else
    t_fail "$nhc history_check events -- a mutator broke the contract"
fi

# --- 4: multi-turn TUI flow (notes + inject shapes) stays clean too ------------
{
    echo "wire openai"
    echo "rule"
    echo "  count 1"
    echo '  tool read_file {"path":"subject1.txt"}'
    echo "rule"
    echo "  text ok"
} > "$tmp/replies2.mm"
mm_start "$tmp/replies2.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT"
cat > "$tmp/run.pd" <<'EOF'
delay 1200
send "read subject1.txt\r"
delay 2500
send "and summarize it\r"
delay 2500
send "/exit\r"
waitexit 15
assertexit 0
EOF
(cd "$ws" && with_deadline 60 "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/t.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --auto \
    2> "$tmp/err2" > /dev/null); rc=$?
mm_stop
if [ "$rc" -eq 0 ] && ! grep -q '\[history\] wire-shape check' "$tmp/err2" \
   && ! grep -q '\[history\] wire-shape check' "$tmp/t.log"; then
    t_ok "two-turn TUI session: no wire-shape warning"
else
    t_fail "rc=$rc or a warning appeared in the TUI flow"
fi

t_done
