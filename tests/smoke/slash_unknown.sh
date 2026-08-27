#!/bin/sh
# smoke: headless `-p "/name"` when no such command exists (M269).
#
# An unresolved "/name" is deliberately NOT an error -- a prompt may legitimately
# start with a path ("/usr/bin/cc is missing") -- so it is passed to the model
# verbatim. That fall-through used to be SILENT, and the silence misled: running
# examples/self-hosting's `/review-diff` before copying its assets into .jichi/
# produced a confident, entirely improvised "review" with exit 0. Nothing said
# the command had not been found.
#
# So: the text still reaches the model (behaviour preserved), AND a warning
# names the missing command. A token containing '/' is a path, not a command
# name, and must stay quiet.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

# A real custom command, so a TYPO of it has something to be near (M345).
mkdir -p "$ws/.jichi/commands"
cat > "$ws/.jichi/commands/review-diff.md" <<'EOF'
---
description: review the working diff
---
Review the current diff carefully.
EOF

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text SLASH_FELL_THROUGH_OK
EOF
mm_start "$tmp/replies.mm" "$tmp"

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

# --- an unknown command name: warn, but still answer -------------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session -p "/no-such-command-here" \
    < /dev/null > "$tmp/out" 2> "$tmp/err"); rc=$?

if [ $rc -eq 0 ] && grep -q "SLASH_FELL_THROUGH_OK" "$tmp/out"; then
    t_ok "the text still reaches the model (fall-through preserved)"
else
    t_fail "rc=$rc, stdout=$(head_bytes 200 "$tmp/out")"
fi

if grep -q "no command '/no-such-command-here'" "$tmp/err"; then
    t_ok "the unresolved command is named on stderr"
else
    t_fail "no warning on stderr: $(head_bytes 300 "$tmp/err")"
fi

if grep -q "jichi commands" "$tmp/err"; then
    t_ok "the warning points at \`jichi commands\`"
else
    t_fail "warning lacks the actionable hint"
fi

# --- a TYPO of an installed command: the warning suggests it (M345) ----------
# The model has had "did you mean 'search_code'?" since M91; this is the same
# kindness at the human's own typo, from the same shared closeness rule --
# which is why check 2's wild guess above still gets the plain warning (the
# absence half of this pair: no suggestion is invented for a far-off name).
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session -p "/reveiw-diff" \
    < /dev/null > "$tmp/out3" 2> "$tmp/err3"); rc3=$?

if grep -q "did you mean '/review-diff'?" "$tmp/err3"; then
    t_ok "a typo'd command name suggests the installed one"
else
    t_fail "no suggestion for /reveiw-diff: $(head_bytes 300 "$tmp/err3")"
fi

if [ $rc3 -eq 0 ] && grep -q "SLASH_FELL_THROUGH_OK" "$tmp/out3"; then
    t_ok "the suggestion is advisory -- the text still reaches the model"
else
    t_fail "rc=$rc3, stdout=$(head_bytes 200 "$tmp/out3")"
fi

# --- a prompt that merely STARTS with a path: no warning ---------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session -p "/usr/bin/cc is missing on this box" \
    < /dev/null > "$tmp/out2" 2> "$tmp/err2") || true

if grep -q "no command" "$tmp/err2"; then
    t_fail "a leading path was mistaken for a command name"
else
    t_ok "a leading path stays quiet (no false warning)"
fi

t_done
