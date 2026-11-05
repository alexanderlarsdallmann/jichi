#!/bin/sh
# tier-v-freedos.sh -- the FreeDOS row: DJGPP-built jichi on FreeDOS 1.3, under QEMU.
#
# WHY THIS EXISTS. Both steps below were performed by hand first, on 2026-09-29
# (docs/plans/2026-09-29-the-fork-less-rows.md section 7); this is the transcript
# of a row that ran, not a guess at one. The FreeMiNT, Guix and illumos rigs set
# that rule.
#
# WHAT IT UNIQUELY EXERCISES, none of which another row can:
#   * DOS runs one program at a time, so there is no fork(): the first row where
#     jc_spawn's fork backend cannot work at all (the reason for M832);
#   * DJGPP's libc and its DPMI host (CWSDPMI): 32-bit code started from a 16-bit
#     real-mode kernel;
#   * 8.3 file names unless DOSLFN is loaded, text-mode files by default (a
#     fopen() without "b" writes CRLF), drive letters in absolute paths, and no
#     permission bits;
#   * a build with no sockets at all (JC_NO_SOCKETS, M835).
#
# WHAT IT NEEDS, all measured on the workstation:
#   * qemu-system-i386, with KVM when /dev/kvm is usable (TCG otherwise, slower);
#   * GNU mtools, to write the guest's disk and read the results back without
#     root: the operator's `sudo apt install mtools`, or a build from GNU's
#     source (4.0.49 was used), with TIER_V_MTOOLS naming its bin directory;
#   * for step 2, DJGPP (i586-pc-msdosdjgpp-gcc), with TIER_V_DJGPP naming its
#     bin directory when it is not on PATH;
#   * FreeDOS 1.3's LiteUSB image, pinned by the SHA-256 that the release itself
#     publishes (verify.txt, beside the image). The image's own
#     PACKAGES\UTIL\CWSDPMI.ZIP supplies the DPMI host, so nothing else is
#     downloaded.
#
# HOW IT DRIVES THE GUEST. The image's FDAUTO.BAT starts the installer; the rig
# replaces it, on a copy, with its own script. CTTY COM1 moves the console to the
# serial port, which QEMU writes to a file. DOS cannot redirect stderr, and a
# console on the serial port keeps every line even if a program hangs. FDAPM
# POWEROFF ends the run, and QEMU then exits by itself. The sentinel carries this
# run's id and is read from the guest's disk after the power-off, because a waiter
# that trusts whatever sentinel it finds reads an earlier run's result as this one's.
#
# Steps (--step N):
#   1  boot, write the sentinel, power off                          (12 s measured)
#   2  cross-build run_tests and jichi from the tree (curl-free, WERROR=1), run the
#      suite with DOSLFN loaded, then `jichi --version`, `jichi describe`, `jichi
#      doctor`, `jichi ls` and `jichi sysmsg`. doctor and ls show the long-name
#      check (M838) from both of its sides; sysmsg shows the prompt's date line
#      from a fresh process, where DJGPP's local time is garbage (M839)
#                                                                   (107 s measured)
#      --no-lfn runs the suite on plain 8.3 names instead            (40 s measured)
#
# Exit codes (the tier-v contract):
#   0  the step ran and every check passed
#   1  the step ran and something failed -- a RESULT, read results-freedos.txt
#   2  usage / missing tool on the host / a pinned download that does not verify
#   3  the guest never wrote THIS run's sentinel -- NOT a result, the rig failed
#
# Usage:
#   scripts/tier-v-freedos.sh --step 1
#   scripts/tier-v-freedos.sh --step 2 [--dirty] [--no-lfn]   # --dirty: the WORKING tree
#   scripts/tier-v-freedos.sh --dry-run --step 2
set -u
. "$(dirname "$0")/_rig_ship.sh"

REPO=$(cd "$(dirname "$0")/.." && pwd)
TIER_V_DIR=${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}
DL="$TIER_V_DIR/freedos-dl"

FD_ZIP=FD13-LiteUSB.zip
FD_URL="https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.3/official/$FD_ZIP"
FD_SHA=64a934585087ccd91a18c55e20ee01f5f6762be712eeaa5f456be543778f9f7e
FD_IMG=FD13LITE.img
DJCC=i586-pc-msdosdjgpp-gcc

STEP=''
DIRTY=0
DRY=0
LFN=1
DEADLINE=''
while [ $# -gt 0 ]; do
    case "$1" in
        --step)     shift; STEP="${1:-}" ;;
        --dirty)    DIRTY=1 ;;
        --no-lfn)   LFN=0 ;;
        --dry-run)  DRY=1 ;;
        --deadline) shift; DEADLINE="${1:-}" ;;
        -h|--help)  sed -n '2,55p' "$0"; exit 0 ;;
        *) echo "tier-v-freedos: unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
