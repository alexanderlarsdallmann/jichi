#!/bin/sh
# smoke: a leading blank does not turn a slash COMMAND into a PROMPT (M550).
#
# THE DEFECT, found by a screen-reader user. Every slash dispatch in the TUI is a
# `strcmp(line, "/x")` or a `line[0] == '/'`, with no whitespace trim. So this:
#
#     [chat] >  /markdown
#
# matched nothing and was sent to the MODEL as a prompt. Two things then happened,
# both from the operator's own transcript: the round-trip cost ~280 output tokens,
# and because "/markdown" is meaningless as a message the model RE-ANSWERED THE
# PREVIOUS QUESTION -- so a listener sat through the entire reply a second time.
# It happened twice before the third attempt, typed without the stray space,
# printed the one line it should have printed all along.
#
# THE SHARP END IS `/exit`. " /exit" did not quit; it became a prompt. The
# documented way out of the program stopped working because of a character that
# occupies no visual space. A sighted user may notice the gap between the prompt
# arrow and the slash; a listener cannot -- which is the same shape as the TTY trap
# in ANECDOTES #66, an escape route failing silently.
#
# WHY "ZERO MODEL REQUESTS" IS THE RIGHT ASSERTION, rather than checking for the
# status line. A mishandled command is not merely unhandled -- it is *sent*. So the
# question that separates the two behaviours exactly is whether the mock server was
# contacted at all, and mockmodel's capture directory answers it: a request file
# exists, or it does not. Matching on output text would also pass for a build that
# sent the command AND happened to print something.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# One rule, and reaching it at all is the failure: no command in this driver should
# ever produce a model call.
cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  text A MODEL WAS CONTACTED
MM
mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"

# Leading space, trailing space, a tab, and a bare control -- driven through a real
# PTY because this is the interactive line-editor path, not the headless one.
cat > "$tmp/run.pd" <<'EOF'
delay 1200
send " /markdown\r"
delay 1200
send "/markdown \r"
delay 1200
send "\t/markdown\r"
delay 1200
send " /exit\r"
waitexit 20
EOF
(cd "$ws" && with_deadline 60 "$SMOKE_TOOLS/ptydrive" \
    --deadline 55 --cols 100 --log "$tmp/out.log" "$tmp/run.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible) >/dev/null 2>&1
rc=$?
mm_stop

# ---- 1: the harness reached the TUI at all (the denominator) ---------------
# Without this every check below is clean because nothing was typed, and a driver
# whose fixture never started is the vacuous shape this project keeps finding.
if [ -s "$tmp/out.log" ] && "$G" -q 'interactive agent' "$tmp/out.log"; then
    t_ok "the TUI started and the script was delivered"
else
    t_fail "no TUI banner in the capture -- nothing below tests anything: \
$(head_bytes 200 < "$tmp/out.log" 2>/dev/null)"
fi

# ---- 2: NOT ONE of the four lines reached the model ------------------------
# The defect itself. Four command lines, each with invisible whitespace; a build
# without the trim sends at least the first three.
nreq=$(ls "$tmp"/cap/req.* 2>/dev/null | "$G" -c . || true)
[ -n "$nreq" ] || nreq=0
if [ "$nreq" = "0" ]; then
    t_ok "zero model requests: whitespace-wrapped commands stayed commands"
else
    t_fail "$nreq model request(s) -- a slash command with surrounding blanks was \
sent as a prompt: $(head_bytes 200 < "$tmp"/cap/req.1 2>/dev/null)"
fi

# ---- 3: and they were actually HANDLED, not merely swallowed ---------------
# Zero requests would also be true of a build that discarded the line. `/markdown`
# prints its new state each time, so three toggles must leave three notices.
nmd=$("$G" -c 'markdown rendering' "$tmp/out.log" 2>/dev/null || true)
[ -n "$nmd" ] || nmd=0
if [ "$nmd" -ge 3 ]; then
    t_ok "all three /markdown forms were handled ($nmd status lines)"
else
    t_fail "only $nmd 'markdown rendering' notices for three toggles -- the \
command was dropped rather than dispatched"
fi

# ---- 4: " /exit" quits ----------------------------------------------------
# The one that matters most: the documented way out must survive a stray space.
# ptydrive's waitexit fails the run if the process never leaves, so a clean rc
# here IS the assertion -- and check 1 proves the script was delivered, so this
# cannot pass by never having typed it.
if [ "$rc" -eq 0 ]; then
    t_ok "a leading space does not stop /exit from quitting"
else
    t_fail "the session did not exit (rc=$rc) -- ' /exit' was treated as a prompt, \
so the documented way out is gone: $(tail -3 "$tmp/out.log" 2>/dev/null | tr '\n' ' ' | head_bytes 160)"
fi

t_done
