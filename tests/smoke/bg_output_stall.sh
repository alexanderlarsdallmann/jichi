#!/bin/sh
# smoke: a background command's output no longer needs a pipe reader (M940).
#
# THE DEFECT. jc_bg_start used to wire the child's stdout/stderr to a pipe
# that jichi reads at tool-call boundaries. A child that prints more than the
# pipe holds and keeps going while the model is in a long call blocks in
# write(2) and shows up "running" minutes after it should be done -- the
# observed 2026-10-12 bg-output stall (a 13-hour make ci). The fix spools child
# output to a file (~/.jichi.d/bgspool/bg-N.out), which the child writes with
# no reader at all. Design: docs/internal/2026-10-12-bg-spooled-design.md;
# diagnosis: docs/internal/2026-10-12-bg-output-stall-handoff.md.
#
# THE TOOTH (sentinel). The background command prints ~263 KiB (20000 lines --
# far more than the default 64 KiB pipe, and more than a single model read can
# drain), then writes a sentinel FILE as its very last act. The model starts it,
# pauses ~2 s (a long foreground/model gap), reads once, and answers.
#   - old pipe code: the child fills the 64 KiB pipe and BLOCKS in write(2)
#     mid-stream; the one read drains 64 KiB but the child is far from done, so
#     it never reaches the sentinel -> sentinel ABSENT (red).
#   - file spool: the ~263 KiB goes to the file (no reader needed), the child
#     runs to the end and writes the sentinel -> sentinel PRESENT (green).
# The spool file is also unlinked once the child is reaped, so the private
# spool dir must be empty at the end.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
sentinel="$ws/bg_done"
spool_dir="$HOME/.jichi.d/bgspool"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"seq 1 20000 | sed 's/^/bgstall_/'; touch '$sentinel'","run_in_background":true}
rule
  count 2
  delay 2000
  tool read_background_output {"id":1}
rule
  count 3
  text BGSTALL_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

t0=$(date +%s)
out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "print 20000 lines in the background, then show me the end" < /dev/null)
rc=$?
t1=$(date +%s)
mm_stop

case "$out" in
    *BGSTALL_DONE*) t_ok "the background 263 KiB run returned to the model" ;;
    *) t_fail "turn incomplete (rc=$rc, $((t1-t0))s): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# THE TEETH: the child reached its final act (wrote the sentinel), i.e. it was
# not blocked in write(2) on a full pipe while the model "thought".
if [ -f "$sentinel" ]; then
    t_ok "the child finished (sentinel written): not pipe-blocked"
else
    t_fail "child did not finish: sentinel absent (blocked in write(2))"
fi

# The spool file is unlinked once the child is reaped; the private dir must be
# empty (a lingering file would mean the child is still blocked and unreaped).
if [ -d "$spool_dir" ] && [ -z "$(ls -A "$spool_dir" 2>/dev/null)" ]; then
    t_ok "the spool was cleaned up (child exited, dir empty)"
else
    t_fail "spool residue / child blocked: $(ls -A "$spool_dir" 2>/dev/null | head_bytes 120)"
fi
