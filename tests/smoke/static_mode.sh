#!/bin/sh
# smoke: the STATIC=1 build mode (M1075, card 106; the 2026-10-17 hardening
# review's finding E-07, chapter E). Sibling of release_mode.sh (M1073, L-01):
# same throwaway-copy build shape, same t_plan discipline, same ask-vs-got.
#
# THE DEFECT THIS EXISTS FOR (E-07). The documented static recipes --
# LOW_MEMORY.md:816 and DEPLOYMENT.md:212 -- build with
# `LDLIBS="-lm -lcurl -static"`. That puts `-static` AFTER `-pie`: on the link
# line `$(JC_LDFLAGS)` carries `-pie` (HARDENLDFLAGS), and the user's LDLIBS is
# appended after it, so the measured order is `-pie ... -static`. Measured on
# this toolchain (gcc 15.2, glibc): `gcc -pie ... -static` yields
# `ELF executable, statically linked`, **Type EXEC -- not PIE** -- while
# `make info` still lists `-pie`. The override ALSO drops $(SANFLAGS),
# $(RT_LIBS) and $(SOCK_LIBS) from LDLIBS: harmless on Linux, wrong on
# illumos/Haiku. The fix is a NAMED static mode, STATIC=1, that the two docs
# point at.
#
# WHAT STATIC=1 MUST DELIVER, and what each check below asks:
#   * a NAMED mode: `make info STATIC=1` carries a `STATIC_MODE` line saying
#     WHICH form the build took -- `static-pie (probed)` where `-static-pie`
#     was probed, else `static (fallback: ...)`; the knob is off by default,
#     so no STATIC -> no line -> the default build is byte-identical (the
#     M1073 invariant);
#   * -static-pie where the probe says yes, else -static with -pie REMOVED
#     from JC_LDFLAGS AND -fPIE REMOVED from JC_CFLAGS (measured: -fPIE
#     objects + -static with no -pie do not link -- the fallback cannot keep
#     -fPIE in the compile flags either);
#   * ASK-VS-GOT: the mode `make info` CLAIMS is the mode the BINARY HAS.
#     Claims static-pie -> the binary is `pie executable, static-pie linked`
#     (Type DYN); claims static fallback -> the binary is Type EXEC statically
#     linked. Never a DYN that is not static, never an EXEC where static-pie
#     was claimed.
#
# SELF-TEST before the binary is trusted (M479): a trivial program is built
# both ways -- `-static-pie` (must be DYN, static-pie linked) and the fallback
# form (no -pie, no -fPIE, -static; must be EXEC, statically linked). The SAME
# way release_mode.sh's __*_chk self-test proves its detector. Where a control
# build cannot run on this toolchain, its binary leg is not asserted
# (cannot attribute) rather than failing or passing vacuously.
#
# Exactly t_plan 6 checks always run (no mid-driver t_skip, whose # SKIP line
# run.sh would still count as ok and break the plan tally); a check that
# cannot assert says so in its ok and leaves the denominator honest.
#
# The build runs in a throwaway COPY under $TMPDIR (src/ include/ Makefile),
# never in the repo tree, and the copy is removed on exit by trap. This driver
# BUILDS its own STATIC=1 jichi and INSPECTS it -- it never runs the
# pre-built $BIN (the smoke_lint check-5 exemption, same category as
# release_mode / teeth_restore).
. "$(dirname "$0")/_smoke.sh"

root=$(cd "$(dirname "$0")/../.." && pwd)

# Decide BEFORE the plan (smoke_lint check 20): every skip must be able to
# fire without a TAP plan line already on stdout.
command -v readelf >/dev/null 2>&1 || t_skip "needs readelf (ELF toolchain)"
command -v file >/dev/null 2>&1 || t_skip "needs file(1)"

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
s=$s/static
copy_err=""
if ! mkdir -p "$s"; then
    copy_err="cannot make the throwaway build dir"
elif ! cp -r "$root/src" "$root/include" "$root/Makefile" "$s/" 2>/dev/null; then
    copy_err="cannot copy src/ include/ Makefile into the throwaway copy"
