#!/bin/sh
# smoke: the context gauge (M358) -- the one resource the notice family never
# metered was the model's own window. Static half: an Environment line states
# the CONFIGURED context window (never the built-in default -- the M355
# armed-only rule). Dynamic half: the first PRESSED mid-turn pass injects ONE
# [context] user-role note built from the pass's own numbers, so the model is
# told to change how it reads instead of deducing it from elision markers.
#
# The pressure recipe: distinct ~4KB files read in sequence (distinct, so the
# eager zero-loss dedup cannot relieve the pressure the driver needs), under a
# contextLimit small enough that the high-water fires mid-turn. Proof rides
# --dump-requests (M341): the note must be IN a later request body, absent
# from the first, and appear EXACTLY ONCE in the last (once-per-run throttle;
# count-not-absence, since the note legitimately persists in history).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Eight distinct 4KB subjects.
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

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT" '"contextLimit":2500'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --log "$tmp/telem.jsonl" --log-level metrics \
    --dump-requests "$tmp/reqs" \
    -p "survey the subjects" < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

first=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | head -1)
last=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | tail -1)
nreq=$(ls "$tmp/reqs"/req-*.json 2>/dev/null | wc -l)

# --- 1: the run produced enough requests to cross the high-water -------------
if [ -n "$first" ] && [ -n "$last" ] && [ "$nreq" -ge 4 ]; then
    t_ok "$nreq request bodies captured"
else
    t_fail "too few captured requests ($nreq) -- pressure never had a chance"
fi

# --- 2: the static half is in every request's system prompt ------------------
if grep -q 'Context window: ~2500 tokens' "$first"; then
    t_ok "system prompt states the configured window (static half)"
else
    t_fail "no 'Context window' line in the first request"
fi

# --- 3: the note is ABSENT before pressure -----------------------------------
n0=$(grep -o '\[context\] this turn has used' "$first" | wc -l)
if [ "$n0" -eq 0 ]; then
    t_ok "first request carries no [context] note (fires only under pressure)"
else
    t_fail "the note appeared before any pressure ($n0 in request 1)"
fi

# --- 4: the note lands EXACTLY ONCE (once-per-run, count-not-absence) --------
n1=$(grep -o '\[context\] this turn has used' "$last" | wc -l)
if [ "$n1" -eq 1 ]; then
    t_ok "last request carries the [context] note exactly once"
else
    t_fail "expected exactly 1 note in the last request, found $n1"
fi

# --- 5: the note carries the reading guidance, not just the number -----------
if grep -q 'offset/limit' "$last" && grep -q 'search_code' "$last"; then
    t_ok "the note names the reading habit (offset/limit + search_code)"
else
    t_fail "guidance text missing from the injected note"
fi

# --- 6: telemetry joins 'the model was told' to the pass that told it --------
noticed=$(grep '"event":"compact"' "$tmp/telem.jsonl" 2>/dev/null \
          | grep -c '"noticed":true')
if [ "$noticed" -eq 1 ]; then
    t_ok "exactly one compact event carries noticed:true"
else
    t_fail "expected 1 noticed compact event, found $noticed"
fi

# --- 7: the static half obeys the M355 rule: no configured limit, no line ----
write_config "$tmp/config2.json" "1"
sys=$( (cd "$ws" && "$BIN" --config "$tmp/config2.json" sysmsg < /dev/null \
        2>/dev/null) || true)
if printf '%s' "$sys" | grep -q 'Context window:'; then
    t_fail "sysmsg states a window nobody configured (built-in default leak)"
else
    t_ok "no configured limit => no Context window line (facts only)"
fi

t_done
