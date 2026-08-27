#!/bin/sh
# smoke: multiline paste into the TUI prompt (M156). A pasted multi-line
# block must reach the model INTACT, not collapsed to its first line:
#   1. bracketed paste: ESC[200~line1<nl>line2<nl>line3 ESC[201~ then Enter
#   2. burst fallback: line1<nl>line2<nl>line3<nl> as ONE write (no markers)
# Each is one ptydrive `send` (a single write = a burst), so both take the
# paste branch. The mock answers PASTE_OK only if all three lines reached
# it (mockmodel's multi-`match` AND), so PASTE_OK in the transcript proves
# the whole block was submitted. (Port of tests/e2e/paste.py, M215.)
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "line1"
  match "line2"
  match "line3"
  text PASTE_OK
rule
  text PASTE_BAD
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# 1. bracketed paste then Enter (\n submits, matching the validated driver)
cat > "$tmp/bracket.pd" <<'EOF'
expect "] " 15
send "\x1b[200~line1\nline2\nline3\x1b[201~\n"
expect "PASTE_OK" 15
delay 500
send "/exit\r"
waitexit 10
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 45 --cols 100 \
    "$tmp/bracket.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "bracketed paste reached the model intact"
else
    t_fail "bracketed-paste script rc=$rc"
fi

# 2. burst fallback: the whole block in one write, no markers
cat > "$tmp/burst.pd" <<'EOF'
expect "] " 15
send "line1\nline2\nline3\n"
expect "PASTE_OK" 15
delay 500
send "/exit\r"
waitexit 10
EOF
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 45 --cols 100 \
    "$tmp/burst.pd" -- "$BIN" --config "$tmp/config.json" --no-route); rc=$?
mm_stop
if [ $rc -eq 0 ]; then
    t_ok "burst multiline (no bracketed paste) reached the model intact"
else
    t_fail "burst-paste script rc=$rc"
fi

t_done
