#!/bin/sh
# world-tick.sh -- optional world simulator. Drifts the robot and occasionally
# raises an obstacle, so a patrol mission has something to react to. Run it in
# the background (jichi's run_terminal_command run_in_background, or a plain
# `while :; do ./world-tick.sh; sleep 5; done &`). NOT a jichi tool.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
S="$here/state"
y=$(cat "$S/pose_y" 2>/dev/null || echo 0)
echo "$(( y + 1 ))" > "$S/pose_y"
# Toggle an obstacle every ~3rd tick using pose_y parity.
if [ "$(( y % 3 ))" -eq 0 ]; then echo 1 > "$S/obstacle"; else echo 0 > "$S/obstacle"; fi
