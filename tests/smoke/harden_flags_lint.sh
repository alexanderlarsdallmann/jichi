#!/bin/sh
# smoke lint: what the build ASKED for is what the binary GOT (M472).
#
# THE DEFECT THIS EXISTS FOR. jichi set no security hardening flags at all, and the
# shipped binary had most of them anyway, because Ubuntu's gcc enables
# -fstack-protector-strong, PIE and full RELRO in its own specs. Measured on a
# toolchain WITHOUT those defaults -- the shape of a musl, bionic or hand-built gcc,
# which is four of the five libcs in docs/PLATFORMS.md:
#
#   HARDEN=0   canary=0 pie=0 relro=0 bindnow=0     <- nothing
#   HARDEN=1   canary=2 pie=1 relro=1 bindnow=1     <- all four
#
# So the previous state was: hardened on the developer's box, unknown everywhere
# else, and measured nowhere. This project probes the C dialect, vsnprintf,
# malloc_trim, clock_gettime and libcurl, and rebuilds every platform row from
# nothing so a result is reproducible rather than remembered. Security mitigations
# were the one axis with no instrument at all.
#
# WHY IT COMPARES ASK-VS-GOT RATHER THAN CHECKING A FIXED LIST. A fixed list would
# fail a legitimate row whose linker has no -z relro, which turns the lint into
# something to be weakened. Deriving the expectation from `make info` instead means:
#
#   * a platform that cannot provide a mitigation is not failed for it -- the probe
#     simply did not select the flag, and there is nothing to compare;
#   * a flag that WAS selected and silently did not take effect is caught. That is
#     the real failure mode, and it happened while writing this block: `comma` was
#     defined after the ':=' that used it, so every -Wl, probe expanded to a bare
#     "-Wl", failed, and was dropped. `make info` printing "-pie" alone was the
#     only symptom.
#
# The lint therefore FAILS only on a broken promise, never on a modest toolchain,
# and its output is the per-row posture statement docs/PLATFORMS.md wants.
. "$(dirname "$0")/_smoke.sh"

root=$(cd "$(dirname "$0")/../.." && pwd)

command -v readelf >/dev/null 2>&1 || t_skip "needs readelf (ELF toolchain)"
[ -f "$root/jichi" ] || t_skip "no built jichi to inspect (run make first)"
readelf -hW "$root/jichi" >/dev/null 2>&1 || t_skip "jichi is not an ELF object here"

# THE MAKE THIS PROJECT NEEDS, not whatever is called `make` (M479). OpenBSD's
# /bin/make is BSD make and cannot parse this GNU Makefile -- which is why the row
# runs `gmake` deliberately, and why this driver, the ONLY one in the tier that
# EXECUTES the project's make rather than reading the Makefile as a file, silently
# stopped working there.
#
# Measured: on OpenBSD `make info` failed, `asked` came back empty, every `want`
# below was false, and all six checks passed VACUOUSLY -- including check 5, the
# floor written specifically to stop this lint passing vacuously, because it is
# gated on `want` and an empty `asked` disables it too. An anti-vacuity guard with
# the same vacuity hole as the thing it guards.
_mk=""
for _c in "${MAKE:-}" make gmake gnumake; do
    [ -n "$_c" ] || continue
    command -v "$_c" >/dev/null 2>&1 || continue
    if "$_c" --version 2>/dev/null | grep -q 'GNU Make'; then _mk="$_c"; break; fi
done
[ -n "$_mk" ] || t_skip "no GNU make found (tried \$MAKE, make, gmake, gnumake)"

t_plan 15

_info=$( (cd "$root" && "$_mk" info 2>/dev/null) || true)

# CHECK 1 IS THE FLOOR FOR THE WHOLE DRIVER, and it is the check OpenBSD needed.
# Everything below reads `asked`; if we could not ask, every one of them reports a
# cheerful "not available on this toolchain" and the driver is worthless. So the
# question "could we ask at all?" is asserted before anything is concluded from
# the answer.
if printf '%s\n' "$_info" | grep -q '^HARDEN  *='; then
    t_ok "$_mk info is readable and reports the hardening posture"
