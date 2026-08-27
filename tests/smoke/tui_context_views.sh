#!/bin/sh
# smoke: the TUI's /context sub-views (M317).
#
# M313 and M315 built these for the CLI; this makes them reachable where
# people work. `/context history` is BETTER here than the subcommand: the TUI
# holds the LIVE history, so there is no one-turn save lag, it works before
# the first save, and the tools view sees the full live registry rather than
# the built-in subset.
#
# Anchors are chosen against failure mode 9 (TEST_INTEGRITY.md): a PTY
# transcript is one flat log of every surface, so each expect must name
# something only ITS view prints.
#
#   plain /context   -> "Context window:"   (the budget report's header)
#   /context tools   -> "share   cum."      (the per-tool table header)
#   /context history -> "by role"           (the history block)
#   /context bogus   -> "unknown view"      (not silently the default)
#
# The tools view is deliberately NOT matched on "tool definitions", which the
# plain report also prints.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
printf 'a line mentioning the timeout setting\nand one more\n' > "$ws/note.txt"

# --- 1: no model needed for the offline views, and an EMPTY history says so ---
# The empty case is the same principle M314/M316 turned on: an absence must be
# stated, not rendered as a block of nothing.
write_config "$tmp/offline.json" 9 '"contextLimit":32000'

cat > "$tmp/offline.pd" <<'EOF'
expect "] " 15
delay 300
send "/context\r"
expect "Context window:" 20
delay 500
send "/context tools\r"
expect "share   cum." 20
delay 500
send "/context history\r"
expect "is empty" 20
delay 500
send "/context nosuchview\r"
expect "unknown view" 20
delay 500
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --cols 100 \
    --log "$tmp/offline.log" "$tmp/offline.pd" -- \
    "$BIN" --config "$tmp/offline.json" --no-lite); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "offline: /context, tools, empty history, and a refused bogus view"
else
    t_fail "offline views script rc=$rc (see transcript above)"
fi

# --- 2 + 3: a live turn with a tool call, then the breakdown -----------------
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"note.txt"}
rule
  match "\"role\":\"tool\""
  text VIEWS_TURN_DONE
rule
  status 500
  body {"error":"unexpected request"}
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/live.json" "$MM_PORT" '"contextLimit":32000'

# `auto` so the tool call is not gated on an approval keypress.
cat > "$tmp/live.pd" <<'EOF'
expect "] " 15
delay 300
send "/auto\r"
delay 500
send "read the note\r"
expect "VIEWS_TURN_DONE" 40
delay 800
send "/context history\r"
expect "by role" 25
delay 500
send "/exit\r"
waitexit 10
assertexit 0
EOF

(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 120 --cols 100 \
    --log "$tmp/live.log" "$tmp/live.pd" -- \
    "$BIN" --config "$tmp/live.json" --no-lite); rc=$?
mm_stop
if [ $rc -eq 0 ]; then
    t_ok "after a live turn, /context history breaks the LIVE history down"
else
    t_fail "live views script rc=$rc (see transcript above)"
fi

# The live history must be attributed by tool name -- the same join M315 fixed,
# exercised here through the TUI rather than a saved session.
#
# Anchored on the PER-TOOL LINE SHAPE, not on the bare name. A bare
# `grep read_file` passes on the tool-activity line the TUI prints while the
# call runs -- so it stayed green with the whole sub-view feature reverted.
# Failure mode 9, in the driver whose own header warns about failure mode 9;
# caught only by running the revert. The `(N%, N call)` suffix is printed by
# nothing else.
if grep -qE "read_file +~?[0-9]+ +\([0-9]+%, [0-9]+ calls?\)" "$tmp/live.log"; then
    t_ok "the live breakdown attributes the tool result to read_file"
else
    t_fail "no per-tool line for read_file in the live breakdown"
fi

t_done
