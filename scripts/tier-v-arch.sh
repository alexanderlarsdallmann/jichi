#!/bin/sh
# tier-v-arch.sh -- the architecture sweep: cross-build with `zig cc`, run the
# unit suite under qemu-user binfmt, one comparable row per target (M469).
#
# WHY THIS EXISTS. jichi's portability claims rested on FOUR architectures --
# x86-64, aarch64, armhf and s390x -- and exactly one of those (s390x) is
# big-endian. Meanwhile this workstation already has ~31 `qemu-user` binfmt
# handlers registered and a `zig cc` that ships musl for 33 linux triples, so a
# dozen untested architectures were reachable at zero install cost and nobody
# had looked. The campaign's own findings argue for looking: M266 retired a
# standing assumption about the index cache's endianness tag, and M460/M461
# found three non-POSIX symbols that four Linux libcs had made look portable.
#
# WHAT A ROW HERE IS, AND IS NOT. It is a CROSS-BUILD RUN UNDER EMULATION, and
# every row says so -- the same distinction docs/PLATFORMS.md already draws for
# s390x. It is not a machine. Two further limits, stated rather than implied:
#
#   * `HAVE_CURL=` -- zig cc bundles libcs, not dependency trees, so these rows
#     link WITHOUT libcurl. They exercise the core, the arenas, the JSON, the
#     pure helpers and the unit suite; they do NOT exercise a model call. A green
#     row here is weaker evidence than a green FreeBSD row.
#   * qemu-user emulates the ISA, not the machine. It will not find a cache
#     coherency bug, a timing bug, or anything about real hardware.
#
# What it IS good for is the defect class C89 portability actually fails on:
# `long` assumed 64-bit, byte order, unaligned struct access, and `%lu`-with-casts
# where long is four bytes. m68k is the sharpest instrument in the set -- big
# endian AND 32-bit AND only 2-byte aligned -- which is why --all runs it first.
#
# Usage:
#   scripts/tier-v-arch.sh --list                  # the candidate matrix, measured
#   scripts/tier-v-arch.sh --arch m68k-linux-musl  # one row
#   scripts/tier-v-arch.sh --all                   # every runnable target
#   scripts/tier-v-arch.sh --dry-run               # print the plan, touch nothing
#   scripts/tier-v-arch.sh --arch X --keep         # keep the build tree
#
# Exit codes, matching the other tier-V rigs:
#   0 every row ran (read the RESULT lines for the verdict)
#   1 a row ran and something under test failed
#   2 the rig could not start (no zig, no binfmt, ...)
#   3 a target BUILT but never executed -- a RIG result (missing handler,
#     qemu too old for the ISA), deliberately not counted as a jichi failure
set -u

DIR="${TIER_V_ARCH_DIR:-$HOME/.cache/jichi-tier-v-arch}"
REPO=$(cd "$(dirname "$0")/.." && pwd)
# Results go in $DIR, never the repo: a rig that can dirty the tree it tests has
# already been three separate mistakes in this campaign.
RESULTS="$DIR/results-arch.txt"
DRY=0; KEEP=0; ONE=""; ALL=0; LIST=0

# The one implementation of "put the tree somewhere to build it" (M466).
. "$(dirname "$0")/_rig_ship.sh"

while [ $# -gt 0 ]; do
    case "$1" in
        --list)    LIST=1 ;;
        --all)     ALL=1 ;;
        --arch)    ONE="$2"; shift ;;
        --dry-run) DRY=1 ;;
        --keep)    KEEP=1 ;;
        --dirty)   TIER_V_ARCH_DIRTY=1 ;;
        # Print the header block rather than a hardcoded line range: a
        # `sed -n '2,40p'` silently loses its last lines the first time an option
        # is added above, which is how tier-v-vm.sh's help lost a row (M430).
        --help|-h) awk 'NR>1 && !/^#/{exit} NR>1' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "tier-v-arch: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

command -v zig >/dev/null 2>&1 || {
    echo "tier-v-arch: zig not found -- this rig cross-builds with 'zig cc'" >&2; exit 2; }
