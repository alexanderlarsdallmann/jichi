#!/bin/sh
# set-led.sh -- a NON-KINETIC actuator (deliberate contrast): it changes an
# indicator, moving no mass and emitting negligible energy, so it is a plain
# mutating tool (ASK in chat, no kinetic gate). $JICHI_ARG_STATE = on|off.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
echo "${JICHI_ARG_STATE:-off}" > "$here/state/led"
echo "led ${JICHI_ARG_STATE:-off}"