else
    t_fail "could not read '$_mk info' -- every check below would report \
'not available on this toolchain' and pass while measuring nothing (M479: OpenBSD's \
make is BSD make; the row uses gmake)"
fi

# What the build asked for. Read from `make info` so this cannot drift from the
# Makefile -- and so a HARDEN=0 build reports honestly instead of failing.
# sed -E with (a|b): GNU's \| alternation is read as a LITERAL pipe by BSD sed, so
# the pattern would match nothing and `asked` would be empty -- which makes every
# `want` below false and the whole lint pass vacuously on exactly the platforms it
# exists for. posix_utils_lint check 11 caught this here; it is the fifth member of
# that family after grep -P, \|, \b and \xNN (M461/M466/M471), typed by someone who
# had just read those rows.
asked=$(printf '%s\n' "$_info" | \
         sed -nE 's/^HARDEN(FLAGS|LDFLAGS)  *= *//p' | tr '\n' ' ')
printf '# asked for:%s\n' "$asked"

want() { case " $asked " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }

# --- THE DETECTOR SELF-TEST, RUN FIRST AND USED AS A PRECONDITION (M479) -------
#
# Every check below reads the ELF for evidence of a mitigation. That evidence is
# NOT portable, and the OpenBSD row is how we know: with the flags correctly
# selected there, `readelf --dyn-syms | grep __stack_chk_fail` finds nothing --
# because that symbol name is glibc's, not a universal fact about stack
# protectors. The lint reported "stack protector selected but no
# __stack_chk_fail -- the flag is not doing what its presence implies", which was
# FALSE: the flag was fine and the detector was wrong.
#
# A check cannot distinguish "the flag is inert" from "my detector does not work
# here" -- unless it first asks whether the detector discriminates on THIS
# toolchain. So that question is asked first, per mitigation, by building the same
# trivial program twice with the platform's defaults countered:
#
#   off = defaults countered, no flags     -> the mitigation must be ABSENT
#   on  = the same, plus our flags         -> the mitigation must be PRESENT
#
# A detector that goes 0 -> 1 discriminates, and its verdict on the real binary
# means something. One that does not (canary and PIE on OpenBSD; anything at all
# on a distro whose compiler enables the mitigation regardless) cannot support a
# conclusion in either direction, and the check says so instead of failing.
#
# This also subsumes what the old floor did: on Ubuntu, where the distro supplies
# canary/PIE/RELRO whatever we ask, `off` is non-zero, the detector does not
# discriminate, and the corresponding check reports "cannot attribute" rather than
# a green that the distro earned.
_hf=$(mktemp -d "${TMPDIR:-/tmp}/jichi_harden.XXXXXX")
printf 'int main(void){char b[64]; b[0]=0; return b[0];}\n' > "$_hf/p.c"
_counter="-fno-stack-protector -no-pie -Wl,-z,norelro"
${CC:-cc} $_counter -o "$_hf/off" "$_hf/p.c" 2>/dev/null || true
${CC:-cc} $_counter -fstack-protector-strong -fPIE -Wl,-z,relro -Wl,-z,now -pie \
    -o "$_hf/on" "$_hf/p.c" 2>/dev/null || true

# 1 iff the detector named by $1 goes absent -> present between off and on.
_disc() {  # $1 = canary|pie|relro
    _o=0; _n=0
    case "$1" in
    canary) _o=$(readelf -sW --dyn-syms "$_hf/off" 2>/dev/null | grep -c '__stack_chk_fail')
            _n=$(readelf -sW --dyn-syms "$_hf/on"  2>/dev/null | grep -c '__stack_chk_fail') ;;
    pie)    _o=$(readelf -hW "$_hf/off" 2>/dev/null | grep -c 'Type:.*DYN')
            _n=$(readelf -hW "$_hf/on"  2>/dev/null | grep -c 'Type:.*DYN') ;;
    relro)  _o=$(readelf -lW "$_hf/off" 2>/dev/null | grep -c 'GNU_RELRO')
            _n=$(readelf -lW "$_hf/on"  2>/dev/null | grep -c 'GNU_RELRO') ;;
    esac
    [ "$_o" = "0" ] && [ "$_n" != "0" ]
}

_built=yes
{ [ -s "$_hf/off" ] && [ -s "$_hf/on" ]; } || _built=no

