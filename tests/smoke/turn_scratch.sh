#!/bin/sh
# smoke: per-tool-call metadata must not accumulate on the per-turn scratch
# arena (M218). The tool loop copies each call's name/args/id before
# executing it; with ~900 B write_file arguments x 200 calls IN ONE TURN
# those copies are ~180 KB that -- when they live on the per-turn arena --
# stay resident until the NEXT turn starts, which in a marathon --auto turn
# is never. This PTY-drives the real TUI through one such turn against the
# mock model and reads the binary's own /context "turn scratch N KB used"
# gauge afterwards. Fixture proportionality (M198): what this measures
# scales with CALLS x ARGS_BYTES (the mock's script format caps a line at
# 1 KB, hence many small calls rather than a few 8 KB ones); the 64 KB bar
# is ~4x the fixed per-turn cost (system message etc.) and ~3x under the
# pre-fix ~180 KB.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

CALLS=200
LIMIT_KB=64
pad=$(printf '%*s' 900 '' | tr ' ' a)

{
    echo 'wire openai'
    i=1
    while [ $i -le $CALLS ]; do
        echo 'rule'
        echo "  count $i"
        printf '  tool write_file {"path":"out.txt","content":"%s"}\n' "$pad"
        i=$((i + 1))
    done
    echo 'rule'
    echo '  text WRITES_ALL_DONE_99'
} > "$tmp/replies.mm"

mm_start "$tmp/replies.mm" "$tmp" $((CALLS + 1))
write_config "$tmp/config.json" "$MM_PORT" \
    '"maxToolIters":250,"permissions":{"allow":["write_file"]}'

# \x15 = Ctrl-U (a stray partial line must not become part of the command)
{
    echo 'expect "] " 15'
    echo 'send "\x15do the writes\r"'
    # Deadline hierarchy (M272): this inner expect must stay BELOW the
    # driver's ptydrive --deadline (120) and run.sh's outer limit (120),
    # because all three scale by the same JC_SMOKE_TIMEOUT_MULT -- raising
    # only this one inverts the hierarchy and the outer watchdog SIGTERMs a
    # healthy run. A machine whose per-turn cost outruns 90 s needs a bigger
    # MULT, not a bigger base -- and mockmodel's own --deadline is a fourth
    # layer that scales too (M273; unscaled, it killed the server mid-run
    # here and no expect budget could have helped).
    echo 'expect "WRITES_ALL_DONE_99" 90'
    echo 'delay 400'
    echo 'send "\x15/context\r"'
    echo 'expect "turn scratch" 10'
    echo 'delay 300'
    echo 'send "\x15/exit\r"'
    echo 'waitexit 10'
} > "$tmp/ts.pd"

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 120 --cols 120 \
    --log "$tmp/ts.log" "$tmp/ts.pd" -- \
    "$BIN" --config "$tmp/config.json" --auto --no-route); rc=$?
mm_stop
if [ $rc -eq 0 ]; then
    t_ok "one turn of $CALLS write_file calls + /context served"
else
    t_fail "ptydrive script rc=$rc"
fi

# parse the gauge from the transcript (strip ANSI first). The literal
# ESC-bracket is a class [\[] (see sessions_footprint.sh for why).
# One pass, and no nullable `grep -o` pattern -- see sessions_footprint.sh for
# the full story: `grep -o '[0-9]*'` prints nothing on OpenBSD's grep and exits
# 0, which failed this driver on that row for months (M481).
kb=$(smoke_plain "$tmp/ts.log" \
    | sed -n 's/.*turn scratch \([0-9][0-9]*\) KB used.*/\1/p' | tail -1)
if [ -n "$kb" ]; then
    if [ "$kb" -le "$LIMIT_KB" ]; then
        t_ok "turn scratch after the turn: ${kb} KB (<= $LIMIT_KB)"
    else
        t_fail "turn scratch ${kb} KB (limit $LIMIT_KB) -- per-call copies are accumulating per-turn (M218)"
    fi
else
    t_fail "could not read the /context turn-scratch gauge"
fi

t_done