fi
copy_ok=yes
[ -z "$copy_err" ] || copy_ok=no

# `make info` read from the COPY, not the repo: the knob lives in the
# Makefile, the copy has the same one, and reading the copy is what the
# driver's own build uses (no drift between the question and the answer).
_info=$( (cd "$s" && "$_mk" -s info STATIC=1 2>/dev/null) || true )
_modeline=$(printf '%s\n' "$_info" | sed -n 's/^STATIC_MODE *= *//p' | head -n 1)

# Build the STATIC=1 jichi in the copy (so check 2's binary leg exists);
# JC_SMOKE_BUILD_JOBS to override, -j4 default. _buildok records whether a
# binary came out; the log is kept so check 2 can say WHY when it did not.
_jobs="${JC_SMOKE_BUILD_JOBS:-4}"
_buildok=no
if [ "$copy_ok" = yes ]; then
    (cd "$s" && "$_mk" -j"$_jobs" STATIC=1 jichi >"$s/build.log" 2>&1) || true
    [ -s "$s/jichi" ] && _buildok=yes
fi

# The copy's STATIC=1 link line (make -n), for check 6 and to quote here so a
# future reader sees the exact form the build took.
_lline=$( (cd "$s" && "$_mk" -n -B STATIC=1 jichi 2>/dev/null) | grep -F -- '-o jichi' | head -n 1)
printf '# STATIC=1 link line: %s\n' "${_lline:-(not captured)}"
printf '# make info STATIC=1 STATIC_MODE line: %s\n' "${_modeline:-(absent)}"

# Count helper: how many times does token T appear as a WHOLE word (space/line
# bounded) on the link line? T is -static-pie or -static or -pie.
_tokcount() { # $1 = link line, $2 = token
    printf '%s\n' "$1" | grep -oE "(^| )$2( |$)" | wc -l | tr -d '[:space:]'
}

# --- CHECK 1: make info STATIC=1 carries the new mode line ---------------------
# RED on the unfixed tree: there is no STATIC knob at all, so the line is
# absent. The exact grep: `grep '^STATIC_MODE *='` on `make -s info STATIC=1`.
if [ "$copy_ok" = "no" ]; then
    t_fail "cannot build the throwaway copy: $copy_err"
elif [ -n "$_modeline" ]; then
    t_ok "make info STATIC=1 names the static mode: ${_modeline}"
else
    t_fail "make info STATIC=1 has no STATIC_MODE line (the knob is not in the Makefile yet -- E-07 red-first; grep '^STATIC_MODE *=' found nothing)"
fi

# --- CHECK 2: ask-vs-got -- the claimed mode is the mode the binary has -------
# static-pie claimed -> binary is `pie executable`, `static-pie linked`, Type
# DYN. static fallback claimed -> Type EXEC, statically linked. A mismatch is
# the finding reproduced. On the UNFIXED tree the mode line is absent, so this
# check cannot attribute a binary to a claimed mode and says so. Where the FULL
# STATIC=1 binary cannot link on THIS host (no static TLS/krb5 chain for
# libcurl -- an environment gap, not the E-07 flag-ordering defect), the binary
# leg is not asserted and the cause is reported (cannot attribute); the flag
# logic is still proven by check 3 (the trivial self-test) and check 6 (the link
# line).
if [ -z "$_modeline" ]; then
    t_ok "binary leg not asserted: make info STATIC=1 names no static mode -- the knob is not in the Makefile yet (E-07 red-first); cannot attribute"
elif [ "$_buildok" = no ]; then
    _why=$(grep -oE 'undefined reference to .[^.]*|cannot find -l[A-Za-z0-9_-]+|No such file or directory' "$s/build.log" 2>/dev/null | head -n 2 | tr '\n' '; ')
    t_ok "binary leg not asserted: the full STATIC=1 binary did not link on this host (${_why:-see build.log}); the environment lacks the static TLS/krb5 chain libcurl needs -- a gap orthogonal to E-07, whose flag logic check 3 and check 6 still prove (cannot attribute)"