if [ "$_built" = "no" ]; then
    t_ok "detector self-test skipped: this toolchain will not build the \
counter-default probe ($_counter)"
    _any=no
else
    _d=""
    for _m in canary pie relro; do
        if _disc "$_m"; then _d="$_d $_m"; fi
    done
    if [ -n "$_d" ]; then
        t_ok "detectors that discriminate on this toolchain:$_d"
        _any=yes
    else
        t_fail "no detector discriminates: adding the flags changes nothing this \
lint can see, so every check below would be decoration. Either the flags are inert \
here or all three detectors are wrong for this platform -- both need a human."
        _any=no
    fi
fi

# `want` AND the detector must both hold before a verdict is claimed. The three
# outcomes are deliberately distinct: not selected / selected-and-verified /
# selected-but-unverifiable. Only the middle one can fail.
_check() {  # $1 = label, $2 = flag, $3 = detector, $4 = readelf test result (0/1)
    if ! want "$2"; then
        t_ok "$2 not selected on this toolchain (not asserted)"
    elif ! _disc "$3"; then
        # The example is per-detector: a shared one named the canary case for the
        # PIE check too, which is exactly the kind of small wrongness that makes a
        # reader distrust the rest of the line.
        case "$3" in
        canary) _why="OpenBSD names its canary handler something other than \
__stack_chk_fail" ;;
        pie)    _why="the OpenBSD probe yields no 'Type: DYN' even with -fPIE -pie; \
observed, not explained" ;;
        *)      _why="this platform's ELF does not carry the evidence this check \
reads" ;;
        esac
        t_ok "$2 selected; cannot verify -- the '$3' detector does not \
discriminate on this toolchain, so its absence proves nothing (M479: $_why)"
    elif [ "$4" = "1" ]; then
        t_ok "asked for $2 and the binary has it ($3)"
    else
        t_fail "$2 was selected, the '$3' detector works here, and the binary \
lacks it -- a broken promise, not a portability question"
    fi
}

_bin="$root/jichi"
_check "relro"  "-Wl,-z,relro" relro \
    "$(readelf -lW "$_bin" 2>/dev/null | grep -q 'GNU_RELRO' && echo 1 || echo 0)"
_check "bindnow" "-Wl,-z,now" relro \
    "$(readelf -dW "$_bin" 2>/dev/null | grep -q 'BIND_NOW' && echo 1 || echo 0)"
_check "canary" "-fstack-protector-strong" canary \
    "$(readelf -sW --dyn-syms "$_bin" 2>/dev/null | grep -q '__stack_chk_fail' && echo 1 || echo 0)"
_check "pie"    "-pie" pie \
    "$(readelf -hW "$_bin" 2>/dev/null | grep -q 'Type:.*DYN' && echo 1 || echo 0)"

rm -rf "$_hf"

# Non-executable stack. Unconditional and detector-free: it needs no flag on any
# toolchain jichi supports, so an executable stack means something actively went
# wrong (an assembly object without .note.GNU-stack, a linker script) rather than
# a missing capability.
if readelf -lW "$_bin" 2>/dev/null | grep 'GNU_STACK' | grep -q 'RWE'; then
    t_fail "the stack is EXECUTABLE (GNU_STACK RWE) -- NX is off"
else
    t_ok "the stack is non-executable"
fi

