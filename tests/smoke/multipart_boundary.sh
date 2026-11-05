#!/bin/sh
# smoke: `doctor` reports WHICH entropy source the identifiers use (C-10,
# M1121, half B). The uuid path (session ids, the multipart upload boundary)
# reads /dev/urandom and falls back to time()-seeded rand() when it cannot --
# and the fallback used to be silent, which is exactly when a boundary becomes
# guessable, so the one place an operator reads posture had to say which source
# it will use.
#
# Check 1 is the BEHAVIOUR: on this host (which has a readable /dev/urandom)
# doctor must print the strong-source line. Check 2 is a STRUCTURAL pin: the
# probe and the doctor wiring exist in the tree, so a build that loses either
# goes red (the reporting half has no red on a host where urandom works, so
# the pin carries its teeth). POSIX sh, doctor.sh is the precedent.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# An intentionally unreachable model server (port 9, discard), as doctor.sh.
write_config "$tmp/config.json" 9

with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/out" 2>&1

if grep -q 'identifiers use /dev/urandom' "$tmp/out"; then
    t_ok "doctor reports the strong source (identifiers use /dev/urandom)"
else
    t_fail "doctor output carries no entropy-source line: $(head_bytes 200 "$tmp/out")"
fi

if $G -q 'jc_entropy_probe' src/util/jc_uuid.c && \
   $G -q 'jc_entropy_probe' src/main.c; then
    t_ok "the probe (jc_uuid.c) and the doctor wiring (main.c) are present"
else
    t_fail "structural pin: jc_entropy_probe missing from jc_uuid.c or its doctor call from main.c"
fi

t_done
