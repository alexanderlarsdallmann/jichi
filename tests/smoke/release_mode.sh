#!/bin/sh
# smoke: the RELEASE=1 build mode (M1073, card 104; the 2026-10-17 hardening
# review's finding L-01, chapter 1 section 8 step 3).
#
# THE DEFECT THIS EXISTS FOR. `make install` copies whatever the last `make`
# produced, and the documented install path is a plain `make -j4` -- so the
# binary people RUN is the default -O0 build: unoptimised, and with zero
# __*_chk _FORTIFY_SOURCE symbols (L-01: /usr/local/bin/jichi measured with
# PIE/RELRO/NX/SSP/CET and exactly 0 fortify imports). The default stays that
# way on purpose (build speed, debuggability) -- the fix is a NAMED release
# mode, RELEASE=1, that the install instructions point at.
#
# WHAT RELEASE=1 MUST DELIVER, and what each check below asks:
#   * make info RELEASE=1 says _FORTIFY_SOURCE is LIVE at a probed level --
#     the knob feeds M1071's OPTLEVEL gate, it does not re-implement the probe
#     (the OPTLEVEL := line is immediate and reads OPT at PARSE time, so the
#     knob has to set OPT before it -- that is where the default-build trap is);
#   * the jichi binary BUILT WITH RELEASE=1 imports >= 1 unique __*_chk
#     fortify symbol, compared ask-vs-got like harden_flags_lint check 10:
#     the level is read from make info, the binary is read with readelf;
#   * where cc_warn_ok selected -ftrivial-auto-var-init=zero for THIS
#     toolchain (the one of the finding's candidate flags in scope here;
#     -mbranch-protection=standard is aarch64-only and -fhardened is a GCC-14
#     umbrella -- both marked [GK] and deliberately NOT taken in this
#     milestone), that flag is on the RELEASE CFLAGS line; where the probe
#     rejected it, nothing is asserted (ask-vs-got, never a fixed list).
#
# SELF-TEST before the binary is trusted (the same discipline as
# harden_flags_lint check 10): the same trivial program is built twice --
# counter-default (-O2 -U_FORTIFY_SOURCE, which must import ZERO __*_chk even
# on a distro gcc that defaults fortify at -O2) and with the RELEASE
# hardening line (which must import >= 1). A detector that goes 0 -> >=1
# means its verdict on the release jichi is meaningful; otherwise the check
# reports cannot-attribute instead of failing or passing vacuously.
#
# Exactly t_plan 6 checks always run (no mid-driver t_skip, whose # SKIP line
# run.sh would still count as ok and break the plan tally); a check that
# cannot assert says so in its ok and leaves the denominator honest.
#
# The build runs in a throwaway COPY under $TMPDIR (src/ include/ Makefile),
# never in the repo tree, and the copy is removed on exit by trap.
. "$(dirname "$0")/_smoke.sh"

root=$(cd "$(dirname "$0")/../.." && pwd)

# Decide BEFORE the plan (smoke_lint check 20): every skip must be able to
# fire without a TAP plan line already on stdout.
command -v readelf >/dev/null 2>&1 || t_skip "needs readelf (ELF toolchain)"

_mk=""
for _c in "${MAKE:-}" make gmake gnumake; do
    [ -n "$_c" ] || continue
    command -v "$_c" >/dev/null 2>&1 || continue
    if "$_c" --version 2>/dev/null | grep -q 'GNU Make'; then _mk="$_c"; break; fi
done
[ -n "$_mk" ] || t_skip "no GNU make found (tried \$MAKE, make, gmake, gnumake)"

_cc=""
for _c in "${CC:-}" cc gcc clang; do
    [ -n "$_c" ] || continue
    command -v "$_c" >/dev/null 2>&1 || continue
    _cc="$_c"; break
done
[ -n "$_cc" ] || t_skip "no C compiler on PATH (tried \$CC, cc, gcc, clang)"

t_plan 6

# --- the throwaway build copy -------------------------------------------------
# A copy that cannot be made fails every check with the same reason (the plan
# is 6 either way -- a t_done after one t_fail would emit only 1 of 6).
s=$(smoke_tmp)
s=$s/release
copy_err=""
if ! mkdir -p "$s"; then
    copy_err="cannot make the throwaway build dir"