# --- the JC_PRINTF floor (E-04): the attribute must STAY on the formatters ---
#
# The finding: with the format attribute in place, the ONE product UB the
# compiler caught (%ld for an int in jc_relay.c) and the two test calls that
# pass a non-literal format were the whole defect surface. If a future edit
# drops the attribute from a formatter, -Wformat goes blind to it again --
# silently. So pin it: jc_platform.h must define JC_PRINTF, and the four
# variadic formatters that must carry it -- jc_snprintf (jc_snprintf.h),
# jc_sb_append_fmt (jc_str.h), jc_logf (jc_log.h), jc_ir_warn (jc_convert.h) --
# must each have a declaration line that names the function AND contains
# JC_PRINTF. (jc_vsnprintf is a va_list overload and cannot carry
# format(printf,...), so it is not in the floor: the checker is jc_snprintf.)
# The count of annotated formatter declarations must be >= 4.
# Bare grep, as the rest of this driver uses it: M479's whole point is that a
# tool may live at a path other than /usr/bin, and a hard-coded $G would make
# this one check vacuous or wrong on exactly those rows.
_pdef=$(grep -c '#define JC_PRINTF' "$root/include/jc_platform.h")
_jc_jc_snprintf=$(grep -c 'jc_snprintf.*JC_PRINTF' "$root/include/jc_snprintf.h")
_jc_jc_sb_append_fmt=$(grep -c 'jc_sb_append_fmt.*JC_PRINTF' "$root/include/jc_str.h")
_jc_jc_logf=$(grep -c 'jc_logf.*JC_PRINTF' "$root/include/jc_log.h")
_jc_jc_ir_warn=$(grep -c 'jc_ir_warn.*JC_PRINTF' "$root/include/jc_convert.h")
_jc=$((_jc_jc_snprintf + _jc_jc_sb_append_fmt + _jc_jc_logf + _jc_jc_ir_warn))
printf '# JC_PRINTF defined in jc_platform.h:%s annotated formatters:%s\n' "$_pdef" "$_jc"
if [ "$_pdef" -ge 1 ] && [ "$_jc" -ge 4 ]; then
    t_ok "JC_PRINTF is defined and carried by all four variadic formatters"
else
    _miss=""
    [ "$_pdef" -ge 1 ] || _miss="jc_platform.h has no #define JC_PRINTF; "
    [ "$_jc_jc_snprintf" -ge 1 ] || _miss="${_miss}jc_snprintf.h (jc_snprintf); "
    [ "$_jc_jc_sb_append_fmt" -ge 1 ] || _miss="${_miss}jc_str.h (jc_sb_append_fmt); "
    [ "$_jc_jc_logf" -ge 1 ] || _miss="${_miss}jc_log.h (jc_logf); "
    [ "$_jc_jc_ir_warn" -ge 1 ] || _miss="${_miss}jc_convert.h (jc_ir_warn)"
    t_fail "JC_PRINTF floor not met (need the #define plus >= 4 annotated \
formatters, got $_jc) -- not annotated: ${_miss%;}"
fi

# --- CHECK 9 (E-05): LTO=1 ALONE must not claim _FORTIFY_SOURCE is live --------
#
# The defect: the old `make info` OPT line said "carries -Os so _FORTIFY_SOURCE
# is live" whenever SIZEFLAGS was non-empty. But LTO=1 alone sets SIZEFLAGS to
# -flto (no -Os, no -O at all), so _FORTIFY_SOURCE is inert at -O0, yet the line
# claimed it was live -- a diagnostic contradicting the build (M693's shape, found
# again for LTO). The fix keys the claim on an actual optimisation LEVEL
# (OPTLEVEL = -O1/-O2/-O3/-Os/-Oz in OPT or SIZEFLAGS), so LTO=1 alone reports
# "inert". This check reads `make info LTO=1` and asserts the claim. It is
# toolchain-independent (it parses make's own output), so it fails on every row.
_opt_lto=$( (cd "$root" && "$_mk" -s info LTO=1 2>/dev/null) || true)
_lto_live=$(printf '%s\n' "$_opt_lto" | grep '^OPT' | grep -c 'is live')
_lto_inert=$(printf '%s\n' "$_opt_lto" | grep '^OPT' | grep -cE 'inert|not set')
if [ "$_lto_live" -ge 1 ]; then
    t_fail "make info LTO=1 claims _FORTIFY_SOURCE is live, but LTO=1 alone sets \
no optimisation level (SIZEFLAGS = -flto, -O0): fortify is inert there. The \
LTO row must say inert (E-05)"
elif [ "$_lto_inert" -ge 1 ]; then
    t_ok "make info LTO=1 does not claim _FORTIFY_SOURCE is live (inert: no \
optimisation level in play)"
else
    t_fail "make info LTO=1 says neither live nor inert on the OPT line -- the \
line changed shape and this check can no longer read it"
fi

