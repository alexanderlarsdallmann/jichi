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

t_plan 8
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
#
# M695: it did not. This case hardcoded "Linux|FreeBSD|NetBSD|OpenBSD" -- a FOURTH
# copy of the list, beside the C, the lint and the setup wizard, in the driver whose
# own header says the lint owns membership. It also had no branch for **Partly
# verified**, so on Cygwin and MSYS2 it took the else and asserted that doctor DOES
# warn "never compiled" -- passing green while the product said something false
# about a platform it had just been compiled on. And its green message claimed "no
# platform warning" while only grepping for one string, which was untrue on FreeBSD,
# where a second platform warning was printed the whole time.
#
# The expectation is derived from PLATFORMS.md now, by the `uname -s = <token>`
# each measured row declares. Comparison is on the FAMILY -- the token up to its
# first '-' -- because the Windows layers append the host's build number and
# CYGWIN_NT-10.0-26200 on the page must still match CYGWIN_NT-10.0-88888 here.
_pl="$SMOKE_ROOT/docs/PLATFORMS.md"
_fam() { printf '%s' "${1%%-*}"; }
_me=$(_fam "$(uname -s)")

_tier_tokens() {   # $1 = "Verified" | "Partly verified"
    awk -v h="### $1" '$0 == h {f=1; next} f && /^### /{exit} f' "$_pl" \
      | grep -o 'uname -s = [A-Za-z0-9_.-]*' | sed 's/^uname -s = //'
}
_tier=unknown
for _t in $(_tier_tokens "Verified"); do
    [ "$(_fam "$_t")" = "$_me" ] && _tier=verified
done
for _t in $(_tier_tokens "Partly verified"); do
    [ "$(_fam "$_t")" = "$_me" ] && _tier=partly
done

# Floor the derivation itself: if the page yields no tokens at all, every host
# reads "unknown" and the branches below assert nothing. That is the vacuous
# pass this family of check keeps producing.
_ntok=$( { _tier_tokens "Verified"; _tier_tokens "Partly verified"; } | grep -c . )
if [ "$_ntok" -ge 7 ]; then
    t_ok "the expected verdict is derived from PLATFORMS.md ($_ntok uname declarations; this host reads $_tier)"
else
    t_fail "PLATFORMS.md yielded $_ntok uname declarations (floor 7) -- the expectation \
below is derived from them, so the next check tests nothing until they read"
fi

_nc=$(grep -ci 'never been compiled on this platform' "$tmp/out" || true)
# 'partly verified', not 'partly verified here': this greps the PRODUCT's
# wording, and the tail of that sentence has already changed once. M695 shipped
# "PARTLY verified here (docs/PLATFORMS.md)", which broke setup_keyfile check 27
# (it counts the lowercase word "platform"); fixing that to "PARTLY verified on
# this platform" then broke THIS check, which was grepping for "here". Two
# checks coupled to one sentence, failing in opposite directions. The phrase is
# now the part that carries the meaning, and portability_lint check 7f pins it
# against main.c so the two cannot drift apart again.
_pv=$(grep -ci 'partly verified' "$tmp/out" || true)
case "$_tier" in
verified)
    if [ "$_nc" -eq 0 ] && [ "$_pv" -eq 0 ]; then
        t_ok "no platform verdict line on $(uname -s) (PLATFORMS.md verifies it, and a verified row costs no line)"
    else
        t_fail "doctor calls $(uname -s) never-compiled ($_nc) or partly verified ($_pv), but PLATFORMS.md carries a Verified row for it"
    fi
    ;;
partly)
    if [ "$_pv" -ge 1 ] && [ "$_nc" -eq 0 ]; then
        t_ok "doctor calls $(uname -s) partly verified, which is what PLATFORMS.md says"
    else
        t_fail "PLATFORMS.md partly-verifies $(uname -s), but doctor said partly=$_pv never-compiled=$_nc -- a measured platform must not be told it was never compiled"
    fi
    ;;
*)
    if grep -qiE 'never been compiled on this platform|not recognised' "$tmp/out"; then
        t_ok "doctor warns on $(uname -s), which PLATFORMS.md does not carry a row for"
    else
        t_fail "doctor stayed silent on $(uname -s) -- an unmeasured platform must say so"
    fi
    ;;
esac

# The merge's whole point: ONE platform statement, not two. doctor used to make
# a never-compiled claim AND a separate "platform is not Linux" claim; on Cygwin
# both fired and both were wrong, and on FreeBSD they contradicted each other.
_stale=$(grep -ci 'platform is not Linux' "$tmp/out" || true)
if [ "$_stale" -eq 0 ]; then
    t_ok "doctor states its platform verdict once, not twice"
else
    t_fail "doctor still carries the retired 'platform is not Linux' line ($_stale) -- \
that is a second, Linux-only verdict beside the one PLATFORMS.md owns"
fi

t_done
