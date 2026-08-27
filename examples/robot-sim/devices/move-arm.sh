#!/bin/sh
# move-arm.sh -- a KINETIC actuator: set the arm joint angle. $JICHI_ARG_JOINT,
# $JICHI_ARG_ANGLE. Logs to actuation.log.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
S="$here/state"
J="${JICHI_ARG_JOINT:-0}"
A="${JICHI_ARG_ANGLE:-0}"
echo "$A" > "$S/arm_$J"
printf '%s arm joint=%s angle=%s\n' "$(date -u +%H:%M:%SZ)" "$J" "$A" \
    >> "$here/actuation.log"
echo "arm joint $J -> $A deg"