# --- CHECK 10 (E-05): when make info says fortify IS live, the binary must      #
#     import a __*_chk symbol, with the same detector self-test as checks 2-6.   #
#
# The finding's caution, applied: HARDENING.md 7b says "no __*_chk symbol appears
# because sprintf/strcpy are banned" -- but memcpy/read/fread/snprintf ALL have
# _chk variants, so a fortified build DOES import them. (sprintf/strcpy are not
# the only fortifiable calls.)
#
# THIS DRIVER INSPECTS THE DEFAULT (inert) jichi, which has no __*_chk -- so the
# "real binary" leg here is the driver's OWN small OPT=-O2 artefact, built the way
# an optimised row builds jichi, not the default binary. The self-test first
# proves the toolchain's _chk detector discriminates (a trivial program built
# counter-default -> no __*_chk; plus the live flags -> at least one __*_chk),
# exactly as canary/pie/relro do above: a detector that goes 0 -> >=1 means the
# real-binary leg's count is meaningful. Without it the check would pass
# vacuously on a distro that fortifies regardless (off already > 0), or fail
# on one whose fortify imports nothing this detector can see.
#
# The probe program uses snprintf, which has __snprintf_chk at EVERY fortify
# level, so "off -> on" is a clean 0 -> >=1 discriminator (a plain memcpy can be
# optimised away at -O2 when its size is a compile-time constant).
_cc=""
for _c in "${CC:-}" cc gcc clang; do
    [ -n "$_c" ] || continue
    command -v "$_c" >/dev/null 2>&1 || continue
    _cc="$_c"; break
done
[ -n "$_cc" ] || { t_skip_one "no C compiler on PATH; cannot run the fortify self-test"; _chk_disc=no; }

_chkdisc() { # count UNIQUE undefined dynamic __*_chk imports in $1 (the fortify
    # symbols). Full names only, so the canary __stack_chk_fail is NOT counted,
    # and the @GLIBC version suffix is stripped. -W --dyn-syms prints the .dynsym
    # table once (no .symtab duplication), so this is a count, not a presence.
    readelf -W --dyn-syms "$1" 2>/dev/null | \
        awk '{ n = $8; sub(/@.*/, "", n); if (n ~ /^__.*_chk$/) k[n] = 1 }
             END { x = 0; for (s in k) x++; print x }'
}

if [ -n "$_cc" ]; then
    _fd=$(mktemp -d "${TMPDIR:-/tmp}/jichi_fortify.XXXXXX") || _fd=""
    if [ -z "$_fd" ]; then
        t_skip_one "mktemp failed; cannot run the fortify self-test"
        _chk_disc=no
    else
        printf '#include <stdio.h>\nint main(void){char b[16];int n=snprintf(b,sizeof b,"%%d",42);return b[0]+n;}\n' > "$_fd/p.c"
        # counter-default = -O2 with the distro's own _FORTIFY_SOURCE removed, so
        # the control is genuinely un-fortified even on an Ubuntu gcc that
        # defaults to _FORTIFY_SOURCE 3 at -O.
        "$_cc" -O2 -U_FORTIFY_SOURCE -o "$_fd/off" "$_fd/p.c" 2>/dev/null || true
        _hf2=$( (cd "$root" && "$_mk" -s info OPT=-O2 2>/dev/null) | sed -n 's/^HARDENFLAGS  *= *//p')
        "$_cc" -std=c89 -pedantic -D_POSIX_C_SOURCE=200112L -O2 $_hf2 -o "$_fd/on" "$_fd/p.c" 2>/dev/null || true
        _fo=0; _fn=0
        [ -s "$_fd/off" ] && _fo=$(_chkdisc "$_fd/off")
        [ -s "$_fd/on" ]  && _fn=$(_chkdisc "$_fd/on")
        printf '# fortify self-test: __*_chk off=%s on=%s\n' "$_fo" "$_fn"
        _chk_disc=no
        { [ -s "$_fd/off" ] && [ -s "$_fd/on" ]; } && _chk_built=yes || _chk_built=no
        if [ "$_chk_built" = "yes" ] && [ "$_fo" -eq 0 ] && [ "$_fn" -ge 1 ]; then
            _chk_disc=yes
        fi
        # The level make info claims is live under OPT=-O2 (empty if inert/none).
        _live2=$( (cd "$root" && "$_mk" -s info OPT=-O2 2>/dev/null) | sed -n 's/.*-D_FORTIFY_SOURCE=\([0-9][0-9]*\).*/\1/p' | head -n 1)
        if [ -z "$_live2" ]; then
            t_skip_one "no live _FORTIFY_SOURCE under OPT=-O2 on this toolchain \
(the probe rejected levels 3 and 2), so there is nothing to verify"
        elif [ "$_chk_disc" = "no" ]; then
            t_ok "OPT=-O2 reports _FORTIFY_SOURCE $_live2 live, but the __*_chk \
detector does not discriminate on this toolchain (off=${_fo} on=${_fn}); cannot \
attribute, so the count is not asserted (E-05)"
        elif [ -s "$_fd/on" ] && [ "$_fn" -ge 1 ]; then
            t_ok "OPT=-O2 reports _FORTIFY_SOURCE $_live2 live, and the \
optimised artefact imports $_fn __*_chk symbol(s) (self-test off=${_fo} -> \
on=${_fn})"
        else
            t_fail "OPT=-O2 reports _FORTIFY_SOURCE $_live2 live and the \
__*_chk detector discriminates here, but the optimised artefact imports NO \
__*_chk symbol -- the flag is not doing what its presence implies (E-05)"
        fi
        rm -rf "$_fd"
    fi
