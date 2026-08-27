#!/bin/sh
# smoke: a tool result's control bytes do not reach the terminal in TUI mode (M472).
#
# THE SECOND PATH, and why it needs its own driver. output_escapes.sh covers the
# model's own text through the headless writer. This one covers the other way
# untrusted bytes reach a terminal, which needs no model cooperation at all:
#
#   a file in the repo contains OSC 52  ->  read_file  ->  cb_tool_result prints it
#
# In headless mode hl_tool_result does `(void)result` -- it prints the tool name and
# status, never the bytes -- so this path exists only in the TUI, and only a PTY
# driver can see it. It is also the path that puts this in M300's untrusted-content
# class: the model is a conduit here, not the author.
#
# THE ASSERTION IS SEQUENCE-SPECIFIC, not "no ESC in the transcript". The TUI emits
# its OWN SGR colour constantly, so a blanket ESC check would be red by
# construction and would have to be weakened until it checked nothing. Instead:
# jichi's colour must still be there (proving the transcript is real and the strip
# did not eat jichi's own output) while the FILE's OSC 52 must be gone.
#
# See docs/analysis/2026-08-17-source-hardening-audit.md §H3.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The fixture: a file whose CONTENTS carry a clipboard-write sequence. \ooo is
# POSIX printf (unlike \xNN, banned tree-wide at M471 as a GNU extension).
printf 'harmless line one\n\033]52;c;cHduZWQ=\007PAYLOAD-AFTER-OSC52\nlast line\n' \
    > "$ws/tainted.txt"
# Confirm the fixture really holds the byte -- a test whose input is wrong proves
# nothing, and building this payload through a shell is exactly where that happens.
if [ "$(od -An -c < "$ws/tainted.txt" | tr -s ' ' '\n' | grep -cx '033')" != "1" ]; then
    t_fail "fixture does not contain a raw ESC byte"
    t_fail "(same cause)"
    t_fail "(same cause)"
    t_done
fi

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"tainted.txt"}
rule
  text DONE-READING
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 6
write_config "$tmp/config.json" "$MM_PORT"

cat > "$tmp/esc.pd" <<'EOF'
expect "] " 15
delay 300
send "show me the file\r"
expect "DONE-READING" 30
delay 400
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 60 --cols 100 \
    --log "$tmp/esc.log" "$tmp/esc.pd" -- \
    "$BIN" --config "$tmp/config.json" --auto); rc=$?
mm_stop

if [ $rc -eq 0 ]; then
    t_ok "the TUI read the tainted file and exited cleanly"
else
    t_fail "PTY script rc=$rc (see transcript above)"
fi

# The file's OSC 52 must not be in the transcript. Matched as the byte sequence
# ESC ] 5 2 in od output, so the check never has to hold a control byte itself.
seq=$(od -An -c < "$tmp/esc.log" | tr -s ' \n' '  ' | grep -c '033 ] 5 2' || true)
if [ "$seq" = "0" ]; then
    t_ok "the file's OSC 52 clipboard write did not reach the terminal"
else
    t_fail "OSC 52 from a tool result reached the terminal ($seq occurrence(s))"
fi

# ...while jichi's own colour did. Without this the check above could pass by
# jichi having printed nothing at all, or by the strip having eaten everything.
if grep -q 'PAYLOAD-AFTER-OSC52' "$tmp/esc.log"; then
    t_ok "the surrounding file content still reached the terminal"
else
    t_fail "the tool result was lost entirely, not just its control bytes"
fi

t_done