[ -d /proc/sys/fs/binfmt_misc ] || {
    echo "tier-v-arch: /proc/sys/fs/binfmt_misc absent -- no emulation handlers" >&2; exit 2; }

# ---------------------------------------------------------------- the matrix
# Measured, not hardcoded: intersect the triples THIS zig can target with the
# handlers THIS kernel has registered. A hardcoded list would rot the first time
# either side changed, and would then silently test fewer arches than it claims.
zig_musl_triples() {
    zig targets 2>/dev/null \
      | grep -oE '"[a-z0-9_]+-linux-musl[a-z0-9]*"' \
      | tr -d '"' | sort -u
}

# arch -> qemu-user handler basename. NATIVE means it needs no emulation.
handler_for() {
    case "${1%%-*}" in
        x86_64)      echo NATIVE ;;
        x86|i386)    echo NATIVE ;;
        aarch64)     echo aarch64 ;;
        aarch64_be)  echo aarch64_be ;;
        arm|thumb)   echo arm ;;
        armeb|thumbeb) echo armeb ;;
        m68k)        echo m68k ;;
        mips)        echo mips ;;
        mipsel)      echo mipsel ;;
        mips64)      echo mips64 ;;
        mips64el)    echo mips64el ;;
        powerpc)     echo ppc ;;
        powerpc64)   echo ppc64 ;;
        powerpc64le) echo ppc64le ;;
        riscv32)     echo riscv32 ;;
        riscv64)     echo riscv64 ;;
        s390x)       echo s390x ;;
        loongarch64) echo loongarch64 ;;
        hexagon)     echo hexagon ;;
        sparc)       echo sparc ;;
        sparc64)     echo sparc64 ;;
        *)           echo "" ;;
    esac
}

# n32/o32-style ABIs need the ABI-specific handler, not the arch's default.
handler_for_triple() {
    case "$1" in
        mips64-*abin32)   echo mipsn32 ;;
        mips64el-*abin32) echo mipsn32el ;;
        *)                handler_for "$1" ;;
    esac
}

is_big_endian() {
    case "${1%%-*}" in
        aarch64_be|armeb|thumbeb|m68k|mips|mips64|powerpc|powerpc64|s390x|sparc|sparc64) return 0 ;;
    esac
    return 1
}
is_32bit() {
    case "${1%%-*}" in
        arm|armeb|thumb|thumbeb|m68k|mips|mipsel|powerpc|riscv32|x86|i386|sparc) return 0 ;;
    esac
    case "$1" in *abin32) return 0 ;; esac
    return 1
}

# One representative per arch/ABI: thumb* are encodings of arm*, and the soft/hard
# float and loongarch f32/sf variants differ in FPU ABI, not in the properties this
# rig is looking for. Dropping them is a CHOICE, printed by --list so it is visible
# rather than silently narrowing the sweep.
representative() {
    case "$1" in
        thumb-*|thumbeb-*)        return 1 ;;
        *musleabi)                                    # prefer the hardfloat sibling
            t="${1%musleabi}musleabihf"
            zig_musl_triples | grep -qx "$t" && return 1 ;;
        loongarch64-linux-muslf32|loongarch64-linux-muslsf) return 1 ;;
    esac
    return 0
}

# CAN zig actually generate code for it? Measured, because the libc table and the
# LLVM backend list are DIFFERENT SETS. `zig targets` advertises m68k-linux-musl --
# it ships musl for m68k -- and the bundled LLVM has no m68k backend at all:
#
#   error: unable to create target: 'No available targets are compatible with
#          triple "m68k-unknown-linux5.10.0-musl"'
#
# Without this probe the matrix claims 22 architectures and silently tests fewer.
# Worse, the failure is not loud where it matters: run `make` at such a target and
# its configure probes ALSO fail to compile, and the Makefile reads every failure
# as "this platform lacks the feature" -- so the m68k attempt selected `-std=gnu89`
# and `-DJC_NO_CLOCK_GETTIME` before dying, exactly the shape of M449 (uClibc
# hiding malloc_trim behind __USE_GNU) and M458 (Guix shipping no `cc`). A probe
# must distinguish "absent" from "could not ask".
can_target() {
    printf 'int main(void){return 0;}\n' \
      | zig cc -target "$1" -x c - -o /dev/null >/dev/null 2>&1
}

