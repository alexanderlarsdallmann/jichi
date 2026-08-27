#!/bin/sh
# smoke: conversational rewind (M34c). One mutating mock turn edits
# hello.txt (snapshots on, so a pre-edit checkpoint is taken and the
# session saved); `rewind 1 --dry-run` previews without changing anything;
# `rewind 1` then reverts BOTH halves: the file returns to its original
# content AND the conversation is truncated (the prompt vanishes from a
# fresh export). Needs git; skips without it.
# (Port of tests/e2e/rewind.py, M211.)
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
PROMPT="replace the greeting in hello.txt"
printf 'ORIGINAL CONTENT\n' > "$ws/hello.txt"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"hello.txt","content":"MODIFIED BY THE AGENT\n"}
rule
  text done
EOF

mm_start "$tmp/replies.mm" "$tmp" 2
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"chat","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --auto -p "$PROMPT" < /dev/null > /dev/null 2>&1)
mm_stop
if grep -q "MODIFIED BY THE AGENT" "$ws/hello.txt"; then
    t_ok "the agent modified the file"
else
    t_fail "file not modified: $(cat "$ws/hello.txt")"
fi

(cd "$ws" && "$BIN" --config "$tmp/config.json" export \
    < /dev/null > "$tmp/before.md" 2>/dev/null)
if grep -q "## User" "$tmp/before.md" && grep -q "$PROMPT" "$tmp/before.md"
then
    t_ok "the transcript carries the user turn before the rewind"
else
    t_fail "user turn missing from the pre-rewind export"
fi

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    rewind 1 --dry-run < /dev/null > "$tmp/dry" 2>&1)
if [ -s "$tmp/dry" ] && grep -q "MODIFIED BY THE AGENT" "$ws/hello.txt"; then
    t_ok "--dry-run previewed and changed nothing"
else
    t_fail "dry-run empty or it modified the file"
fi

(cd "$ws" && with_deadline 30 "$BIN" --config "$tmp/config.json" \
    rewind 1 < /dev/null > /dev/null 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "ORIGINAL CONTENT" "$ws/hello.txt" \
   && ! grep -q "MODIFIED" "$ws/hello.txt"; then
    t_ok "rewind restored the file"
else
    t_fail "rewind rc=$rc; file: $(cat "$ws/hello.txt")"
fi

# the session TITLE still carries the prompt, so assert on the message
# body instead: no "## User" turn left, and the meta line says 0 messages
(cd "$ws" && "$BIN" --config "$tmp/config.json" export \
    < /dev/null > "$tmp/after.md" 2>/dev/null)
if ! grep -q "## User" "$tmp/after.md" \
   && grep -q '\*\*Messages:\*\* 0' "$tmp/after.md"; then
    t_ok "rewind truncated the conversation (0 messages in the export)"
else
    t_fail "the user turn survived the rewind in the transcript"
fi

t_done
