#!/bin/sh
# mock-play.sh -- stand-in for aplay/ffplay: log the audio file jichi handed us
# ($JICHI_AUDIO_FILE), don't touch real hardware. Swap for `aplay -q` on a box
# with a speaker.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
printf '%s play %s\n' "$(date -u +%H:%M:%SZ)" "${JICHI_AUDIO_FILE:-?}" \
    >> "$here/sound.log"
echo "played ${JICHI_AUDIO_FILE:-?}"