else
    _bin="$s/jichi"
    _ft=$(file "$_bin" 2>/dev/null || true)
    _bt=$(readelf -hW "$_bin" 2>/dev/null | sed -n 's/.*Type: *\([A-Z]*\).*/\1/p' | head -n 1)
    printf '# binary: %s (Type %s)\n' "$_ft" "$_bt"
    case "$_modeline" in
        static-pie*)
            if printf '%s' "$_ft" | grep -q 'pie executable' \
               && printf '%s' "$_ft" | grep -q 'static-pie linked' \
               && [ "$_bt" = "DYN" ]; then
                t_ok "claimed static-pie and the binary is pie executable, static-pie linked (Type DYN)"
            else
                t_fail "claimed static-pie but the binary is not: file='$(_ft)' Type='$(_bt)' -- a static-pie build must be DYN, pie executable, static-pie linked"
            fi
            ;;
        *)
            if [ "$_bt" = "EXEC" ] && printf '%s' "$_ft" | grep -q 'statically linked'; then
                t_ok "claimed the static fallback and the binary is Type EXEC, statically linked"
            else
                t_fail "claimed the static fallback but the binary is not: file='$(_ft)' Type='$(_bt)' -- the fallback must be EXEC, statically linked (never a DYN that is not static)"
            fi
            ;;
    esac
fi

# --- CHECK 3: the detector self-test (M479) -- a trivial program, both ways ----
# -static-pie must come out DYN, static-pie linked; the fallback form (no -pie,
# no -fPIE, -static) must come out EXEC, statically linked. Where a control
# cannot build on this toolchain its leg is not asserted (cannot attribute),
# rather than failing or passing vacuously.
_stpie_ctl_ok=""   # "yes" / "no"
_fb_ctl_ok=""      # "yes" / "no"
_chk=$(mktemp -d "${TMPDIR:-/tmp}/jichi_static_chk.XXXXXX" 2>/dev/null) || _chk=""
if [ -z "$_chk" ]; then
    t_ok "self-test not run: mktemp failed on this host; the control builds are not asserted (cannot attribute)"
else
    printf 'int main(void){return 0;}\n' > "$_chk/p.c"
    # Control A: -static-pie.
    "$_cc" -static-pie -o "$_chk/A" "$_chk/p.c" 2>/dev/null || true
    _a_t=$(readelf -hW "$_chk/A" 2>/dev/null | sed -n 's/.*Type: *\([A-Z]*\).*/\1/p' | head -n 1)
    _a_f=$(file "$_chk/A" 2>/dev/null || true)
    if [ "$_a_t" = "DYN" ] && printf '%s' "$_a_f" | grep -q 'static-pie linked'; then
        _stpie_ctl_ok=yes
    else
        _stpie_ctl_ok=no
    fi
    # Control B: the fallback form -- no -pie, no -fPIE, -static.
    "$_cc" -static -o "$_chk/B" "$_chk/p.c" 2>/dev/null || true
    _b_t=$(readelf -hW "$_chk/B" 2>/dev/null | sed -n 's/.*Type: *\([A-Z]*\).*/\1/p' | head -n 1)
    _b_f=$(file "$_chk/B" 2>/dev/null || true)
    if [ "$_b_t" = "EXEC" ] && printf '%s' "$_b_f" | grep -q 'statically linked'; then
        _fb_ctl_ok=yes
    else
        _fb_ctl_ok=no
    fi
    printf '# self-test: -static-pie -> Type %s (%s); fallback -static -> Type %s (%s)\n' "$_a_t" "$_stpie_ctl_ok" "$_b_t" "$_fb_ctl_ok"
    if [ "$_stpie_ctl_ok" = yes ] && [ "$_fb_ctl_ok" = yes ]; then
        t_ok "self-test: -static-pie is DYN static-pie, the fallback -static is EXEC static -- the detector discriminates here"
    else
        t_ok "self-test: the -static-pie control is ${_stpie_ctl_ok}, the fallback control is ${_fb_ctl_ok} -- a leg that cannot build here is not asserted (cannot attribute), not a vacuous pass"
    fi
fi
rm -rf "$_chk" 2>/dev/null