candidates() {
    zig_musl_triples | while read -r t; do
        representative "$t" || continue
        h=$(handler_for_triple "$t")
        [ -n "$h" ] || continue
        can_target "$t" || continue
        if [ "$h" = NATIVE ]; then
            echo "$t NATIVE"
        elif [ -f "/proc/sys/fs/binfmt_misc/qemu-$h" ]; then
            echo "$t qemu-$h"
        fi
    done
}

# The ones that pass every OTHER test and fail only the code-generation probe:
# reported by --list rather than dropped in silence, because "zig says it can and
# then cannot" is a finding about the toolchain worth writing down.
no_backend() {
    zig_musl_triples | while read -r t; do
        representative "$t" || continue
        h=$(handler_for_triple "$t")
        [ -n "$h" ] || continue
        [ "$h" = NATIVE ] || [ -f "/proc/sys/fs/binfmt_misc/qemu-$h" ] || continue
        can_target "$t" || echo "$t"
    done
}

if [ "$LIST" -eq 1 ]; then
    echo "tier-v-arch: candidate matrix (zig musl targets INTERSECT registered handlers)"
    echo
    printf '%-32s %-16s %s\n' TRIPLE RUNS-VIA PROPERTIES
    candidates | while read -r t h; do
        p=""
        is_big_endian "$t" && p="big-endian"
        is_32bit "$t" && p="${p:+$p, }32-bit"
        case "$t" in *abin32) p="${p:+$p, }odd ABI" ;; esac
        printf '%-32s %-16s %s\n' "$t" "$h" "${p:-little-endian, 64-bit}"
    done
    echo
    echo "dropped as non-representative: thumb* (arm encodings), soft-float siblings,"
    echo "loongarch f32/sf -- they differ in FPU ABI, not in what this rig measures."
    _nb=$(no_backend | tr '\n' ' ')
    if [ -n "$_nb" ]; then
        echo
        echo "zig SHIPS MUSL FOR THESE BUT CANNOT COMPILE THEM (no LLVM backend in this"
        echo "zig build) -- a handler exists, the libc exists, code generation does not:"
        echo "  $_nb"
    fi
    exit 0
fi

# m68k first: big-endian AND 32-bit AND 2-byte aligned is the most hostile
# combination in the set for struct packing and alignment assumptions.
order_targets() {
    candidates | awk '{print}' | sort -k1,1 | awk '
        /^m68k/ { first = first $0 "\n"; next }
        { rest = rest $0 "\n" }
        END { printf "%s%s", first, rest }'
}

if [ -n "$ONE" ]; then
    TARGETS=$(candidates | awk -v w="$ONE" '$1 == w {print}')
    [ -n "$TARGETS" ] || { echo "tier-v-arch: '$ONE' is not a runnable candidate here (try --list)" >&2; exit 2; }
elif [ "$ALL" -eq 1 ]; then
    TARGETS=$(order_targets)
else
    echo "tier-v-arch: name --arch <triple>, or --all, or --list" >&2; exit 2
fi

NT=$(printf '%s\n' "$TARGETS" | grep -c .)

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-arch: DRY RUN"
    echo "  targets : $NT"
    printf '%s\n' "$TARGETS" | sed 's/^/            /'
    echo "  build   : make CC=\"zig cc -target <triple>\" HAVE_CURL= WERROR=1"
    echo "  run     : ./run_tests under binfmt, then ./jichi --version"
    echo "  workdir : $DIR/build/<triple>   (never the repo tree)"
    echo "  results : $RESULTS"
    exit 0
fi

mkdir -p "$DIR/build" || exit 2

