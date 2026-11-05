#!/bin/sh
# smoke: a task's title and a tool's output are cut and logged at whole characters
# (M889, the demos drive's N51).
#
# THE DEFECT. spawn_parallel printed each task's title with `%.70s`, a cut by bytes.
# Run 0104's title had an em dash (E2 80 94) starting at its 70th byte, so one of its
# three bytes was kept. The model's copy was repaired on the way in (`dup_content`);
# the telemetry line that carried the batch result was not valid UTF-8, and a strict
# reader rejected it. The event log's sink passed ill-formed input through: its own
# cut is character-safe, but it trusted what it was given.
#
# THE FIX. The title and the answer cap cut with jc_utf8_trunc_len, and the event
# log's sink repairs any line that is not valid UTF-8 (U+FFFD for a stray byte), as
# `dup_content` does for messages -- whichever producer cut it.
#
#   1  the task's title in the batch result ends at a whole character
#   2  the telemetry line that carries the batch result is valid UTF-8
#   3  that line is there -- the instrument reached it
#   4  a shell command's ill-formed output is repaired in its telemetry line -- the
#      sink, for any producer (the lead runs it: a read-only child may not)
#   5  a child's answer over the 16 KB cap is cut at a whole character: the cap falls
#      inside a two-byte character, and a byte cut leaves a stray byte that the model's
#      copy shows as U+FFFD
#
# Validity is iconv's verdict (POSIX), not a pattern.
. "$(dirname "$0")/_smoke.sh"

command -v iconv >/dev/null 2>&1 || t_skip "needs iconv to judge UTF-8"

t_plan 5
smoke_home
WS=$(smoke_tmp)
CAP=$(smoke_tmp)
TEL="$CAP/telemetry.jsonl"

# 69 ASCII bytes, then an em dash: `%.70s` keeps its first byte.
t69=$(printf '%-69s' 'CHILDU census of the webrtc signaling wall' | tr ' ' 'x')
title="${t69}— and the rest of the title"

# The lead first runs a command whose output is ill-formed on purpose (4), then the
# batch (1-3); the child answers at once. A read-only child may not run a shell.
cat > "$CAP/r.mm.in" <<'EOF'
wire openai
rule
  match "LEADU"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADU"
  match "printf"
  tool spawn_parallel {"tasks":[{"task":"@TITLE@"}]}
rule
  match "LEADU"
  tool run_terminal_command {"command":"printf 'X\\342\\200Y'"}
EOF
sed "s#@TITLE@#$title#" "$CAP/r.mm.in" > "$CAP/r.mm"
# 16,383 ASCII bytes, then a two-byte character across the 16,384-byte answer cap. A
# script line holds at most 1,023 bytes, so the answer goes out as a recorded stream.
aans=$(printf '%16383s' '' | tr ' ' 'a')
printf 'data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"%s\303\251TAIL"},"finish_reason":null}]}\n\ndata: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}\n\ndata: [DONE]\n\n' "$aans" > "$CAP/long.sse"
printf 'rule\n  match "CHILDU"\n  sse-file %s\n' "$CAP/long.sse" >> "$CAP/r.mm"
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "logging":{"level":"full","path":"$TEL"}}
EOF
( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
    -p "LEADU run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop
LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | tail -1)

# 1: the title, as the lead's model receives it, cut at a character: the 69 bytes and
#    "...", no U+FFFD (EF BF BD) where a stray byte was repaired on the way in
fffd=$(printf '\357\277\275')
if [ -n "$LR" ] && grep -q "### Task 1: ${t69}\.\.\." "$LR" &&
   ! LC_ALL=C grep -q "${t69}${fffd}" "$LR"; then
    t_ok "the task's title in the batch result ends at a whole character"
else
    t_fail "the title was cut inside a character: $(LC_ALL=C grep -o '### Task 1: [^\\]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 120 | od -c | head -n 3 | tr '\n' ' ')"
fi
# 2-3: the lead's spawn_parallel tool_call line, which carries the batch result
LC_ALL=C grep '"name":"spawn_parallel"' "$TEL" 2>/dev/null | LC_ALL=C grep '### Task 1' > "$CAP/lead.line"
if [ -s "$CAP/lead.line" ] && iconv -f UTF-8 -t UTF-8 < "$CAP/lead.line" > /dev/null 2>&1; then
    t_ok "the telemetry line that carries the batch result is valid UTF-8"
else
    t_fail "the batch result's telemetry line is not valid UTF-8 (or missing): $(wc -c < "$CAP/lead.line" 2>/dev/null) bytes"
fi
if [ -s "$CAP/lead.line" ]; then
    t_ok "the telemetry recorded the batch result -- the instrument reached it"
else
    t_fail "no spawn_parallel tool_call line with the batch result in $TEL: $(wc -l < "$TEL" 2>/dev/null) lines"
fi
# 4: the lead's shell output, ill-formed on purpose
LC_ALL=C grep '"name":"run_terminal_command"' "$TEL" 2>/dev/null | LC_ALL=C grep 'X' > "$CAP/child.line"
if [ -s "$CAP/child.line" ] && iconv -f UTF-8 -t UTF-8 < "$CAP/child.line" > /dev/null 2>&1 &&
   LC_ALL=C grep -q "$fffd" "$CAP/child.line"; then
    t_ok "a shell command's ill-formed output is repaired in its telemetry line"
else
    t_fail "the shell output's telemetry line is not repaired: $(wc -c < "$CAP/child.line" 2>/dev/null) bytes"
fi
# 5: the child's answer, as the lead's model receives it: the a-run reached it, and no
#    U+FFFD follows it where the cap fell
a64=$(printf '%64s' '' | tr ' ' 'a')
if [ -n "$LR" ] && LC_ALL=C grep -q "$a64" "$LR" && ! LC_ALL=C grep -q "${a64}${fffd}" "$LR"; then
    t_ok "a child's answer over the 16 KB cap is cut at a whole character"
else
    t_fail "the answer cap cut inside a character (or the answer never arrived): $(LC_ALL=C grep -c "$a64" "${LR:-/dev/null}" 2>/dev/null) a-runs"
fi

t_done
