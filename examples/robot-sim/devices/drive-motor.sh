#!/bin/sh
# drive-motor.sh -- a KINETIC actuator: integrate the sim pose and drain the
# battery. Arguments arrive as $JICHI_ARG_* (left/right/duration). Appends a line
# to actuation.log -- the proof-of-motion artifact the tests assert on.
#
# In a real robot this would write a velocity to a serial port or a ROS topic;
# here it just moves numbers in flat files. Either way jichi only reaches it
# through the kinetic gate.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
S="$here/state"

L="${JICHI_ARG_LEFT:-0}"
R="${JICHI_ARG_RIGHT:-0}"
D="${JICHI_ARG_DURATION:-1}"

# Integer-only integration (POSIX sh): forward = (L+R)/2 * duration.
fwd=$(( (L + R) / 2 * D ))
x=$(cat "$S/pose_x" 2>/dev/null || echo 0)
x=$(( x + fwd ))
echo "$x" > "$S/pose_x"

batt=$(cat "$S/battery" 2>/dev/null || echo 100)
batt=$(( batt - D ))
[ "$batt" -lt 0 ] && batt=0
echo "$batt" > "$S/battery"

printf '%s drive L=%s R=%s D=%s -> pose_x=%s battery=%s\n' \
    "$(date -u +%H:%M:%SZ)" "$L" "$R" "$D" "$x" "$batt" >> "$here/actuation.log"
echo "moved: pose_x=$x battery=$batt"