# SNAPSHOT ONCE, not per target (M470). The ship used to run inside the loop, so a
# commit landing mid-sweep gave later rows a different tree than earlier ones and the
# table silently stopped being comparable -- every row still printed a check count, so
# nothing looked wrong. It is why M469's commit had to wait for a 21-target sweep to
# finish before anything could be committed, which is a real cost on a rig meant to run
# unattended. One snapshot, copied per target.
SRC="$DIR/src"
rm -rf "$SRC"; mkdir -p "$SRC" || exit 2
jc_rig_ship_tar "$REPO" "${TIER_V_ARCH_DIRTY:-0}" | tar -C "$SRC" -xf - 2>/dev/null
[ -f "$SRC/Makefile" ] || { echo "tier-v-arch: the tree did not ship to $SRC" >&2; exit 2; }
: > "$RESULTS"
N_OK=0; N_FAIL=0; N_NORUN=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));   echo "ok - $*";     echo "ok    - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL  - $*" >> "$RESULTS"; }
norun(){ N_NORUN=$((N_NORUN+1)); echo "no-run - $*"; echo "NORUN - $*" >> "$RESULTS"; }
note() { echo "$*" >> "$RESULTS"; }

note "# tier-v-arch sweep"
note "# date     : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# host     : $(uname -srm)"
note "# zig      : $(zig version 2>/dev/null)"
note "# qemu-user: $(qemu-aarch64 --version 2>/dev/null | head -1 || echo 'version unknown')"
jc_rig_ship_stamp "$REPO" "${TIER_V_ARCH_DIRTY:-0}" >> "$RESULTS"
note ""

