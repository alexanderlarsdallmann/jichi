#!/bin/sh
# smoke: the ACP tool-result echo truncates on a character boundary (M536).
#
# THE DEFECT. acp_tool_done() capped the text it echoes to the editor at
# ACP_RESULT_MAX with a raw memcpy:
#
#     memcpy(bounded, result, ACP_RESULT_MAX);
#
# 8192 is not a multiple of 3, so for any tool result made of 3-byte characters
# and long enough to truncate, splitting one was the NORMAL outcome -- the same
# arithmetic that made M439's 512-byte preview defect routine rather than rare.
# The stray byte then rides inside a JSON-RPC string, and a strict client
# (serde_json in Zed, Go's encoding/json) is entitled to reject the whole
# notification: the failure mode is not a mangled tail, it is a LOST FRAME, so
# the editor's tool call never leaves the "running" state.
#
# WHAT MAKES IT WORTH A DRIVER RATHER THAN A NOTE. jc_utf8_trunc_len already
# existed, was already unit tested, and was already CALLED IN THIS FILE --
# acp_cmd_run() truncates terminal output with it forty lines earlier. One call
# site got the helper and its sibling did not. A unit test on the helper cannot
# see that; only the wiring can, which is exactly what M439's driver said about
# its own pure-function twin.
#
# THE CHECKS BORROW M439's TECHNIQUE, including both of the mistakes recorded in
# tests/smoke/jsonl_utf8.sh: "the last byte must not be a continuation byte" is
# INVERTED (a valid string ending in U+2026 ends in 0xA6), and a length-modulo
# test is defeated by read_file's `cat -n` gutter. Counting the bytes that must
# pair is gutter-independent and is what check 3 does.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"
fifo="$tmp/in.fifo"
outf="$tmp/out.jsonl"
mkfifo "$fifo"
: > "$outf"

# wait for a line in the growing $1 whose .id == $2 and has result/error,
# within $3 seconds; print it on success. (Same shape as acp_cancel.sh.)
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

# The fixture: U+2026 (E2 80 A6) repeated well past the 8192-byte cap. Every one
# of its bytes is distinctive, so a split leaves a lone E2 or an orphan 80/A6.
: > "$ws/wide.txt"
i=0
while [ "$i" -lt 400 ]; do
    printf '\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246\342\200\246' >> "$ws/wide.txt"
    i=$((i + 1))
done
bytes=$(wc -c < "$ws/wide.txt" | tr -d ' ')

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text ACP_UTF8_DONE
rule
  tool read_file {"path":"wide.txt"}
EOF
mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,
 "maxRetries":0,"timeouts":{"stall":120}}
EOF

(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --acp \
    < "$fifo" > "$outf" 2>/dev/null) &
ACP_PID=$!
exec 3<>"$fifo"
send() { printf '%s\n' "$1" >&3; }

acp_teardown() {
    exec 3>&-
    kill "$ACP_PID" 2>/dev/null
    wait "$ACP_PID" 2>/dev/null
    mm_stop
}

# ---- 1: the fixture is big enough to truncate, and the turn ran -------------
# The extraction floor. A file under 8192 bytes would make every check below
# pass vacuously, because no cut would happen at all.
send '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1,"clientCapabilities":{}}}'
if acp_wait_id "$outf" 1 20 > /dev/null && [ "$bytes" -gt 8192 ]; then
    t_ok "initialize answered and the fixture is ${bytes} bytes (> the 8192 cap)"
else
    t_fail "initialize failed or fixture too small (bytes=$bytes, want > 8192)"
    acp_teardown; t_fail -; t_fail -; t_fail -; t_done
fi

send '{"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"'"$ws"'","mcpServers":[]}}'
ns=$(acp_wait_id "$outf" 2 20)
sid=$(printf '%s' "$ns" | "$JQ" .result.sessionId 2>/dev/null)
if [ -z "$sid" ] || [ "$sid" = "null" ]; then
    t_fail "no sessionId -- cannot reach a tool call"
    acp_teardown; t_fail -; t_fail -; t_done
fi

send '{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"'"$sid"'","prompt":[{"type":"text","text":"read the wide file"}]}}'
acp_wait_id "$outf" 3 90 > /dev/null
acp_teardown

# ---- 2: a truncated tool_call_update carrying text was emitted -------------
# The completed update is the one that echoes the result; grep for the marker the
# truncation itself appends, so this cannot match the untruncated case.
line=$(grep '"sessionUpdate":"tool_call_update"' "$outf" \
       | grep '\[truncated\]' | head -1)
printf '%s' "$line" > "$tmp/line.json"
txt=$("$JQ" '.params.update.content[0].content.text' "$tmp/line.json" 2>/dev/null)
tlen=$(printf '%s' "$txt" | wc -c | tr -d ' ')
if [ -n "$txt" ] && [ "$tlen" -gt 4000 ]; then
    t_ok "a truncated tool_call_update was echoed (${tlen} bytes of text)"
else
    t_fail "no truncated tool_call_update found (text len=$tlen): \
$(printf '%s' "$line" | head_bytes 200)"
fi

# ---- 3: no orphaned lead or trail byte anywhere in the echoed text ---------
# Every U+2026 contributes exactly one E2 and one A6, so the two counts are equal
# in any well-formed prefix; a cut that split a character leaves an extra E2 (or
# an E2 80 pair) with no matching A6. Gutter-independent, unlike a length test.
ne2=$(printf '%s' "$txt" | od -An -tx1 | tr -s ' ' '\n' | grep -c '^e2$')
na6=$(printf '%s' "$txt" | od -An -tx1 | tr -s ' ' '\n' | grep -c '^a6$')
if [ "$ne2" -gt 1000 ] && [ "$ne2" -eq "$na6" ]; then
    t_ok "every lead byte has its trail byte ($ne2 = $na6): no character split"
else
    t_fail "e2 count $ne2 vs a6 count $na6 -- a character was split at the cut"
fi

# ---- 4: the truncation marker survives, so the client is TOLD ---------------
# A boundary-safe cut that also dropped the marker would leave the editor
# believing it had the whole result.
case "$txt" in
    *"... [truncated]") t_ok "the echoed text still ends with the truncation marker" ;;
    *) t_fail "the marker is missing or not final: \
$(printf '%s' "$txt" | od -An -c | tail -2 | tr -s ' \n' ' ')" ;;
esac

t_done
