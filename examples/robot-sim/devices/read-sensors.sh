#!/bin/sh
# read-sensors.sh -- a READ-ONLY sensor: assemble a JSON snapshot from the
# flat-file world state. No arguments; jichi reads the JSON from stdout.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
S="$here/state"

get() { cat "$S/$1" 2>/dev/null || echo "$2"; }

printf '{"pose_x":%s,"pose_y":%s,"heading":%s,"battery":%s,"obstacle":%s}\n' \
    "$(get pose_x 0)" "$(get pose_y 0)" "$(get heading 0)" \
    "$(get battery 100)" "$(get obstacle 0)"