case "$STEP" in
    1) : "${DEADLINE:=120}" ;;    # measured 12 s; ten times that
    2) : "${DEADLINE:=1200}" ;;   # measured 107 s with DOSLFN; over ten times that
    *) echo "tier-v-freedos: --step 1 or 2 is required" >&2; exit 2 ;;
esac
[ -n "${TIER_V_MTOOLS:-}" ] && PATH="$TIER_V_MTOOLS:$PATH"
[ -n "${TIER_V_DJGPP:-}" ] && PATH="$TIER_V_DJGPP:$PATH"
# The image's geometry is not a whole number of tracks, which mtools reports on
# every call; the file system itself is sound, so the warning is silenced.
MTOOLS_SKIP_CHECK=1
export PATH MTOOLS_SKIP_CHECK

say() { printf 'tier-v-freedos: %s\n' "$*"; }

if [ "$DRY" = 1 ]; then
    say "step $STEP ($(jc_rig_ship_label "$DIRTY")), deadline ${DEADLINE}s, under $TIER_V_DIR"
    say "would fetch and verify $FD_ZIP ($FD_SHA)"
    say "would replace FDAUTO.BAT on a copy of $FD_IMG with the rig's script (CTTY COM1, the sentinel, FDAPM POWEROFF)"
    [ "$STEP" = 2 ] && say "would cross-build run_tests and jichi with $DJCC HAVE_CURL= WERROR=1, and run the suite $([ "$LFN" = 1 ] && echo 'with DOSLFN' || echo 'on plain 8.3 names')"
    say "would run: timeout $DEADLINE qemu-system-i386 [-enable-kvm] -m 256 -display none -no-reboot"
    exit 0
fi

for t in qemu-system-i386 mcopy mmd mtype curl unzip sha256sum timeout od cmp; do
    command -v "$t" > /dev/null 2>&1 || {
        echo "tier-v-freedos: missing on the host: $t" >&2
        case "$t" in m*) echo "  (GNU mtools: sudo apt install mtools, or set TIER_V_MTOOLS)" >&2 ;; esac
        exit 2; }
done
if [ "$STEP" = 2 ]; then
    command -v "$DJCC" > /dev/null 2>&1 ||
        { echo "tier-v-freedos: missing $DJCC (step 2 cross-builds with DJGPP; set TIER_V_DJGPP)" >&2; exit 2; }
fi

# fetch URL FILE SHA -- download once into $DL, and refuse anything that does not verify
fetch() {
    if [ ! -f "$DL/$2" ]; then
        mkdir -p "$DL" && curl -fsSL --max-time 300 -o "$DL/$2.part" "$1" && mv "$DL/$2.part" "$DL/$2" ||
            { echo "tier-v-freedos: download failed: $1" >&2; exit 2; }
    fi
    got=$(sha256sum "$DL/$2" | cut -d' ' -f1)
    [ "$got" = "$3" ] || { echo "tier-v-freedos: $2 is $got, pinned $3 -- refusing it" >&2; exit 2; }
}
fetch "$FD_URL" "$FD_ZIP" "$FD_SHA"

RID="fd-$(date +%s)-$$"
RUN="$TIER_V_DIR/freedos-$(date +%Y%m%d-%H%M%S)-step$STEP"
mkdir -p "$RUN" || exit 2
RES="$RUN/results-freedos.txt"
log() { printf '%s\n' "$*" >> "$RES"; }
say "run $RID in $RUN"
unzip -q -o "$DL/$FD_ZIP" "$FD_IMG" -d "$RUN" && mv "$RUN/$FD_IMG" "$RUN/work.img" || exit 2
IMG="$RUN/work.img"

