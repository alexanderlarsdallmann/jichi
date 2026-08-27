#!/bin/sh
# mock-record.sh -- stand-in for arecord: create a tiny placeholder wav at the
# path jichi appended as the final argument. Swap for `arecord -q -f cd` on a box
# with a microphone (jichi stops it after the requested seconds).
set -eu
out=""
for a in "$@"; do out="$a"; done
[ -n "$out" ] && printf 'RIFFmock-recording' > "$out"
echo "recorded ${JICHI_AUDIO_SECONDS:-?}s -> $out"