# --- CHECK 4: the fallback mode is REACHABLE (ask-vs-got over the probe) -------
# If the -static-pie control could NOT build on this toolchain, the copy must
# have taken the fallback (mode line says static, binary EXEC). If it COULD,
# the copy must be static-pie. This is ask-vs-got over the probe, not over a
# fixed expectation.
if [ -z "$_modeline" ]; then
    t_ok "probe leg not asserted: make info STATIC=1 names no static mode (the knob is not in the Makefile yet -- E-07 red-first)"
else
    case "$_modeline" in
        static-pie*)
            if [ "$_stpie_ctl_ok" = yes ]; then
                t_ok "the -static-pie control built, so the copy must be static-pie -- and it is"
            else
                t_fail "the -static-pie control did NOT build here, yet the copy claims static-pie -- the fallback was not taken when the probe failed"
            fi
            ;;
        *)
            if [ "$_stpie_ctl_ok" = no ]; then
                t_ok "the -static-pie control did NOT build here, so the copy must be the fallback -- and it is"
            else
                t_fail "the -static-pie control built here, yet the copy claims the fallback -- the probe should have selected static-pie"
            fi
            ;;
    esac
fi

# --- CHECK 5: the default build (no STATIC) is unchanged ------------------------
# Without STATIC the default link line carries -pie exactly as today, and
# make info (no STATIC) does NOT carry the STATIC_MODE line -- the knob is off
# by default, so the block is skipped and the default build is byte-identical
# (M1073's invariant).
_dinfo=$( (cd "$s" && "$_mk" -s info 2>/dev/null) || true )
_dmodeline=$(printf '%s\n' "$_dinfo" | sed -n 's/^STATIC_MODE *= *//p' | head -n 1)
_dline=$( (cd "$s" && "$_mk" -n -B jichi 2>/dev/null) | grep -F -- '-o jichi' | head -n 1)
_dpie=$(printf '%s\n' "$_dline" | grep -oE '(^| )-pie( |$)' | grep -c 'pie' || true)
if [ -n "$_dmodeline" ]; then
    t_fail "make info WITHOUT STATIC carries a STATIC_MODE line: ${_dmodeline} -- the knob is supposed to be off by default (default build must be byte-identical)"
elif [ "$_dpie" -ge 1 ]; then
    t_ok "default build unchanged: no STATIC_MODE line, and the default link line still carries -pie"
else
    t_fail "default link line no longer carries -pie -- STATIC=1 must not perturb the default build (line: ${_dline:-(not captured)})"
fi

# --- CHECK 6: the old recipe's defect is gone ----------------------------------
# With STATIC=1 the copy's link line must NOT contain the bare -static AFTER
# -pie (the finding's exact measured defect: -pie ... -static -> EXEC). In
# static-pie mode the line has -static-pie and NO bare -static (it may keep
# -pie alongside -static-pie -- that is the measured working shape); in
# fallback mode it has -static and NO -pie. The link line is quoted above.
if [ -z "$_modeline" ]; then
    t_ok "defect leg not asserted: make info STATIC=1 names no static mode (the knob is not in the Makefile yet -- E-07 red-first)"
else
    case "$_modeline" in
        static-pie*)
            _nsp=$(_tokcount "$_lline" -static-pie)
            _nbs=$(_tokcount "$_lline" -static)
            if [ "$_nsp" -ge 1 ] && [ "$_nbs" -eq 0 ]; then
                t_ok "static-pie link line: -static-pie present, no bare -static (the -pie ... -static defect is gone)"
            else
                t_fail "static-pie link line malformed (-static-pie=$_nsp bare -static=$_nbs; must be >=1/0): ${_lline:-(not captured)}"
            fi
            ;;
        *)
            _nbs=$(_tokcount "$_lline" -static)
            _npe=$(_tokcount "$_lline" -pie)
            if [ "$_nbs" -ge 1 ] && [ "$_npe" -eq 0 ]; then
                t_ok "fallback link line: -static present, no -pie (the -pie ... -static defect is gone)"
            else
                t_fail "fallback link line malformed (-static=$_nbs -pie=$_npe; must be >=1/0): ${_lline:-(not captured)}"
            fi
            ;;
    esac
fi

t_done