fi

# --- CHECK 11 (E-06, M1074): the COMMAND-LINE form -- the user's CFLAGS must be
#     honored ON the compile line, with the project's own flags intact. -------
#
# GNU convention (E-06): CFLAGS/CPPFLAGS/LDFLAGS are the USER's variables, empty
# by default; the project's compile line lives in JC_CFLAGS, and the user's
# CFLAGS is appended AFTER it on the build line. So a command-line CFLAGS value
# must appear on the compile line together with the project's -Iinclude. Red
# today, before the rename: the command-line CFLAGS overrides the Makefile's
# `CFLAGS = <project>` assignment, so the compile line is just the user's flag
# and -Iinclude is absent (the project's dialect/-I/-D all vanish -> the build
# would fail). Both markers are asserted: the user flag is honored AND the
# project flags survive.
_e06_cmd=$( (cd "$root" && "$_mk" -n -B CFLAGS=-DJC_E06_MARK src/util/jc_str.o 2>/dev/null) | \
    grep 'src/util/jc_str.c' | head -n 1)
if printf '%s\n' "$_e06_cmd" | grep -q -- '-DJC_E06_MARK' && \
   printf '%s\n' "$_e06_cmd" | grep -q -- '-Iinclude'; then
    t_ok "command-line CFLAGS=-DJC_E06_MARK appears on the compile line WITH the \
project's -Iinclude intact (E-06, M1074)"
else
    t_fail "command-line CFLAGS form (E-06, M1074): the compile line for \
src/util/jc_str.c does not carry BOTH -DJC_E06_MARK and -Iinclude -- the user's \
CFLAGS must be honored with the project's flags intact: $_e06_cmd"
fi

# --- CHECK 12 (E-06, M1074): the ENVIRONMENT form -- the user's CFLAGS must be
#     honored from the environment too, with the project's flags intact. ------
#
# The same contract as check 11, but the CFLAGS value comes from the environment
# rather than the command line. Red today, before the rename: the Makefile's
# `CFLAGS = <project>` assignment overrides the environment, so -DJC_E06_ENV is
# silently dropped from the compile line (the distro/dpkg-buildflags case from
# the finding). After the rename the user's CFLAGS is empty by default in the
# Makefile, so the environment value survives and is appended on the build line.
_e06_env=$( (cd "$root" && CFLAGS=-DJC_E06_ENV "$_mk" -n -B src/util/jc_str.o 2>/dev/null) | \
    grep 'src/util/jc_str.c' | head -n 1)
if printf '%s\n' "$_e06_env" | grep -q -- '-DJC_E06_ENV' && \
   printf '%s\n' "$_e06_env" | grep -q -- '-Iinclude'; then
    t_ok "env CFLAGS=-DJC_E06_ENV appears on the compile line WITH the project's \
-Iinclude intact (E-06, M1074)"
else
    t_fail "env CFLAGS form (E-06, M1074): the compile line for src/util/jc_str.c \
does not carry BOTH -DJC_E06_ENV and -Iinclude -- the user's CFLAGS must be \
honored from the environment with the project's flags intact: $_e06_env"
fi

