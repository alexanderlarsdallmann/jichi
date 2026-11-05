#!/bin/sh
# smoke: `jichi ps` lists the runs alive on this machine and how to reach them
# (M759).
#
# THE GAP, from the operator's twelve questions (plan sections 9 and 10): a run
# may hold a lease (~/.jichi.d/leases), open a control socket
# (~/.jichi.d/control/<pid>.sock) and write a journal (~/.jichi.d/runs/<run>.jsonl),
# and no command walked that chain -- a supervisor had to know the pid to find the
# socket. `ps` joins the three by pid, and counts what runs that are gone left
# behind without removing it.
#
# Checked against the effect: an isolated HOME with nothing running, then a real
# bounded run with --control in the background (its first tool is `sleep 3`, the
# window), then planted records for a pid that does not exist.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# --- 1: nothing is running --------------------------------------------------------
# The JSON captures take STDOUT ONLY. jichi prints its JSON on stdout and its
# informational banners on stderr, and that split is correct -- but on a
# <1 GB machine the auto-lite banner IS printed at startup (V2f, 492 MB
# Debian 9, 2026-10-16), and `2>&1` would merge it into the payload where
# jsonq's cJSON_Parse then reads nothing. The human captures keep 2>&1; they
# grep, so an extra stderr line cannot mislead them.
"$BIN" ps < /dev/null > "$tmp/none.txt" 2>&1; rc1=$?
"$BIN" ps --output json < /dev/null > "$tmp/none.json" 2>/dev/null
if [ "$rc1" -eq 0 ] && grep -q 'no jichi run is alive' "$tmp/none.txt" \
   && [ "$("$JQ" '.runs[0]' "$tmp/none.json" 2>/dev/null)" = "" ]; then
    t_ok "with nothing running, ps says so, and its JSON lists no runs"
else
    t_fail "ps with nothing running (rc=$rc1): $(head_bytes 160 "$tmp/none.txt")"
fi

# --- a live bounded run with a control socket ---------------------------------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text PS_DONE
rule
  tool run_terminal_command {"command":"sleep 3"}
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto --control "$tmp/c.sock" \
    --lease warn --budget-tokens 100k --deadline 2m --no-session \
    -p "run the task" < /dev/null > "$tmp/run.out" 2> "$tmp/run.err") &
run_pid=$!
_i=0
while [ ! -S "$tmp/c.sock" ] && [ $_i -lt 20 ]; do _i=$((_i + 1)); sleep 1; done
sleep 1
"$BIN" ps < /dev/null > "$tmp/live.txt" 2>&1
"$BIN" ps --output json < /dev/null > "$tmp/live.json" 2>/dev/null
wait "$run_pid"
mm_stop
jpid=$(sed -n 's/.*"pid":\([0-9]*\).*/\1/p' "$tmp/live.json" | head -1)

# 2: the live run is listed, with its control socket, under its own pid (the run
# was started with exec, so $! is jichi itself)
if grep -q "$tmp/c.sock" "$tmp/live.txt" && [ "$jpid" = "$run_pid" ]; then
    t_ok "a live run is listed with its control socket, under its own pid ($jpid)"
else
    t_fail "the live run is not listed as pid $run_pid (got '$jpid'): $(head_bytes 200 "$tmp/live.txt")"
fi

# 3: ...and its lease, mode, workspace and socket, in the JSON a supervisor reads.
# The socket is at an explicit path, so it can only have come from the lease; the
# root is compared canonically, as jichi records it.
wsreal=$(cd "$ws" && pwd -P)
if [ "$("$JQ" '.runs[0].lease' "$tmp/live.json" 2>/dev/null)" = "true" ] \
   && [ "$("$JQ" '.runs[0].mode' "$tmp/live.json" 2>/dev/null)" = "auto" ] \
   && [ "$("$JQ" '.runs[0].root' "$tmp/live.json" 2>/dev/null)" = "$wsreal" ] \
   && [ "$("$JQ" '.runs[0].control' "$tmp/live.json" 2>/dev/null)" = "$tmp/c.sock" ]; then
    t_ok "the JSON row carries the lease, the mode, the workspace and the socket"
else
    t_fail "JSON row: $(head_bytes 300 "$tmp/live.json")"
fi

# 4: after it exits, it is gone from the list -- and it left nothing ps would count
# as stale. The stale count is only worth reading if a run that ended cleanly does
# not add to it: its lease is released and its socket unlinked on the way out.
"$BIN" ps < /dev/null > "$tmp/after.txt" 2>&1
"$BIN" ps --output json < /dev/null > "$tmp/after.json" 2>/dev/null
if grep -q 'no jichi run is alive' "$tmp/after.txt" \
   && [ "$("$JQ" '.stale.leases' "$tmp/after.json" 2>/dev/null)" = "0" ] \
   && [ "$("$JQ" '.stale.sockets' "$tmp/after.json" 2>/dev/null)" = "0" ]; then
    t_ok "after the run exits, ps lists nothing and counts nothing left behind"
else
    t_fail "ps after the run: $(head_bytes 200 "$tmp/after.txt") $(head_bytes 120 "$tmp/after.json")"
fi

# --- records left by a run that is gone -----------------------------------------------
# A pid that cannot be alive: above any pid_max this host allows.
dead=2147483000
mkdir -p "$HOME/.jichi.d/leases" "$HOME/.jichi.d/control"
printf '{"v":1,"run":"deadbeef","pid":%s,"started":1,"mode":"auto"}\n' "$dead" \
    > "$HOME/.jichi.d/leases/1.json"
: > "$HOME/.jichi.d/control/$dead.sock"
"$BIN" ps < /dev/null > "$tmp/stale.txt" 2>&1
"$BIN" ps --output json < /dev/null > "$tmp/stale.json" 2>/dev/null

# 5: a dead run's lease and socket are counted, not listed as alive
if grep -q '1 lease(s) and 1 control socket(s) are left by runs that are gone' "$tmp/stale.txt" \
   && grep -q 'no jichi run is alive' "$tmp/stale.txt" \
   && [ "$("$JQ" '.stale.leases' "$tmp/stale.json" 2>/dev/null)" = "1" ]; then
    t_ok "a dead run's lease and socket are counted as left behind, not as alive"
else
    t_fail "stale records: $(head_bytes 240 "$tmp/stale.txt")"
fi

# 6: ...and ps removed neither: a record of how a run ended is evidence
if [ -f "$HOME/.jichi.d/leases/1.json" ] && [ -e "$HOME/.jichi.d/control/$dead.sock" ]; then
    t_ok "ps removes nothing"
else
    t_fail "ps deleted a record it only had to read"
fi

# 7: a FIFO named like a lease does not hang ps. It reads names it found by
# scanning, and opening a FIFO with no writer blocks forever (M198); without the
# regular-file check this is where ps would stop.
if command -v mkfifo > /dev/null 2>&1; then
    rm -f "$HOME/.jichi.d/control/$dead.sock"
    mkfifo "$HOME/.jichi.d/leases/2.json"
    with_deadline 20 "$BIN" ps < /dev/null > "$tmp/fifo.txt" 2>&1; rc7=$?
    if [ "$rc7" -eq 0 ] && grep -q '1 lease(s) and 0 control socket(s)' "$tmp/fifo.txt"; then
        t_ok "a FIFO named like a lease is skipped, not read (ps did not hang)"
    else
        t_fail "ps with a FIFO in the leases directory (rc=$rc7): $(head_bytes 200 "$tmp/fifo.txt")"
    fi
else
    t_skip_one "no mkfifo on this host"
fi

t_done
