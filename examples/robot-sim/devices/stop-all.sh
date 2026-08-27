#!/bin/sh
# stop-all.sh -- the SAFE-STATE tool. Kinetic (it commands the hardware), but
# listed in kineticCommandsAllow so it stays callable even when every other
# kinetic action is refused (the E-stop guarantee). Always fast, no arguments.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
printf '%s STOP-ALL (safe state)\n' "$(date -u +%H:%M:%SZ)" \
    >> "$here/actuation.log"
echo "all actuators commanded to safe state"