# The FAT partition's first sector: the MBR's first entry, bytes 454-457, little-endian.
# shellcheck disable=SC2046
set -- $(od -An -tu1 -j454 -N4 "$IMG")
[ $# -eq 4 ] || { echo "tier-v-freedos: cannot read the image's partition table" >&2; exit 2; }
LBA=$(( $1 + $2 * 256 + $3 * 65536 + $4 * 16777216 ))
D="$IMG@@$((LBA * 512))"
mtype -i "$D" ::/FDCONFIG.SYS > /dev/null 2>&1 ||
    { echo "tier-v-freedos: mtools cannot read the FAT partition at sector $LBA" >&2; exit 2; }

log "run:      $RID"
log "step:     $STEP"
if [ "$STEP" = 2 ]; then
    jc_rig_ship_stamp "$REPO" "$DIRTY" >> "$RES"
else
    log "tree: none shipped (step 1 boots the image and powers off)"
fi
log "guest:    FreeDOS 1.3 $FD_ZIP (sha256 $FD_SHA), $(qemu-system-i386 --version 2>&1 | head -n 1)"

if [ "$STEP" = 2 ]; then
    say "cross-building run_tests and jichi ($(jc_rig_ship_label "$DIRTY"))"
    mkdir -p "$RUN/src" && jc_rig_ship_tar "$REPO" "$DIRTY" | tar -C "$RUN/src" -xf - || exit 2
    (cd "$RUN/src" && make CC="$DJCC" HAVE_CURL= WERROR=1 run_tests jichi > "$RUN/cross-build.log" 2>&1) ||
        { log "cross-build: FAILED (cross-build.log)"; say "cross-build failed"; exit 1; }
    # The row measures the build a system without sockets gets (M835); a DJGPP that
    # has grown a socket library is a different configuration and must say so.
    grep -q -- '-DJC_NO_SOCKETS' "$RUN/cross-build.log" ||
        { log "cross-build: JC_NO_SOCKETS was not set -- not the configuration this row measures"; exit 1; }
    log "cross-build: ok, run_tests.exe $(wc -c < "$RUN/src/run_tests.exe") B, jichi.exe $(wc -c < "$RUN/src/jichi.exe") B"
    # The DPMI host, from the image's own package: the stub loads it from the
    # program's directory. The long name RUN_TESTS.EXE would reach plain DOS as
    # RUN_TE~1.EXE, so the guest gets 8.3 names.
    mcopy -n -i "$D" ::/PACKAGES/UTIL/CWSDPMI.ZIP "$RUN/cwsdpmi.zip" &&
        unzip -q -o -j "$RUN/cwsdpmi.zip" BIN/CWSDPMI.EXE -d "$RUN" ||
        { echo "tier-v-freedos: no CWSDPMI.EXE in the image's PACKAGES\\UTIL\\CWSDPMI.ZIP" >&2; exit 2; }
    mmd -i "$D" ::/JICHI ::/TMP ::/HOME &&
        mcopy -i "$D" "$RUN/src/run_tests.exe" ::/JICHI/RUNTESTS.EXE &&
        mcopy -i "$D" "$RUN/src/jichi.exe" ::/JICHI/JICHI.EXE &&
        mcopy -i "$D" "$RUN/CWSDPMI.EXE" ::/JICHI/CWSDPMI.EXE || exit 2
    log "suite:    $([ "$LFN" = 1 ] && echo 'with DOSLFN' || echo 'plain 8.3 names (--no-lfn)')"
fi

# The guest's script, in DOS line endings. TMPDIR and HOME are forward-slash paths
# on C:, which DJGPP's libc accepts and jichi's path code expects.
{
    printf '@echo off\r\n'
    printf 'SET DOSDIR=\\FREEDOS\r\n'
    printf 'SET PATH=%%dosdir%%\\BIN;C:\\JICHI\r\n'
    printf 'ECHO RIG START run=%s> COM1\r\n' "$RID"
    if [ "$STEP" = 2 ]; then
        printf 'SET TMPDIR=C:/TMP\r\nSET TEMP=C:/TMP\r\nSET TMP=C:/TMP\r\nSET HOME=C:/HOME\r\n'
        [ "$LFN" = 1 ] && printf 'DOSLFN > COM1\r\n'
        printf 'CTTY COM1\r\nC:\r\nCD \\JICHI\r\n'
        printf 'RUNTESTS.EXE\r\nECHO RUNTESTS rc=%%ERRORLEVEL%%\r\n'
        printf 'JICHI.EXE --version\r\nECHO JICHI-VERSION rc=%%ERRORLEVEL%%\r\n'
        printf 'JICHI.EXE describe > C:\\DESCRIBE.TXT\r\nECHO JICHI-DESCRIBE rc=%%ERRORLEVEL%%\r\n'
        printf 'JICHI.EXE doctor > C:\\DOCTOR.TXT\r\nECHO JICHI-DOCTOR rc=%%ERRORLEVEL%%\r\n'
        printf 'JICHI.EXE ls\r\nECHO JICHI-LS rc=%%ERRORLEVEL%%\r\n'
        printf 'JICHI.EXE sysmsg > C:\\SYSMSG.TXT\r\nECHO JICHI-SYSMSG rc=%%ERRORLEVEL%%\r\n'
    fi
    printf 'ECHO RIG DONE run=%s> C:\\SENTINEL.TXT\r\n' "$RID"
    printf 'ECHO RIG DONE run=%s> COM1\r\n' "$RID"
    printf 'FDAPM POWEROFF\r\n'
} > "$RUN/fdauto.bat"
mcopy -o -i "$D" "$RUN/fdauto.bat" ::/FDAUTO.BAT || exit 2
mtype -i "$D" ::/FDAUTO.BAT | cmp -s - "$RUN/fdauto.bat" ||
    { echo "tier-v-freedos: FDAUTO.BAT on the guest disk is not the rig's script" >&2; exit 2; }

KVM=''
[ -r /dev/kvm ] && [ -w /dev/kvm ] && KVM=-enable-kvm
say "booting FreeDOS under qemu-system-i386 ($([ -n "$KVM" ] && echo KVM || echo TCG), deadline ${DEADLINE}s)"
t0=$(date +%s)
# shellcheck disable=SC2086
timeout "$DEADLINE" qemu-system-i386 $KVM -m 256 -display none -no-reboot \
    -drive "file=$IMG,format=raw,if=ide,index=0" -serial "file:$RUN/serial.log" \
    < /dev/null > "$RUN/qemu.out" 2>&1
qrc=$?
secs=$(( $(date +%s) - t0 ))
log "emulator: rc=$qrc after ${secs}s ($([ -n "$KVM" ] && echo KVM || echo TCG))"
tr -d '\r\000' < "$RUN/serial.log" > "$RUN/serial.txt" 2>/dev/null

if ! mtype -i "$D" ::/SENTINEL.TXT 2>/dev/null | tr -d '\r' | grep -qxF "RIG DONE run=$RID"; then
    log "sentinel: MISSING for $RID -- the rig failed; see serial.txt and qemu.out"
    say "no sentinel for $RID after ${secs}s (emulator rc=$qrc): not a result -- see $RUN/serial.txt"
    head -n 3 "$RUN/qemu.out" >&2
    exit 3
fi
log "sentinel: ok ($RID)"

if [ "$STEP" = 1 ]; then
    say "step 1 ok: booted, wrote the sentinel and powered off in ${secs}s"
    exit 0
fi

summary=$(grep -E '^[0-9]+ checks, [0-9]+ failures' "$RUN/serial.txt" | tail -n 1)
log "run_tests: ${summary:-no summary line} ($(grep -E '^RUNTESTS rc=' "$RUN/serial.txt" | tail -n 1))"
grep -E '^  FAIL ' "$RUN/serial.txt" >> "$RES"
grep -E 'Exiting due to signal|Stack Fault|Page fault|General Protection' "$RUN/serial.txt" | head -n 3 >> "$RES"
log "jichi --version: $(grep -E '^jichi [0-9]' "$RUN/serial.txt" | head -n 1) ($(grep -E '^JICHI-VERSION rc=' "$RUN/serial.txt" | tail -n 1))"
mcopy -n -i "$D" ::/DESCRIBE.TXT "$RUN/describe.txt" 2>/dev/null
log "jichi describe: $(grep -E '^JICHI-DESCRIBE rc=' "$RUN/serial.txt" | tail -n 1), $(wc -c < "$RUN/describe.txt" 2>/dev/null || echo 0) B"
mcopy -n -i "$D" ::/DOCTOR.TXT "$RUN/doctor.txt" 2>/dev/null
log "jichi doctor: $(grep -E '^JICHI-DOCTOR rc=' "$RUN/serial.txt" | tail -n 1); $(tr -d '\r' < "$RUN/doctor.txt" 2>/dev/null | grep -i 'long file names' | head -n 1 | sed 's/^ *//')"
log "jichi ls: $(grep -E '^JICHI-LS rc=' "$RUN/serial.txt" | tail -n 1); 8.3 warnings on the console: $(grep -c 'keeps only 8.3 file names' "$RUN/serial.txt")"
# The system prompt's date line, from a fresh process in which no TZ was ever
# valid -- the case DJGPP gets wrong (M839) -- next to the host's UTC date.
mcopy -n -i "$D" ::/SYSMSG.TXT "$RUN/sysmsg.txt" 2>/dev/null
log "jichi sysmsg: $(grep -E '^JICHI-SYSMSG rc=' "$RUN/serial.txt" | tail -n 1); $(tr -d '\r' < "$RUN/sysmsg.txt" 2>/dev/null | grep "Today's date" | head -n 1 | sed 's/^- //') (host UTC date $(date -u +%Y-%m-%d))"
say "step 2: ${summary:-no summary} -- $RES"
case "$summary" in
    *" 0 failures") exit 0 ;;
    *) exit 1 ;;
esac
