#!/bin/sh
# M807 (P2): a spawn_parallel WRITE child records its own reasoning to a transcript
# at ~/.jichi.d/attempts/<run>/t<k>.jsonl -- one JSONL line per model reply and one
# per tool call -- so a successor can read what the child did and why (P3 builds the
# end record from this file's tail). The file is born redacted (a registered secret
# a child's tool prints must not reach it), 0600 in a 0700 dir, and follows
# --no-session: when sessions are off, nothing is written. This began as the drive's
# T6/T7 and the review's T-new-1 (the canary redacted in the transcript).
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
CANARY=CANARYSECRETzt7x0123456789
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_W: write a file then answer","write":true}]}
rule
  match "TASK_W"
  match "\"role\":\"tool\""
  text CHILDDONE
rule
  match "TASK_W"
  tool run_terminal_command {"command":"printf '$CANARY\\n' > w.txt"}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
# apiKey IS the canary, so the value-based redactor has it registered; the child's
# tool command echoes it into the transcript's args, which must come out scrubbed.
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
# Sessions ON (no --no-session): the transcript follows the session switch.
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" -q --auto \
    -p "LEADPROMPT run a writer" < /dev/null > "$tmp/out.txt" 2>&1 )
mm_stop

tfile=$(find "$HOME/.jichi.d/attempts" -name 't1.jsonl' 2>/dev/null | head -1)
tdir=$(dirname "$tfile" 2>/dev/null)
models=0; tools=0; canary=0; fperm=""; dperm=""
if [ -n "$tfile" ] && [ -f "$tfile" ]; then
    models=$(grep -c '"event":"model_call"' "$tfile" 2>/dev/null)
    tools=$(grep -c '"event":"tool_call"' "$tfile" 2>/dev/null)
    canary=$(grep -c "$CANARY" "$tfile" 2>/dev/null)
    fperm=$(ls -ld "$tfile" 2>/dev/null | cut -c5-10)
    dperm=$(ls -ld "$tdir" 2>/dev/null | cut -c5-10)
fi
echo "# transcript: ${tfile:-none}; model_call lines=$models, tool_call lines=$tools; canary occurrences=$canary"
echo "# perms: file group/other='$fperm' dir group/other='$dperm' (want '------')"
if [ -n "${E1_RES:-}" ]; then mkdir -p "$E1_RES"; [ -n "$tfile" ] && cp -p "$tfile" "$E1_RES/" 2>/dev/null; fi

grep -q CHILDDONE "$tmp/out.txt" 2>/dev/null || grep -q LEADDONE "$tmp/out.txt" 2>/dev/null \
    && t_ok "instrument: the write child ran and the batch finished" \
    || t_fail "instrument: the scenario did not run -- $(tr '\n' ' ' < "$tmp/out.txt" | head_bytes 120)"
[ -n "$tfile" ] && t_ok "the write child wrote a transcript ($tfile)" \
    || t_fail "no transcript under ~/.jichi.d/attempts/*/t1.jsonl"
[ "$models" -ge 1 ] && t_ok "the transcript carries the child's model reply/replies ($models)" \
    || t_fail "the transcript has no model_call line"
[ "$tools" -ge 1 ] && t_ok "the transcript carries the child's tool call(s) ($tools)" \
    || t_fail "the transcript has no tool_call line"
[ "$canary" = 0 ] && t_ok "the transcript is born redacted (the registered secret does not appear)" \
    || t_fail "the transcript leaked the canary $canary time(s) -- not redacted"
{ [ "$fperm" = "------" ] && [ "$dperm" = "------" ]; } \
    && t_ok "transcript is 0600 in a 0700 dir (no group/other access)" \
    || t_fail "transcript perms are too open: file='$fperm' dir='$dperm'"

# ---- M1041-d-c: successful tool_call row has NO fail_class key ---------------
# The child's successful run_terminal_command is the only successful tool call.
# Extract its tool_call row and assert NO fail_class key (green when condition
# guards fail_class write; red if condition removed and fail_class always written).
tc_success=$(grep '"event":"tool_call".*"ok":true' "$tfile" 2>/dev/null | head -1)
if [ -z "$tc_success" ]; then
    t_fail "the successful tool_call row has NO fail_class key (no successful row found)"
else
    if echo "$tc_success" | grep -q '"fail_class"'; then
        t_fail "the successful tool_call row has fail_class key (should be absent)"
    else
        t_ok "the successful tool_call row has NO fail_class key"
    fi
fi

# ---- negative: --no-session writes no transcript -----------------------------
rm -rf "$HOME/.jichi.d/attempts"
tmp2=$(smoke_tmp); ws2=$(smoke_tmp)
cp "$tmp/replies.mm" "$tmp2/replies.mm"
mm_start_unbounded "$tmp2/replies.mm" "$tmp2/cap"
cat > "$tmp2/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$CANARY","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
( cd "$ws2" && with_deadline 90 "$BIN" --config "$tmp2/config.json" -q --no-session --auto \
    -p "LEADPROMPT run a writer" < /dev/null > "$tmp2/out.txt" 2>&1 )
mm_stop
nneg=$(find "$HOME/.jichi.d/attempts" -name 't*.jsonl' 2>/dev/null | grep -c .)
echo "# --no-session transcripts written: $nneg (want 0)"
[ "$nneg" = 0 ] && t_ok "--no-session writes no transcript" \
    || t_fail "--no-session still wrote $nneg transcript(s)"
t_done
