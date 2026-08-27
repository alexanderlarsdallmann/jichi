#!/bin/sh
# smoke: `/learn analyze` in the TUI, on a real PTY (M292).
#
# The learn-analyze report was CLI-only: it lived as two statics in main.c, so a
# TUI user had to leave the session to see what their own logs said. This drives
# the TUI command end to end against a hand-written telemetry log and asserts the
# report the shared renderer produces -- offline, so no model is needed for the
# analysis itself (the TUI still needs a config to start, pointed at a dead port).
#
# Two PTY rules the tier learned the hard way: the expect after a send must be a
# string that is NOT already in the startup banner, and sends need human-scale
# gaps or the M156 burst-paste window merges them into one logical line.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

write_config "$tmp/config.json" 9

# A log with one tool failing every call (below the 60% ok-rate floor) and an
# envelope outcome, stamped with this workspace so the report's filter keeps it.
ws=$(cd "$tmp" && pwd)
cat > "$tmp/tel.jsonl" <<EOF
{"v":1,"event":"tool_call","ws":"$ws","ts":9000,"name":"flaky_tool","ok":false}
{"v":1,"event":"tool_call","ws":"$ws","ts":9001,"name":"flaky_tool","ok":false}
{"v":1,"event":"tool_call","ws":"$ws","ts":9002,"name":"flaky_tool","ok":false}
{"v":1,"event":"turn_end","ws":"$ws","ts":9003,"outcome":"budget_exhausted","rolled_back":false}
EOF

# An explicit path argument, so the driver does not depend on which log the
# session happens to be writing.
cat > "$tmp/learn.pd" <<EOF
expect "] " 20
delay 400
send "/learn analyze $tmp/tel.jsonl\r"
expect "flaky_tool" 25
delay 600
send "/exit\r"
waitexit 15
EOF

# Drive from INSIDE $tmp: the TUI filters the report to its own workspace (M56),
# so the log's "ws" stamp and jichi's cwd must be the same directory. Launching
# elsewhere correctly yields "no recurring problems" -- which is the filter
# working, and is how this fixture was wrong on its first run.
if (cd "$tmp" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --log "$tmp/pty.log" \
        "$tmp/learn.pd" -- "$BIN" --config "$tmp/config.json" --no-lite) \
        > "$tmp/drive.out" 2>&1; then
    t_ok "TUI /learn analyze <log> runs and reports the failing tool"
else
    t_fail "TUI /learn analyze <log> runs and reports the failing tool"
    sed 's/^/    | /' "$tmp/drive.out"
fi

# The report is the SHARED renderer's, so the outcome line must be there too --
# it is the one that stops the mentor drafting "budget stop = failure" lessons.
if grep -aq "Autonomy outcomes" "$tmp/pty.log"; then
    t_ok "the shared report's outcome line reaches the TUI"
else
    t_fail "the shared report's outcome line reaches the TUI"
fi

# It says WHICH log it read: an unlabelled report invites being read as covering
# something else (the M286/M290 lesson, applied to this surface).
if grep -aq "analysed $tmp/tel.jsonl" "$tmp/pty.log"; then
    t_ok "the TUI names the log it analysed"
else
    t_fail "the TUI names the log it analysed"
fi

# And the CLI subcommand renders the same finding from the same file, which is
# the point of sharing jc_learn_analyze_render rather than formatting twice.
"$BIN" --config "$tmp/config.json" learn analyze "$tmp/tel.jsonl" \
    > "$tmp/cli.out" 2>&1 < /dev/null
if grep -q "flaky_tool" "$tmp/cli.out"; then
    t_ok "the CLI renders the same finding from the same log"
else
    t_fail "the CLI renders the same finding from the same log"
    sed 's/^/    | /' "$tmp/cli.out"
fi

# The filter is load-bearing: driven from a DIFFERENT cwd, the same log yields no
# findings, because its events belong to another workspace. A shared telemetry dir
# mixes projects, and an unfiltered TUI report would rank another project's
# problems as this one's.
cat > "$tmp/other.pd" <<EOF
expect "] " 20
delay 400
send "/learn analyze $tmp/tel.jsonl\r"
expect "No recurring problems" 25
delay 600
send "/exit\r"
waitexit 15
EOF
otherws=$(smoke_tmp)
if (cd "$otherws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 \
        --log "$tmp/pty2.log" "$tmp/other.pd" -- \
        "$BIN" --config "$tmp/config.json" --no-lite) \
        > "$tmp/drive2.out" 2>&1; then
    t_ok "the TUI report is filtered to its own workspace"
else
    t_fail "the TUI report is filtered to its own workspace"
    sed 's/^/    | /' "$tmp/drive2.out"
fi

t_done
