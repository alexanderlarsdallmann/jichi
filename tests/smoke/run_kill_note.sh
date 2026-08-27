#!/bin/sh
# smoke: a byte-cap kill names the capture limit, never the memory budget (M342).
#
# Found by the seam-1 measurement (docs/analysis/2026-08-09-three-seams.md): a
# driven subagent re-issued the same over-cap `git log` 32 times in ONE turn,
# escalating -N from 300 to 20,000,000 -- because every kill told it
# "[stopped: exceeded the memory budget]". run_command_watched shared one
# `killed` flag across three causes (byte cap, memory watchdog, abort) and
# gave them all the memory-budget label, so the model was debugging a false
# diagnosis. M325's rule, applied one layer down: name the limit that fired.
#
# The assertions read mockmodel's captured SECOND request (req.2) -- the tool
# result as the model actually sees it -- and are anchored on the kill note's
# own shape ("stopped: output exceeded the"). NOT on "capture limit": the tool
# layer's separate truncation note contains those words too, so an unanchored
# grep would pass without the fix (the M293/M296 lesson).
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# One shell call whose output exceeds the 1 MB capture ceiling within
# milliseconds, then a closing text turn once the tool result comes back.
# The child is bounded by `head -n` even if the kill path itself were
# broken. -n, not -c: this command runs inside JICHI's /bin/sh, not in
# this driver, so the head_bytes helper is not in scope -- and OpenBSD's
# head has no -c at all. 100000 lines x 17 bytes = 1.7 MB, over the cap.
cat > "$tmp/k.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"yes 0123456789abcdef | head -n 100000"}
rule
  match "\"role\":\"tool\""
  text KILL_DONE
EOF
mm_start "$tmp/k.mm" "$tmp/cap" 2
# runTimeout routes the command through the watched fork path (the one that
# kills at the cap); without it the popen path truncates without killing.
write_config "$tmp/c.json" "$MM_PORT" '"runTimeout":30'
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session -p "big" < /dev/null) >/dev/null 2>&1
mm_stop

req="$tmp/cap/req.2"

# --- 1: the artifact exists (M310: absence proves nothing without it) --------
if [ -s "$req" ]; then
    t_ok "the tool result went back to the model (req.2 captured)"
else
    t_fail "no second request captured -- nothing below can be trusted"
fi

# --- 2: the kill note names the cause that actually fired --------------------
if grep -q "stopped: output exceeded the" "$req" 2>/dev/null; then
    t_ok "the kill note names the byte capture limit"
else
    t_fail "the kill note does not say the output cap fired"
fi

# --- 3: ... and no longer claims a memory-budget kill -------------------------
if grep -q "exceeded the memory budget" "$req" 2>/dev/null; then
    t_fail "a byte-cap kill still claims 'exceeded the memory budget'"
else
    t_ok "no false memory-budget claim on a byte-cap kill"
fi

# --- 4: the factual part is unchanged: the child WAS SIGKILLed ----------------
if grep -q "exit status: 137" "$req" 2>/dev/null; then
    t_ok "exit status 137 (SIGKILL) still reported"
else
    t_fail "exit status 137 missing from the tool result"
fi

t_done