# --- CHECK 13 (E-06, M1074-b, audit F1): pin the USER-CFLAGS POSITION on the
#     compile line -- the user marker must come AFTER the project's flags. ----
#
# Checks 11/12 assert the user marker is PRESENT, but nothing pins that it comes
# AFTER the project's flags. A flipped rule `$(CC) $(CFLAGS) $(JC_CFLAGS)` would
# stay green at 11/12 (marker present, -Iinclude present) and restore the
# "project flags win" defect: for -O/-D collisions the FIRST flag on the line is
# the one that takes effect, so user flags placed before the project's would be
# overridden by them. Anchor on -fPIE, the last probe-selected flag in
# JC_CFLAGS' HARDENFLAGS: the user marker must sit to its RIGHT.
#
# Reuses check 11's exact recipe line ($_e06_cmd). Two distinct failure modes,
# neither of which may pass vacuously:
#   -fPIE absent  -> t_fail. We CANNOT anchor a position we cannot see (M479:
#                    a check that passes vacuously when it cannot measure is a
#                    defect, not a skip).
#   marker absent -> t_fail. Check 11 already covers presence; here absence is
#                    a separate, distinct failure, not a "not selected" honest
#                    non-assertion (there IS no probe for -DJC_E06_MARK: it is
#                    the test's own input, so its absence means the build dropped
#                    the user's CFLAGS entirely).
_e06_fpie=$(printf '%s\n' "$_e06_cmd" | grep -c -- '-fPIE')
_e06_mark=$(printf '%s\n' "$_e06_cmd" | grep -c -- '-DJC_E06_MARK')
_e06_pos=$(printf '%s\n' "$_e06_cmd" | \
    awk '{ i = index($0,"-fPIE"); j = index($0,"-DJC_E06_MARK"); \
           if (i > 0 && j > i) print "after"; else print "before-or-absent" }')
if [ "$_e06_mark" -eq 0 ]; then
    t_fail "user-CFLAGS position (E-06, M1074-b, F1): the compile line carries \
NO -DJC_E06_MARK at all -- the user's CFLAGS was dropped, so there is nothing \
whose position to pin: $_e06_cmd"
elif [ "$_e06_fpie" -eq 0 ]; then
    t_fail "user-CFLAGS position (E-06, M1074-b, F1): -fPIE is absent from the \
compile line, so this check cannot anchor the user flag against the project's \
HARDENFLAGS -- it refuses to pass on a line it cannot measure (M479): \
$_e06_cmd"
elif [ "$_e06_pos" = "after" ]; then
    t_ok "user CFLAGS -DJC_E06_MARK sits AFTER -fPIE (the project's last \
HARDENFLAGS) on the compile line, so user flags do not get overridden by the \
project's (E-06, M1074-b, F1)"
else
    t_fail "user-CFLAGS position (E-06, M1074-b, F1): -DJC_E06_MARK is \
$_e06_pos -fPIE on the compile line -- the rule must be \
\$(JC_CFLAGS) \$(CPPFLAGS) \$(CFLAGS), not a flipped order that lets the \
project's flags win -O/-D collisions: $_e06_cmd"
fi

# --- CHECK 14 (E-06, M1074-b, audit F2): pin the USER-LDFLAGS POSITION on the
#     link line -- the user marker must come AFTER the project's link flags. --
#
# Same shape as check 13, for the link line. The recorded consequence of the
# order: a rig's command-line LDFLAGS=-static (scripts/tier-v-arch.sh) must land
# AFTER -pie, HARDENLDFLAGS' last probe-selected flag -- a user -static placed
# before the project's -pie would not compose into a PIE static link. Anchor on
# -pie. If the link line did not resolve, the extraction broke (target or
# recipe shape changed) -> t_fail. If -pie is NOT selected on this row, the
# probe honestly rejected it, so there is nothing to compare -> t_ok, not a
# skip (ask-vs-got, the same discipline as checks 2-6).
_e06_link=$( (cd "$root" && LDFLAGS=-DJC_E06_LINKMARK "$_mk" -n -B jichi 2>/dev/null) | \
    grep -E '(^|[ /])jichi$' | head -n 1)