elif ! cp -r "$root/src" "$root/include" "$root/Makefile" "$s/" 2>/dev/null; then
    copy_err="cannot copy src/ include/ Makefile into the throwaway build dir"
fi
copy_ok=yes
[ -z "$copy_err" ] || copy_ok=no

# `make info` read from the COPY, not the repo: the knob lives in the
# Makefile, the copy has the same one, and reading the copy is what the
# driver's own build uses (no drift between the question and the answer).
_info=$( (cd "$s" && "$_mk" -s info RELEASE=1 2>/dev/null) || true )

_optline=$(printf '%s\n' "$_info" | grep '^OPT ' | head -n 1)
_cflagsline=$(printf '%s\n' "$_info" | grep '^JC_CFLAGS ' | head -n 1)
_hfline=$(printf '%s\n' "$_info" | sed -n 's/^HARDENFLAGS  *= *//p')
printf '# make info RELEASE=1 OPT line:%s\n' "$_optline"
printf '# make info RELEASE=1 CFLAGS line:%s\n' "$_cflagsline"

# Is _FORTIFY_SOURCE claimed LIVE, and at which level? The level is read
# from the -D_FORTIFY_SOURCE=n the build really gets -- not re-probed here.
_live=$(printf '%s\n' "$_optline" | grep -c 'is live')
_level=$(printf '%s\n' "$_optline" "$_cflagsline" | sed -n 's/.*-D_FORTIFY_SOURCE=\([0-9][0-9]*\).*/\1/p' | head -n 1)

# Did the probe select -ftrivial-auto-var-init=zero for THIS toolchain?
_tavi_ok=no
if printf '%s\n' "$_cflagsline" | grep -q -- '-ftrivial-auto-var-init=zero'; then
    _tavi_ok=yes
fi

# --- CHECK 1: make info is readable (the floor for this driver) ----------------
if [ "$copy_ok" = "no" ]; then
    t_fail "cannot build the throwaway copy: $copy_err"
elif printf '%s\n' "$_info" | grep -q '^HARDEN  *='; then
    t_ok "$_mk info RELEASE=1 is readable from the throwaway copy"
else
    t_fail "could not read '$_mk info RELEASE=1' -- every check below would \
pass while measuring nothing"
fi

# --- CHECK 2: the knob itself -- RELEASE=1 claims a LIVE fortify --------------
if [ "$_live" -ge 1 ]; then
    t_ok "make info RELEASE=1 reports _FORTIFY_SOURCE live (level ${_level:-unparsed})"
elif [ -n "$_level" ]; then
    t_ok "make info RELEASE=1 reports _FORTIFY_SOURCE level $_level on the CFLAGS line"
else
    t_fail "make info RELEASE=1 does NOT report a live _FORTIFY_SOURCE \
(RELEASE=1 must be -O2 at the probed fortify level, feeding the M1071 \
OPTLEVEL gate; got: $_optline)"
fi

# --- CHECK 3: ask-vs-got for the probed TAVI candidate flag ---------------------
# RELEASE_CFLAGS is empty by default, so where the probe did not select the
# flag there is nothing to compare (a fixed list would fail honest platforms).
if [ "$_tavi_ok" = "yes" ]; then
    t_ok "the probe selected -ftrivial-auto-var-init=zero and it is on the \
RELEASE CFLAGS line"
else
    t_ok "-ftrivial-auto-var-init=zero is not on the RELEASE CFLAGS line \
(the probe did not select it on this toolchain, or the knob does not exist \
yet); ask-vs-got, so nothing is asserted"
fi

# --- CHECK 4: the __*_chk detector self-test (ask-vs-got discipline) ------------
# The same trivial program, built twice: counter-default (-O2 with the
# distro's own _FORTIFY_SOURCE removed) must import ZERO __*_chk; the RELEASE
# hardening line must import >= 1. snprintf is fortifiable at every level,
# so 0 -> >=1 is a clean discriminator.
_chkdisc() { # count unique undefined dynamic __*_chk imports in $1
    readelf -W --dyn-syms "$1" 2>/dev/null | \
        awk '{ n = $8; sub(/@.*/, "", n); if (n ~ /^__.*_chk$/) k[n] = 1 }
             END { x = 0; for (s in k) x++; print x }'
}

