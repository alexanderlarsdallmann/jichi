#!/bin/sh
# smoke: `doctor` compares the stall timeout against the latency already on disk (M589).
#
# THE DEFECT, and it cost a real run. jichi aborts a model stream that sends
# nothing for `timeouts.stall` seconds -- 30 by default. Measured 2026-08-25 on a
# local 9B: mean 14.3s per call, MAX 228s. The project driving it set no
# `timeouts` block at all, so the 30s default applied and a call in the latency
# tail was killed ten minutes into an autonomous run:
# `error: model stalled (timed out)`.
#
# The number that predicted the failure was in jichi's OWN telemetry, written the
# night before, and nothing compared the two. This is M584's lesson in a third
# place: the measurement was recorded and unread.
#
# WHAT IS CHECKED, and what is deliberately not:
#
#   checked      -- the denominator (2): doctor ran and produced rows at all,
#                   because every assertion below reads one line out of its
#                   output and an absence holds trivially in an empty capture.
#                   The M567 driver next door earned that check with two vacuous
#                   passes.
#                -- a slow model against a short timeout WARNS, naming both
#                   numbers and the flag that fixes it (3, 4).
#                -- CONTROL: the same log against a timeout that clears the
#                   slowest call reports OK, not a warning (5).
#                -- CONTROL: a model with too FEW calls is not warned about (6),
#                   because one slow call is not a tail. That floor is
#                   JC_DOCTOR_LAT_MIN_CALLS, and without this check the warning
#                   would fire on any log containing a single slow request.
#
#   NOT checked  -- that the timeout is WELL chosen. Only that jichi has stopped
#                   keeping the two numbers in separate rooms.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
# jc_app_load_telemetry reads the NEWEST jsonl under the state dir -- not the
# config's `logging.path` -- and then FILTERS BY THE WORKSPACE ROOT jichi
# resolved. Both matter: a log written anywhere else is invisible, and a `ws`
# stamp that does not match the canonicalized root filters to zero events, which
# the loader reports the same as "no log at all". So the fixture writes into the
# isolated HOME and stamps the root jichi itself prints.
TDIR="$HOME/.jichi.d/telemetry"
mkdir -p "$TDIR"

# A telemetry log with a model that answered slowly. `latency_ms` is the field
# the summariser sums into lat_max; 6 calls clears the >= 5 floor. Written out
# literally so the expected max can be read off the file by eye.
mklog() {   # mklog FILE N MAXMS
    : > "$1"; i=0
    while [ "$i" -lt "$2" ]; do
        i=$((i + 1))
        ms=1200
        [ "$i" -eq 1 ] && ms="$3"
        printf '{"v":1,"ts":178760000%d,"sid":"aaaa","ws":"%s","seq":%d,' "$i" "$ws" "$i" >> "$1"
        printf '"event":"model_call","depth":0,"turn":1,"model":"slowmodel",' >> "$1"
        printf '"status":200,"ok":true,"latency_ms":%s,"in_tok":100,"out_tok":10}\n' "$ms" >> "$1"
    done
}

# `lowResource: false` is pinned as smoke_lint requires of every inline driver
# config, and the reason bites here: auto-lite reshapes the configuration on a
# low-RAM host and could change which doctor rows appear.
mkcfg() {   # mkcfg FILE [STALL]
    if [ -n "$2" ]; then
        cat > "$1" <<EOF
{ "lowResource": false, "timeouts": { "stall": $2 },
  "models": [ { "name": "chat", "provider": "openai", "model": "slowmodel",
  "apiBase": "http://127.0.0.1:1/v1", "apiKeyEnv": "JICHI_API_KEY",
  "roles": ["chat"] } ] }
EOF
    else
        cat > "$1" <<EOF
{ "lowResource": false,
  "models": [ { "name": "chat", "provider": "openai", "model": "slowmodel",
  "apiBase": "http://127.0.0.1:1/v1", "apiKeyEnv": "JICHI_API_KEY",
  "roles": ["chat"] } ] }
EOF
    fi
}

# --- 1: the fixture is what the checks assume -------------------------------
mklog "$TDIR/t.jsonl" 6 228000
n=$($G -c '"event":"model_call"' "$TDIR/t.jsonl")
if [ "$n" = "6" ] && $G -q '228000' "$TDIR/t.jsonl"; then
    t_ok "fixture: 6 model calls, slowest 228000 ms"
else
    t_fail "the fixture is not what the checks below assume ($n calls) -- every
   assertion here reads it, so a broken fixture is a silent pass."
fi

# --- 2: the denominator -----------------------------------------------------
mkcfg "$tmp/short.json" 30
(cd "$ws" && "$BIN" --config "$tmp/short.json" doctor < /dev/null) \
    > "$tmp/out.short" 2>&1
rows=$($G -c '^[!x✓v]' "$tmp/out.short" 2>/dev/null || echo 0)
if [ "${rows:-0}" -ge 3 ]; then
    t_ok "doctor produced $rows row(s) -- the checks below have something to read"
else
    t_fail "doctor produced almost no output, so every assertion below holds
   trivially. rc/output:
$(head_bytes 300 < "$tmp/out.short")"
fi

# --- 3+4: a slow model against a short timeout WARNS, with both numbers ------
if $G -qi 'stall timeout is 30s' "$tmp/out.short" &&
   $G -qi '228s' "$tmp/out.short"; then
    t_ok "a 30s stall timeout against a 228s call warns, naming both numbers"
else
    t_fail "doctor did not compare the stall timeout with the measured latency.
   Both numbers are on disk -- the timeout in the config, the max in telemetry --
   and keeping them in separate rooms is what killed a run mid-flight. Saw:
$($G -i stall "$tmp/out.short" | head_bytes 300)"
fi
if $G -q 'timeout-stall' "$tmp/out.short"; then
    t_ok "the warning names the flag that fixes it"
else
    t_fail "the warning states a cause and no remedy -- the message class that
   amplifies retry loops (M342/M360). It must name --timeout-stall."
fi

# --- 5: CONTROL -- a timeout that clears the slowest call is OK, not a warning
mkcfg "$tmp/long.json" 600
(cd "$ws" && "$BIN" --config "$tmp/long.json" doctor < /dev/null) \
    > "$tmp/out.long" 2>&1
if $G -q 'clears the slowest measured call' "$tmp/out.long" &&
   ! $G -qi 'stall timeout is 600s, but' "$tmp/out.long"; then
    t_ok "control: a 600s timeout over the same log reports OK, not a warning"
else
    t_fail "a stall timeout that clears the measured maximum must not warn, or
   the check is unconditional and checks 3-4 prove nothing. Saw:
$($G -i stall "$tmp/out.long" | head_bytes 300)"
fi

# --- 6: CONTROL -- one slow call is not a tail ------------------------------
# Without the JC_DOCTOR_LAT_MIN_CALLS floor this would warn on any log holding a
# single slow request, which is noise rather than evidence.
mklog "$TDIR/t.jsonl" 2 228000
(cd "$ws" && "$BIN" --config "$tmp/short.json" doctor < /dev/null) \
    > "$tmp/out.few" 2>&1
if ! $G -qi 'stall timeout is 30s, but' "$tmp/out.few"; then
    t_ok "control: 2 calls is below the floor, so no warning is raised"
else
    t_fail "warned on a 2-call log. One slow call is not a latency tail, and a
   warning on it teaches the reader to ignore the check."
fi

t_done