for line in $(printf '%s\n' "$TARGETS" | tr ' ' '@'); do
    t=$(printf '%s' "$line" | cut -d@ -f1)
    h=$(printf '%s' "$line" | cut -d@ -f2)
    W="$DIR/build/$t"
    say "$t  (runs via $h)"
    rm -rf "$W"; mkdir -p "$W" || { bad "$t: could not create $W"; continue; }
    cp -a "$SRC/." "$W/" || { bad "$t: could not copy the snapshot"; continue; }

    # Can this environment run the subprocess tests AT ALL? Probed per target, before
    # the suite, because of M469: all six MIPS rows reported "unit suite RAN and
    # reported failures: 11,627 checks, 73 failures", which reads as an accusation
    # against jichi -- and the cause was one level below it. `pipe()` fails under
    # qemu-mips with zig's musl (MIPS is the one Linux architecture whose pipe syscall
    # returns both descriptors in registers), so all 73 were downstream: the seven
    # failing files were exactly those that spawn a subprocess or read /proc.
    #
    # A row that blames the program for its emulator's gap is worse than no row.
    # tier-v-openbsd.sh already has the pattern (its procfs check); this is the same
    # idea, per target, and it only ever DOWNGRADES an accusation -- a passing suite is
    # reported the same whatever the probe said.
    _envp="probe-not-built"
    if zig cc -target "$t" -static -o "$W/envprobe" "$REPO/scripts/envprobe.c" >/dev/null 2>&1; then
        _envp=$( (cd "$W" && ./envprobe 2>&1 | tr '\n' ' ') )
        [ -n "$_envp" ] || _envp="probe-produced-nothing"
    fi
    note "  env probe: $_envp"
    case "$_envp" in
        *FAIL*|probe-not-built|probe-produced-nothing) _envbad=1 ;;
        *)                                             _envbad=0 ;;
    esac
    [ "$_envbad" -eq 0 ] || note "  ^ subprocess tests cannot run here; suite failures are the ENVIRONMENT'S"

    # WERROR=1 first, because zero warnings is the project standard on EVERY
    # translation unit. If that fails, retry without it -- the distinction
    # between "warns" and "will not compile" is the whole finding.
    # TWO things this got wrong first time round, both worth the comment:
    #
    #  * `make` alone builds jichi + jichi-convert, NOT run_tests -- so the row
    #    reported `./run_tests: not found` and I read the shell's "not found" as a
    #    missing ELF interpreter when the file simply had never been built. Name
    #    the targets.
    #  * LDFLAGS=-static. zig's musl output is DYNAMICALLY linked against
    #    /lib/ld-musl-<arch>.so.1 by default, which does not exist on this host,
    #    and binfmt+qemu-user then cannot run it. Static is also what makes the row
    #    honest: nothing from the host leaks in.
    _mk="make CC=zig_cc HAVE_CURL= LDFLAGS=-static run_tests jichi"
    _t0=$(date +%s)
    if (cd "$W" && make CC="zig cc -target $t" HAVE_CURL= LDFLAGS=-static WERROR=1 run_tests jichi >build.log 2>&1); then
        _secs=$(( $(date +%s) - _t0 ))
        ok "$t: WERROR=1 cross-build clean (${_secs}s)"
        _werror=clean
    elif (cd "$W" && make CC="zig cc -target $t" HAVE_CURL= LDFLAGS=-static run_tests jichi >build2.log 2>&1); then
        bad "$t: builds but NOT warning-free -- first diagnostics:"
        (cd "$W" && grep -E 'error|warning' build.log 2>/dev/null | head -6) | tee -a "$RESULTS"
        _werror=warns
    else
        bad "$t: cross-build FAILED -- first diagnostics:"
        (cd "$W" && grep -E 'error:|error ' build2.log build.log 2>/dev/null | head -8) | tee -a "$RESULTS"
        [ "$KEEP" -eq 1 ] || rm -rf "$W"
        continue
    fi

    note "  file(run_tests): $( (cd "$W" && file run_tests 2>/dev/null | cut -d: -f2-) )"

    # A target that BUILT but cannot EXECUTE is a rig result (exit 3), not a
    # jichi failure -- the M451 exit-code contract, applied per row.
    if ! (cd "$W" && ./run_tests >test.log 2>&1); then
        if (cd "$W" && grep -qE 'checks, [0-9]+ failures' test.log); then
            _r=$( (cd "$W" && grep -oE '[0-9]+ checks, [0-9]+ failures' test.log | tail -1) )
            if [ "$_envbad" -eq 1 ]; then
                norun "$t: $_r -- env probe says [$_envp]: the EMULATOR cannot run the subprocess tests"
            else
                bad "$t: unit suite RAN and reported failures: $_r"
            fi
            (cd "$W" && grep -B2 -A2 -iE 'FAIL' test.log | head -20) | tee -a "$RESULTS"
        else
            norun "$t: built, but the binary did not execute under $h ($( (cd "$W" && tail -2 test.log | tr '\n' ' ' | cut -c1-90) ))"
        fi
        [ "$KEEP" -eq 1 ] || rm -rf "$W"
        continue
    fi
    _res=$( (cd "$W" && grep -oE '[0-9]+ checks, [0-9]+ failures' test.log | tail -1) )
    case "$_res" in
        *", 0 failures") ok "$t: unit suite under $h -- $_res" ;;
        "")              norun "$t: ran but printed no '<N> checks, <M> failures' marker" ;;
        *)
            if [ "$_envbad" -eq 1 ]; then
                norun "$t: $_res -- but the env probe says [$_envp], so these are the EMULATOR'S limits, not jichi's"
            else
                bad "$t: $_res"
            fi
            ;;
    esac
    if (cd "$W" && ./jichi --version >ver.log 2>&1) && (cd "$W" && grep -q jichi ver.log); then
        ok "$t: ./jichi --version runs ($( (cd "$W" && tr -d '\n' < ver.log) ))"
    else
        norun "$t: ./jichi --version did not run"
    fi
    note ""
    [ "$KEEP" -eq 1 ] || rm -rf "$W"
done

note ""
note "# $N_OK ok, $N_FAIL failed, $N_NORUN could-not-run"
echo
echo "tier-v-arch: $N_OK ok, $N_FAIL failed, $N_NORUN could-not-run -- $RESULTS"
[ "$N_FAIL" -gt 0 ] && exit 1
[ "$N_NORUN" -gt 0 ] && exit 3
exit 0
