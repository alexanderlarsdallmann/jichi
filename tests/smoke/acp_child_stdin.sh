#!/bin/sh
# smoke: a command jichi runs does not read the ACP client's protocol stream
# (M844, ACP-1).
#
# THE DEFECT. Over ACP, jichi's stdin IS the client's JSON-RPC stream, and the six
# places jichi starts a program handed that stdin on (JC_SPAWN_INHERIT): the
# shell's default and watched paths, background commands, the verify command,
# the snapshot store's git and the git tools. A command that reads stdin --
# `head -n1`, `cat`, an installer asking "continue? [Y/n]" -- ate the client's
# next message; the 2026-09-29 branch review reproduced a swallowed
# session/cancel.
#
# HOW THIS SEES IT. The mock model asks for `touch started; head -n1` under
# --auto. The driver waits for `started`, then writes a marker line into jichi's
# stdin. A child that inherits the stream reads the marker, which then goes back
# to the model in the tool result, and the mock's capture of that request shows
# it. A child whose stdin is /dev/null gets EOF at once, and the marker reaches
# jichi instead. Checks 1-2 are the controls that keep check 3 from passing
# vacuously: the command ran, and the turn completed.
#
# UNIVERSE (check 4): every `fd_stdin` assignment in src/ -- the field each
# jc_spawn request carries (M832 routed every fork-to-exec site through
# jc_spawn). None may be JC_SPAWN_INHERIT, and the count is exact, so a new
# spawn site makes its author decide what the child's stdin is. Check 3 drives
# the default shell path; check 4 holds the other five sites, which one run
# cannot reach.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
fifo="$tmp/in.fifo"
outf="$tmp/out.jsonl"
cap="$tmp/cap"
MARK=acp-stdin-marker-7f3a
mkfifo "$fifo"
: > "$outf"

# wait for a line in the growing $1 whose .id == $2 and has result/error,
# within $3 seconds; print it on success. (From acp_cancel.sh.)
acp_wait_id() {
    _end=$(( $(date +%s) + $3 ))
    while [ "$(date +%s)" -le "$_end" ]; do
        while IFS= read -r _l; do
            [ "$(printf '%s' "$_l" | "$JQ" .id 2>/dev/null)" = "$2" ] || continue
            if printf '%s' "$_l" | "$JQ" -q .result 2>/dev/null \
               || printf '%s' "$_l" | "$JQ" -q .error 2>/dev/null; then
                printf '%s\n' "$_l"
                return 0
            fi
        done < "$1"
        sleep 1
    done
    return 1
}

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"touch $ws/started; head -n1"}
rule
  text done
EOF
mm_start "$tmp/replies.mm" "$cap"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0}
EOF

# jichi's read-open of the FIFO blocks until a writer; backgrounding it and
# then opening fd 3 read-write both unblocks it AND keeps stdin open.
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --acp --auto \
    < "$fifo" > "$outf" 2>/dev/null) &
ACP_PID=$!
exec 3<>"$fifo"
send() { printf '%s\n' "$1" >&3; }

send '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{}}}'
acp_wait_id "$outf" 1 15 > /dev/null
send '{"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"'"$ws"'","mcpServers":[]}}'
sid=$(acp_wait_id "$outf" 2 15 | "$JQ" .result.sessionId 2>/dev/null)
send '{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"'"$sid"'","prompt":[{"type":"text","text":"run it"}]}}'

# Wait for the command to be running, then put a line on jichi's stdin.
_end=$(( $(date +%s) + 20 ))
while [ ! -f "$ws/started" ] && [ "$(date +%s)" -le "$_end" ]; do
    sleep 1
done
send '{"jsonrpc":"2.0","method":"x/marker","params":{"m":"'"$MARK"'"}}'
res=$(acp_wait_id "$outf" 3 30)
exec 3>&-
kill "$ACP_PID" 2>/dev/null
wait "$ACP_PID" 2>/dev/null
mm_stop

# --- 1-2: the controls --------------------------------------------------------
if [ -f "$ws/started" ]; then
    t_ok "control: the model's command ran"
else
    t_fail "control failed: the command never ran (no $ws/started), so check 3
 would pass without testing anything"
fi
if [ -n "$res" ]; then
    t_ok "control: the turn completed ($(printf '%s' "$res" | "$JQ" .result.stopReason 2>/dev/null))"
else
    t_fail "control failed: session/prompt never returned"
fi

# --- 3: the protocol line did not reach the command ----------------------------
if [ ! -f "$cap/req.2" ]; then
    t_fail "no second model request was captured, so the tool result never went
 back to the model and check 3 cannot be judged"
elif grep -q "$MARK" "$cap/req.2"; then
    t_fail "the command READ the client's protocol line: the marker came back to
 the model in the tool result. A child of jichi --acp inherits the JSON-RPC
 stream as its stdin (ACP-1)"
else
    t_ok "the client's protocol line did not reach the command's stdin"
fi

# --- 4: no spawn site hands a child jichi's own stdin ----------------------------
inh=$(grep -rn -E 'fd_stdin[[:space:]]*=[[:space:]]*JC_SPAWN_INHERIT' "$SMOKE_ROOT/src" \
      2>/dev/null | sed "s|^$SMOKE_ROOT/||")
nset=$(grep -rn -E 'fd_stdin[[:space:]]*=' "$SMOKE_ROOT/src" 2>/dev/null | wc -l | tr -d ' ')
if [ -n "$inh" ]; then
    t_fail "a spawn site hands its child jichi's own stdin -- over ACP that is the
 client's protocol stream (ACP-1). Use JC_SPAWN_DEVNULL, or a pipe:
$inh"
elif [ "$nset" -ne 12 ]; then
    t_fail "src/ has $nset fd_stdin assignments; expected exactly 12 (nine
 /dev/null, three pipes). A spawn site was added or removed: decide what its
 child's stdin is, then change this count and say why.
$(grep -rn -E 'fd_stdin[[:space:]]*=' "$SMOKE_ROOT/src" | sed "s|^$SMOKE_ROOT/||")"
else
    # The eleventh site is the jichi-harness drive's gzip child (M1059, H5,
    # behaviour 11: it reads its OWN source file, not stdin) and the twelfth
    # /dev/null site is the jichi-harness DRIVE spawn (M1059, H5, behaviour 6:
    # the loop's run child, like the run's own jichi child, never reads the
    # operator's stdin). Each is a C89 spawn site like the others, not an ACP
    # client, so the census counts them but the ACP-1 rule (never inherit) is
    # what this lint actually pins: all twelve are /dev/null or a pipe, none
    # JC_SPAWN_INHERIT.
    t_ok "no spawn site inherits jichi's stdin (12 fd_stdin assignments, none JC_SPAWN_INHERIT)"
fi

t_done