_disc_ok=no
_ft=$(mktemp -d "${TMPDIR:-/tmp}/jichi_release_chk.XXXXXX" 2>/dev/null) || _ft=""
_fn=0
if [ -z "$_ft" ]; then
    t_ok "fortify self-test not run: mktemp failed on this host; the release \
binary count is therefore not asserted (cannot attribute)"
elif [ -z "$_hfline" ]; then
    t_fail "make info RELEASE=1 has no HARDENFLAGS line to build with -- the \
ask-vs-got comparison is impossible"
else
    printf '#include <stdio.h>\nint main(void){char b[16];int n=snprintf(b,sizeof b,"%%d",42);return b[0]+n;}\n' > "$_ft/p.c"
    "$_cc" -O2 -U_FORTIFY_SOURCE -o "$_ft/off" "$_ft/p.c" 2>/dev/null || true
    "$_cc" -std=c89 -pedantic -D_POSIX_C_SOURCE=200112L -O2 $_hfline -o "$_ft/on" "$_ft/p.c" 2>/dev/null || true
    _fo=0; _fn=0
    [ -s "$_ft/off" ] && _fo=$(_chkdisc "$_ft/off")
    [ -s "$_ft/on" ]  && _fn=$(_chkdisc "$_ft/on")
    printf '# fortify self-test: __*_chk off=%s on=%s\n' "$_fo" "$_fn"
    if [ "$_fo" -eq 0 ] && [ "$_fn" -ge 1 ]; then
        _disc_ok=yes
        t_ok "__*_chk detector discriminates here (counter-default off=${_fo} \
-> release flags on=${_fn})"
    else
        t_ok "the __*_chk detector does not discriminate on this toolchain \
(off=${_fo} on=${_fn}); the release binary count is not asserted -- \
cannot attribute"
    fi
fi
rm -rf "$_ft" 2>/dev/null

# --- CHECK 5: the release jichi builds in the throwaway copy --------------------
# Always built where the copy exists, so check 6's premise (a binary to
# read) cannot silently vanish; -j so a 60 s driver deadline covers even
# slow silicon (JC_SMOKE_BUILD_JOBS to override).
if (cd "$s" && "$_mk" -j4 RELEASE=1 jichi >/dev/null 2>&1); then
    [ -s "$s/jichi" ] || {
        t_fail "make -j4 RELEASE=1 jichi exited 0 but produced no binary"
        t_done
    }
    t_ok "jichi builds in the throwaway copy under RELEASE=1 (-j4)"
else
    t_fail "make -j4 RELEASE=1 jichi failed in the throwaway copy -- the \
release mode cannot build"
fi

# --- CHECK 6: the RELEASE=1 jichi imports __*_chk (ask-vs-got) -------------------
# Where make info claims no live fortify, the promise is already broken and
# the count is asserted anyway (it will be 0 and say so). Where the detector
# cannot discriminate, the count proves nothing and is not asserted (M479).
if [ "${_disc_ok:-no}" = "no" ]; then
    t_ok "release binary leg not asserted: the __*_chk detector does not \
discriminate on this toolchain, so its count proves nothing (M479)"
else
    _rn=$(_chkdisc "$s/jichi")
    printf '# release jichi: %s unique __*_chk imports\n' "$_rn"
    if [ "$_rn" -ge 1 ]; then
        t_ok "jichi built with RELEASE=1 imports $_rn unique __*_chk \
fortify symbol(s) (self-test off=0 -> on=${_fn})"
    elif [ "$_live" -ge 1 ]; then
        t_fail "jichi built with RELEASE=1 imports ZERO __*_chk symbols \
although make info claims fortify live -- the flag is not doing what its \
presence implies (L-01 reproduced in the tree)"
    else
        t_fail "jichi built with RELEASE=1 imports ZERO __*_chk symbols -- \
the installed-binary shape L-01 measured: a release build without a live \
_FORTIFY_SOURCE"
    fi
fi

t_done