_e06_pie=$(printf '%s\n' "$_e06_link" | grep -c -- '-pie')
_e06_linkpos=$(printf '%s\n' "$_e06_link" | \
    awk '{ i = index($0,"-pie"); j = index($0,"-DJC_E06_LINKMARK"); \
           if (i > 0 && j > i) print "after"; else print "before-or-absent" }')
if [ -z "$_e06_link" ]; then
    t_fail "user-LDFLAGS position (E-06, M1074-b, F2): the jichi link recipe \
did not resolve to a command line -- the target or its recipe shape changed and \
this check cannot measure it"
elif [ "$_e06_pie" -eq 0 ]; then
    t_ok "user-LDFLAGS position (E-06, M1074-b, F2): -pie is not selected on \
this row, so the probe answered ask-vs-got and the LDFLAGS order is not asserted"
elif [ "$_e06_linkpos" = "after" ]; then
    t_ok "user LDFLAGS -DJC_E06_LINKMARK sits AFTER -pie (the project's last \
HARDENLDFLAGS) on the link line, so a rig's -static composes into the PIE link \
(E-06, M1074-b, F2)"
else
    t_fail "user-LDFLAGS position (E-06, M1074-b, F2): -DJC_E06_LINKMARK is \
$_e06_linkpos -pie on the link line -- the rule must be \
\$(JC_LDFLAGS) \$(LDFLAGS), not a flipped order: $_e06_link"
fi

# --- CHECK 15 (E-06, M1074-b, audit F3): pin the D1 EXCLUSION -- user flags
#     must NOT leak into fallbacks-compile. ----------------------------------
#
# D1 (the E-06 design note): fallbacks-compile answers "does the PROJECT's
# fallback path compile under the PROJECT's dialect?", a different question from
# "does the user's CFLAGS break X?" -- so the user's CFLAGS/CPPFLAGS are
# deliberately excluded from that target (only $(JC_CFLAGS) is passed). That
# exclusion was pinned by prose only. This check pins it two ways:
#   floor: count the cc -c invocations the target expands to. If it is BELOW the
#          floor, the extraction broke or the target's shape changed -- a low
#          count is an extraction break, not "no leak", so t_fail (M479: do not
#          pass on a measurement that cannot be made). The floor is MEASURED, not
#          assumed: today's un-anchored count of ' -c .* -o /dev/null' is 8
#          (the standalone -DJC_NO_{CLOCK_GETTIME,MMAP,NANOSLEEP,NOFORK} recipes
#          plus the select / no-sockets / HAVE-macro / nofork loops, whose lines
#          end in '|| exit 1; \' or '; done' and so a trailing-$ anchor would
#          miss -- a lower anchor is the extraction break, not a real reduction).
#   leak:  with CFLAGS/CPPFLAGS set, no cc -c line may carry a user marker.
_fc_count=$( (cd "$root" && "$_mk" -n fallbacks-compile 2>/dev/null) | \
    grep -c ' -c .* -o /dev/null')
_fc_leak=$( (cd "$root" && CFLAGS=-DJC_E06_ENV CPPFLAGS=-DJC_E06_CPP \
    "$_mk" -n fallbacks-compile 2>/dev/null) | grep -c 'JC_E06')
if [ "$_fc_count" -lt 8 ]; then
    t_fail "fallbacks-compile exclusion (E-06, M1074-b, F3): only $_fc_count cc \
-c invocations (floor 8) -- the extraction broke or the target's recipe shape \
changed, and this check refuses to assert a leak it cannot measure (M479)"
elif [ "$_fc_leak" -ge 1 ]; then
    t_fail "fallbacks-compile exclusion (E-06, M1074-b, F3): user CFLAGS/\
CPPFLAGS LEAK into fallbacks-compile ($_fc_leak recipe line(s) carry a JC_E06 \
marker) -- D1 requires that target to compile under the PROJECT's dialect only, \
not the user's"
else
    t_ok "fallbacks-compile expands its $_fc_count cc -c recipes under the \
project's JC_CFLAGS alone -- user CFLAGS/CPPFLAGS do not leak in (D1, \
E-06, M1074-b, F3)"
fi

t_done
