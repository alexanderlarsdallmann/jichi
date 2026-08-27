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

t_plan 7

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

t_done
