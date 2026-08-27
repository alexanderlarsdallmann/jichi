#!/bin/sh
# smoke: `doctor` against an offline config -- reports (no hang), exits 1
# on the unreachable model server, and the JSON form parses.
#
# M400 adds the platform note. jichi is verified on Linux only, and doctor is
# the command every page tells a newcomer to run first -- so an unverified host
# must hear it from the tool, not from a page they did not open. The check runs
# on Linux, so it pins the SILENT half: the verified case costs no line. Its
# teeth are proven by perturbing the guard (`if (!jc_platform_is_linux())` ->
# `if (1)`), rebuilding, and watching this go red -- which is also the only way
# to exercise the warning branch on a Linux box.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

# An intentionally unreachable model server (port 9, discard).
write_config "$tmp/config.json" 9

with_deadline 40 "$BIN" --config "$tmp/config.json" doctor \
    < /dev/null > "$tmp/out" 2>&1; rc=$?
if [ $rc -eq 1 ] && [ -s "$tmp/out" ]; then
    t_ok "doctor exits 1 on an unreachable server, without hanging"
else
    t_fail "doctor rc=$rc (want 1); out=$(head_bytes 120 "$tmp/out")"
fi

if grep -q "m" "$tmp/out"; then
    t_ok "doctor names the configured model"
else
    t_fail "doctor output does not mention the model"
fi

if grep -i -q "unreachable" "$tmp/out"; then
    t_ok "doctor reports the server as unreachable"
else
    t_fail "no 'unreachable' in doctor output"
fi

if grep -q "ok," "$tmp/out" || grep -q "problem" "$tmp/out"; then
    t_ok "doctor prints a summary line"
else
    t_fail "no summary line in doctor output"
fi

with_deadline 40 "$BIN" --config "$tmp/config.json" doctor --output json \
    < /dev/null > "$tmp/out.json" 2>/dev/null
if "$SMOKE_TOOLS/jsonq" -q '.' "$tmp/out.json"; then
    t_ok "doctor --output json parses"
else
    t_fail "doctor --output json did not parse"
fi

# M400: the platform note stays silent on a verified platform -- and speaks on
# an unverified one. Both halves, because the driver runs on both now (M459).
#
# doctor decides with `if (!jc_platform_is_linux())`: a NAME check that
# hardcodes "Linux == verified". On FreeBSD it correctly warns, and this check
# -- written when Linux was the only host the tier ever saw -- read that
# correct warning as a failure, with a message insisting the platform "IS the
# verified one". jichi was right; the check was parochial.
#
# RESOLVED (M486). This note used to say the check was "already going stale",
# because the driver had begun running on a FreeBSD guest where jichi builds and
# passes its suites while doctor still called it never-compiled -- and it deferred
# the question to PLATFORMS.md rather than assume. PLATFORMS.md then answered it:
# FreeBSD, NetBSD and OpenBSD all run the FULL gate. Nothing carried that back into
# the C for months, so the product contradicted its own documentation in the first
# place a support conversation looks.
#
# The contract now: a kernel with a Verified row stays SILENT, exactly as Linux
# does, and every other platform warns. jc_platform_verified_row() holds the list
# and portability_lint check 7c pins it to PLATFORMS.md in both directions, so this
# driver asserts the BEHAVIOUR and the lint owns the membership question.
case "$(uname -s)" in
Linux|FreeBSD|NetBSD|OpenBSD)
    if grep -qi 'never been compiled on this platform' "$tmp/out"; then
        t_fail "doctor calls $(uname -s) never-compiled, but PLATFORMS.md carries a Verified row for it"
    else
        t_ok "no platform warning on $(uname -s) (a verified platform stays silent)"
    fi
    ;;
*)
    if grep -qiE 'never been compiled on this platform|not recognised' "$tmp/out"; then
        t_ok "doctor warns on $(uname -s), which jichi does not list as verified"
    else
        t_fail "doctor stayed silent on $(uname -s) -- an unverified platform must say so"
    fi
    ;;
esac

t_done
