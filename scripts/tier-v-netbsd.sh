#!/bin/sh
# tier-v-netbsd.sh -- the NetBSD row, from nothing to a result (M480).
#
# WHY THIS ROW EXISTS, and what it adds that the other two BSDs cannot:
#
#   1. **procfs.** It is the ONLY non-Linux kernel in the matrix that can run
#      tests/smoke/child_fds.sh -- the driver proving M472's descriptor fence
#      (a model-issued shell inherits none of jichi's descriptors: not the run
#      journal, not the telemetry sink, not the provider socket). FreeBSD and
#      OpenBSD have no procfs, so that driver declines there and the guarantee
#      was verified on exactly ONE kernel out of five libcs. NetBSD supplies
#      /proc/<pid>/fd, and this rig mounts it -- which is why the row records
#      the mount as a step: without it the driver skips and the row would look
#      the same as OpenBSD's while proving strictly less.
#
#   2. **GCC as the system compiler on a BSD.** FreeBSD and OpenBSD are clang.
#      NetBSD 10 ships GCC 10.5 in base, so `make info`'s WARN_OPTIONAL is
#      populated (-Wlogical-op -Wduplicated-cond -Wjump-misses-init) where on
#      OpenBSD it reads "(none: this compiler has no GCC-only warning flags)".
#      Those three warnings had never been applied to jichi off glibc.
#      It also honours -fstack-clash-protection, which OpenBSD's clang ACCEPTS
#      AND IGNORES -- the defect that broke that row's build for six milestones
#      (M479). So this is the first non-Linux row where harden_flags_lint finds
#      all three of its detectors discriminating and verifies, rather than
#      reporting "cannot verify".
#
#   3. A third BSD libc, and NetBSD's own /bin/sh, for the ~200 POSIX-sh drivers.
#
# WHY IT IS SHORTER THAN tier-v-openbsd.sh. That rig drives autoinstall(8) over
# a serial console and serves a response file over HTTP, because OpenBSD ships
# no image you can just boot. NetBSD ships a **live image** -- a bootable disk
# with a full system and sshd already enabled -- so there is no installer to
# drive, no answer file, no host HTTP server, and no python3 host requirement.
# The whole install step collapses into: fetch, gunzip, resize, boot.
#
# WHAT STILL HAS TO BE TYPED AT THE CONSOLE, and the three traps in doing it:
#
#   * The live image's loader shows a 5-second countdown MENU (1 boot / 2 single
#     user / 3 boot prompt), not a bare `boot>` prompt. SPACE stops the
#     countdown, `3` reaches the prompt, and only there does `consdev com0`
#     work. Matching a bare "boot" to find that prompt fires three screens too
#     early -- "Primary Bootstrap" and "BIOS Boot" both contain it -- and the
#     text then lands in the menu, whose RETURNs boot the machine. Measured:
#     it rebooted the guest twice. The pattern is 'Choose an option'.
#   * The kernel does NOT use the serial console until told. Everything up to
#     `consdev com0` appears; then the screen clears and serial goes silent for
#     the rest of the boot. That silence is the symptom of skipping this step,
#     and it looks exactly like a hang.
#   * **A writer must stay on the fifo for the VM's whole life.** Holding the
#     write end on a fd and exiting sends EOF to qemu's stdin, and qemu then
#     stops reading stdin for good: reopening the fifo later writes into a pipe
#     nobody reads. The console looks dead when it is only deaf. Hence HOLDER.
#
# The key is planted over that console, since there is no answer file to put it
# in -- which is the one thing this rig does that the FreeBSD cloud-image rig
# gets for free from cloud-init.
#
# THE GUEST'S NON-INTERACTIVE ssh PATH IS /bin:/usr/bin. Not a footnote: it made
# pkg_add, useradd, chown, mount and sysctl all report "not found" while every
# one of them was installed, and a rig that reads those as absent tools draws
# false conclusions about the platform. Every root command here sets PATH.
#
# HOST REQUIREMENTS: qemu-system-x86_64 with KVM, curl, qemu-img, gunzip, and a
# `sleep` that accepts a fraction (GNU coreutils does; the per-character console
# typing below needs it). No python3 -- unlike the OpenBSD rig, there is no
# response file to serve.
#
# Usage (--ref-secs is REQUIRED: it is THIS bench's serial `make WERROR=1`
# seconds, the denominator of JC_SMOKE_TIMEOUT_MULT. Measure it with
# `make clean && time make WERROR=1`, median of three; never copy a published
# row's multiplier -- see docs/SESSION_RUNBOOK.md §5):
#   scripts/tier-v-netbsd.sh --ref-secs 6.66   # full row
#   scripts/tier-v-netbsd.sh --dry-run         # print the plan, touch nothing
#   scripts/tier-v-netbsd.sh --keep            # leave the VM up afterwards
#   scripts/tier-v-netbsd.sh --release 10.1    # a different release
#   scripts/tier-v-netbsd.sh --reuse           # skip fetch+provision, boot the disk
#   scripts/tier-v-netbsd.sh --dirty           # ship the WORKING tree, not HEAD:
#                                              # the only way to verify a portability
#                                              # fix before committing it
#
# Exit codes, matching the other tier-V rigs:
#   0 the row ran (read the RESULT lines for its verdict)
#   1 the row ran and something under test failed
#   2 the rig could not start (no qemu, download failed, ...)
#   3 never reached userspace -- a RIG failure, not a result about jichi
set -u

# The one implementation of JC_SMOKE_TIMEOUT_MULT, and why it is not inline here.
. "$(dirname "$0")/_rig_mult.sh"

REL="${TIER_V_NETBSD_RELEASE:-10.1}"
DIR="${TIER_V_NETBSD_DIR:-$HOME/.cache/jichi-tier-v-netbsd}"
PORT="${TIER_V_NETBSD_PORT:-2223}"
REF_SECS="${JC_REF_SECS:-}"
MEM=2048
SMP=2
DISK_GB=12
DRY=0; KEEP=0; REUSE=0; DIRTY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1 ;;
        --keep)    KEEP=1 ;;
        --reuse)   REUSE=1 ;;
        --dirty)   DIRTY=1 ;;
        --release) REL="$2"; shift ;;
        --port)    PORT="$2"; shift ;;
        --mem)     MEM="$2"; shift ;;
        --smp)     SMP="$2"; shift ;;
        --ref-secs) REF_SECS="$2"; shift ;;
        # Print the header comment block rather than a hardcoded line range: a
        # `sed -n '2,45p'` silently loses its last lines the first time an option
        # is added above, which is how tier-v-vm.sh's help lost a row (M430).
        --help|-h) awk 'NR>1 && !/^#/{exit} NR>1' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "tier-v-netbsd: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done

# Refuse before doing the work, not after: a row whose denominator is unknown
# cannot be compared with any other row, which is the point of measuring it.
[ "$DRY" -eq 1 ] || jc_rig_ref_or_die "tier-v-netbsd" "$REF_SECS" || exit 2

REPO=$(cd "$(dirname "$0")/.." && pwd)
IMGGZ="$DIR/NetBSD-$REL-amd64-live.img.gz"
URL="https://cdn.NetBSD.org/pub/NetBSD/images/$REL/NetBSD-$REL-amd64-live.img.gz"
DISK="$DIR/netbsd-$REL.qcow2"
KEY="$DIR/tierv-netbsd"
FIFO="$DIR/in.fifo"
CONSOLE="$DIR/netbsd-console.log"
# Results go in $DIR, never the repo: a rig that can dirty the tree it tests
# has already been three separate mistakes in this campaign.
RESULTS="$DIR/results-netbsd.txt"
SSH_OPTS="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8 -o IdentitiesOnly=yes"
# The guest's ssh PATH is /bin:/usr/bin, so root commands live nowhere it looks.
GPATH='PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/pkg/bin:/usr/pkg/sbin; export PATH;'
PKGURL="http://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/amd64/$REL/All"

N_OK=0; N_FAIL=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));   echo "ok - $*";     echo "ok   - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL - $*" >> "$RESULTS"; }
note() { echo "$*" >> "$RESULTS"; }
# shellcheck disable=SC2086
g()  { ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"; }
# shellcheck disable=SC2086
gr() { ssh $SSH_OPTS -i "$KEY" -p "$PORT" root@127.0.0.1 "$GPATH $*"; }

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-netbsd: DRY RUN"
    echo "  release : NetBSD $REL amd64 (LIVE IMAGE -- no installer to drive)"
    echo "  image   : $URL"
    echo "  cache   : $DIR"
    echo "  vm      : -m $MEM -smp $SMP, disk grown to ${DISK_GB}G, ssh on 127.0.0.1:$PORT"
    echo "  results : $RESULTS"
    echo "  steps   : fetch img.gz -> gunzip -> qcow2 + resize -> boot (menu:"
    echo "            SPACE, 3, consdev com0, boot) -> plant key over console"
    echo "            -> pkg_add -> mount procfs -> ship -> gmake WERROR=1"
    echo "            -> test -> smoke -> offline surfaces"
    echo "  NOTE    : NetBSD's make is BSD make; the row uses gmake deliberately."
    echo "  NOTE    : pkg-config is a REQUIRED package, not a nicety -- /usr/pkg is"
    echo "            off the base compiler's default search path, so without it the"
    echo "            libcurl compile probe reports no and jichi silently builds"
    echo "            without networking."
    echo "  NOTE    : procfs is 'noauto' in /etc/fstab. Mounting it is what lets"
    echo "            child_fds.sh run -- the only non-Linux row that can."
    exit 0
fi

for t in qemu-system-x86_64 curl qemu-img gunzip; do
    command -v "$t" >/dev/null 2>&1 || {
        echo "tier-v-netbsd: $t not found (host requirement)" >&2; exit 2; }
done
[ -w /dev/kvm ] || echo "tier-v-netbsd: warning: /dev/kvm not writable; this will be slow" >&2

mkdir -p "$DIR"
: > "$RESULTS"
note "# tier-v-netbsd row: NetBSD $REL amd64 (live image)"
note "# date        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# host        : $(uname -srm)"
note "# revision    : $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
note ""

[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N '' -C tier-v-netbsd -f "$KEY"

# ------------------------------------------------------------ 1. the media
if [ "$REUSE" -eq 0 ]; then
    say "fetch the live image (~437 MB)"
    if [ ! -s "$IMGGZ" ]; then
        curl -fL --retry 3 -C - -o "$IMGGZ" "$URL" || {
            echo "tier-v-netbsd: download failed: $URL" >&2; exit 2; }
    fi
    ok "live image present ($(( $(wc -c < "$IMGGZ") / 1048576 )) MB)"

    say "unpack + convert (raw -> qcow2, grown to ${DISK_GB}G)"
    rm -f "$DISK" "$DIR/live.raw"
    gunzip -c "$IMGGZ" > "$DIR/live.raw" || { echo "tier-v-netbsd: gunzip failed" >&2; exit 2; }
    qemu-img convert -f raw -O qcow2 "$DIR/live.raw" "$DISK" || exit 2
    # The live image's rc grows the root FFS to the disk on first boot, so this
    # resize is what turns a 1.9 GB image into ~10 GB of usable space. Without
    # it the build fits but leaves no room for the smoke tier's temp trees.
    qemu-img resize -q "$DISK" "${DISK_GB}G" || exit 2
    rm -f "$DIR/live.raw"
    ok "disk ready (root FFS grows to ${DISK_GB}G on first boot)"
fi

# -------------------------------------------------------------- 2. boot it
say "boot (kvm, ${MEM}M, ${SMP} cpu; driving the loader menu over serial)"
if [ -f "$DIR/netbsd.pid" ] && kill -0 "$(cat "$DIR/netbsd.pid" 2>/dev/null)" 2>/dev/null; then
    echo "tier-v-netbsd: a VM from a previous --keep run is still up." >&2
    echo "  stop it first:  kill \$(cat $DIR/netbsd.pid)" >&2
    exit 2
fi
rm -f "$FIFO" "$CONSOLE"; mkfifo "$FIFO"; : > "$CONSOLE"

# -nographic with stdin on a fifo, NOT -serial file: this console must be
# WRITTEN to (the key is planted through it), and -daemonize cannot give a
# writable stdin.
qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
    -device virtio-net,netdev=n0 \
    -nographic < "$FIFO" >> "$CONSOLE" 2>&1 &
VMPID=$!
echo "$VMPID" > "$DIR/netbsd.pid"
# THE HOLDER. Keeps a writer on the fifo for the VM's whole life so qemu never
# sees EOF on stdin; without it the console goes permanently deaf the first time
# a writer closes, and every later send is silently discarded. stdout is closed
# so a caller's pipeline can finish (a subshell inheriting stdout makes `| tail`
# wait for the VM -- measured, it cost a five-minute timeout).
( exec 9> "$FIFO"; while kill -0 "$VMPID" 2>/dev/null; do sleep 5; done ) >/dev/null 2>&1 &
HOLDER=$!
echo "$HOLDER" > "$DIR/holder.pid"

# CLEAN SHUTDOWN FIRST (M482). This used to `kill` qemu outright, which is
# pulling the power cord: the guest's filesystem is left dirty, and one
# interrupted run corrupted this row's disk badly enough that the next boot
# stopped at "UNEXPECTED INCONSISTENCY; RUN fsck_ffs MANUALLY" in single user,
# costing a full reinstall. `halt -p` gives the guest a chance to unmount; the
# kill stays as the fallback for a guest that never came up or has wedged, which
# is why the wait is bounded rather than open-ended.
stop_vm() {
    if [ "$KEEP" -eq 1 ]; then
        echo "tier-v-netbsd: VM left up (pid $VMPID, ssh -p $PORT -i $KEY tierv@127.0.0.1)"
        return
    fi
    if [ -n "${VMPID:-}" ]; then
        gr 'halt -p' >/dev/null 2>&1
        _sd=0
        while [ "$_sd" -lt 20 ]; do
            kill -0 "$VMPID" 2>/dev/null || break
            sleep 1; _sd=$((_sd+1))
        done
        kill "$VMPID" 2>/dev/null
    fi
    [ -n "${HOLDER:-}" ] && kill "$HOLDER" 2>/dev/null
    rm -f "$FIFO"
}
trap stop_vm EXIT INT TERM

# A real wait-for, not a sleep: it prints the pattern it gave up on, which is
# the one line that explains the run.
# Polls four times a second, not once. The console is a local file, so the grep
# is free -- and the loader's countdown is FIVE SECONDS, so a 1 s poll loses the
# race often enough to matter (it did, on the first from-scratch run).
waitfor() {
    _p=$1; _lim=$(( ${2} * 4 )); _i=0
    while [ "$_i" -lt "$_lim" ]; do
        kill -0 "$VMPID" 2>/dev/null || {
            echo "tier-v-netbsd: qemu exited while waiting for: $_p" >&2; return 2; }
        tr -d '\r' < "$CONSOLE" | grep -qF "$_p" && return 0
        sleep 0.25; _i=$((_i+1))
    done
    echo "tier-v-netbsd: TIMEOUT ($(( _lim / 4 ))s) waiting for: $_p" >&2
    tr -d '\r' < "$CONSOLE" | tail -20 >&2
    return 1
}
# Waits for the FIRST of two patterns and says which arrived. Needed because
# after `boot` the guest may either reach userspace or reboot itself (see the
# resize note below), and blindly waiting for one of those loses ~4 minutes
# before reporting the wrong reason.
waitfor2() {
    _p1=$1; _p2=$2; _lim=$(( ${3} * 4 )); _i=0
    while [ "$_i" -lt "$_lim" ]; do
        kill -0 "$VMPID" 2>/dev/null || return 2
        tr -d '\r' < "$CONSOLE" | grep -qF "$_p1" && return 0
        tr -d '\r' < "$CONSOLE" | grep -qF "$_p2" && return 1
        sleep 0.25; _i=$((_i+1))
    done
    echo "tier-v-netbsd: TIMEOUT ($(( _lim / 4 ))s) waiting for '$_p1' or '$_p2'" >&2
    return 3
}
send() { printf '%s\n' "$1" > "$FIFO"; }
# THE LOADER DROPS CHARACTERS. It has no flow control on serial, so a whole line
# written at once arrives short: `consdev com0` was received as `consdev com`,
# which reset the loader, and the next menu then auto-booted to VGA -- silence on
# serial for the rest of the boot, indistinguishable from a hang. This is the same
# rule the PTY drivers follow (human-scale delays between sends, CLAUDE.md); the
# loader needs it PER CHARACTER, not per line.
typeslow() {
    _ts=$1
    while [ -n "$_ts" ]; do
        printf '%s' "$(printf '%s' "$_ts" | cut -c1)" > "$FIFO"
        _ts=$(printf '%s' "$_ts" | cut -c2-)
        sleep 0.08
    done
    printf '\n' > "$FIFO"
}

# ONE LOADER MENU -> A KERNEL TALKING ON com0. Factored into a function because
# it has to be driven MORE THAN ONCE: see the loop below.
#
# 'Choose an option' is the MENU's own line. A bare 'boot' also occurs in
# "Primary Bootstrap" and "BIOS Boot", matches three screens too early, and the
# text then lands in the countdown menu -- whose RETURNs boot the machine.
drive_menu() {
    waitfor 'Choose an option' 120 || return 1
    # EXACTLY ONE SPACE. The first space is consumed as the countdown-stop key;
    # every later one is ordinary INPUT on the option line, so five of them made
    # the menu read "    3", reject it and re-prompt. Spamming the keystroke to
    # win the race is what lost it -- the race is fixed in waitfor's poll
    # interval (4 Hz) instead.
    printf ' ' > "$FIFO"
    # 'Option: [1]:' is printed only when the countdown was STOPPED. If it never
    # appears the menu chose "boot normally" and the console is about to go
    # silent. Confirming it before typing is what separates "the keys were sent"
    # from "the loader is listening" -- the first version of this block announced
    # "console moved to com0" after merely sending them, and was cheerfully green
    # on a run where the loader had dropped a character and reset. That is M479's
    # lesson, repeated in new code the same day.
    waitfor 'Option: [1]:' 30 || {
        echo "tier-v-netbsd: the countdown expired before SPACE landed" >&2; return 1; }
    typeslow '3'
    waitfor 'type "?" or "help"' 60 || {
        echo "tier-v-netbsd: never reached the loader's boot prompt" >&2; return 1; }
    typeslow 'consdev com0'
    # The loader ECHOES what it received, so the echo is the check: if the console
    # shows the whole string, no character was dropped. The only cheap way to tell
    # "typed" from "arrived".
    waitfor 'consdev com0' 30 || {
        echo "tier-v-netbsd: the loader received a TRUNCATED consdev command" >&2
        tr -d '\r' < "$CONSOLE" | tail -6 >&2; return 1; }
    # `consdev` REOPENS the console and reprints the banner on it, and the first
    # character typed across that switch is LOST: `boot` arrived as `oot`, the
    # loader answered "unknown command", and it sat at its prompt until the row
    # timed out 240 s later with nothing on stdout to say why. So let the switch
    # settle, flush with a bare newline (harmless here), type, and verify the
    # echo -- retrying, because an unknown command costs nothing and a dropped
    # 'b' costs the row.
    _booted=0
    for _try in 1 2 3; do
        sleep 2
        printf '\n' > "$FIFO"
        sleep 1
        typeslow 'boot'
        if waitfor '> boot' 10; then _booted=1; break; fi
        echo "tier-v-netbsd: boot command arrived truncated (attempt $_try), retrying" >&2
    done
    [ "$_booted" -eq 1 ] || {
        echo "tier-v-netbsd: the loader never echoed a complete 'boot'" >&2
        tr -d '\r' < "$CONSOLE" | tail -8 >&2; return 1; }
    return 0
}

# THE LIVE IMAGE REBOOTS ITSELF ONCE. On the first boot of a disk we grew, its rc
# grows the root disklabel and FFS to match ("Growing ld0 disklabel (1907MB ->
# 12288MB)", ~7 s) and then calls reboot -- `reboot: rebooted by root` on the
# console. So the loader menu appears TWICE, and a rig that drives only the first
# one leaves the second to time out and auto-boot to VGA: serial goes silent, the
# row fails at `login:` four minutes later, and the log says nothing about a
# resize. Driving every menu that appears is the fix.
#
# The console log is ROTATED between boots so these same simple content greps keep
# working -- the full transcript is appended to netbsd-console-full.log first,
# because a discarded console is how a boot-time finding gets lost.
: > "$CONSOLE"
if [ "$REUSE" -eq 1 ]; then
    # Provisioning wrote /boot.cfg with consdev=com0, so a reused disk talks to
    # serial from the loader on. No typing, and no resize (it happened once). The
    # fallback is not defensive padding: if that file is ever lost, driving the
    # menu is exactly the recovery, and failing the row instead would waste the
    # 15-minute install that produced the disk.
    if waitfor 'login:' 180; then
        ok "booted straight to login: (/boot.cfg pins consdev=com0 -- no typing)"
    else
        say "no serial login prompt on the reused disk -- falling back to the menu"
        drive_menu || exit 3
        waitfor 'login:' 240 || exit 3
        ok "kernel console is on com0 and userspace booted (menu fallback)"
    fi
else
_boot_n=0
while : ; do
    _boot_n=$((_boot_n+1))
    [ "$_boot_n" -gt 3 ] && { bad "the guest rebooted more than twice -- not the resize"; exit 3; }
    drive_menu || exit 3
    waitfor2 'login:' 'rebooted by root' 300
    case $? in
        0) ok "kernel console is on com0 and userspace booted (login: reached, boot $_boot_n)"
           break ;;
        1) say "the guest grew its root filesystem and rebooted itself -- driving the loader again"
           note "boot $_boot_n: resized the root FFS and rebooted (expected, once)"
           cat "$CONSOLE" >> "$DIR/netbsd-console-full.log"; : > "$CONSOLE" ;;
        *) exit 3 ;;
    esac
done
fi

# ------------------------------------------- 3. plant the key over the console
# There is no answer file on this path, so the console IS the provisioning
# channel. The live image logs root in with no password; that is a property of
# the image, and it is why this rig never needs a password anywhere.
if [ "$REUSE" -eq 0 ]; then
    say "plant the row key over the console (no answer file on a live image)"
    send 'root'
    sleep 4
    send "mkdir -p /root/.ssh && chmod 700 /root/.ssh && echo '$(cat "$KEY.pub")' > /root/.ssh/authorized_keys && chmod 600 /root/.ssh/authorized_keys && echo KEY-PLANTED"
    waitfor 'KEY-PLANTED' 60 || { bad "could not plant the key over the console"; exit 3; }
    # consdev in /boot.cfg makes every LATER boot serial-native, so --reuse runs
    # need no typing at all. The first boot still has to be typed at: this file
    # can only be written once we are inside.
    send 'printf "menu=Boot normally:boot\nconsdev=com0\ntimeout=3\n" > /boot.cfg && echo BOOTCFG-OK'
    waitfor 'BOOTCFG-OK' 30 || note "    warning: /boot.cfg not written; --reuse will still need the menu typing"
    ok "key planted; /boot.cfg pins consdev=com0 for later boots"
fi

_up=0
for _i in $(seq 1 60); do
    if gr true 2>/dev/null; then _up=1; break; fi
    sleep 5
done
[ "$_up" -eq 1 ] || { bad "never reached userspace -- no ssh after ~5 min"; exit 3; }
ok "ssh reachable: NetBSD guest is up"

# ------------------------------------------------------- 4. provision + user
if [ "$REUSE" -eq 0 ]; then
    say "pkg_add the toolchain (gmake, pkg-config, curl, git-base, poppler-utils)"
    # pkg-config is NOT optional here. The Makefile's libcurl check is a compile
    # probe with no CFLAGS of its own; pkgsrc installs to /usr/pkg, which the base
    # compiler does not search, so without pkg-config the probe reports no and
    # jichi builds a NETWORKLESS binary while `make info` says only
    # "HAVE_CURL =" with no reason. git-base is for the `attempt` subcommand's
    # worktree isolation -- attempt_tainted.sh fails without it, and the failure
    # names snapshots rather than git.
    # poppler-utils supplies pdftotext: not a jichi dependency (the PDF path shells
    # out and errors actionably when it is missing), but without it `pdf` and
    # `docs_pdf` decline, so two of this row's declines were a package nobody had
    # installed rather than anything about the platform (M482).
    if gr "PKG_PATH='$PKGURL' pkg_add -I gmake pkg-config curl git-base poppler-utils >/tmp/pkg.log 2>&1 && echo PKG_OK" \
            2>/dev/null | grep -q PKG_OK; then
        ok "toolchain present (gmake, not make: NetBSD's make is BSD make)"
    else
        bad "pkg_add failed"; gr 'tail -15 /tmp/pkg.log' >> "$RESULTS" 2>&1; exit 1
    fi

    # THE STEP THAT MAKES THIS ROW WORTH RUNNING. procfs is 'noauto' in
    # /etc/fstab; mounted, /proc/<pid>/fd exists and child_fds.sh runs -- the
    # only non-Linux row where M472's descriptor fence is verified rather than
    # assumed. Proven both ways during M480: unmounted the driver prints
    # "1..0 # skip", mounted it reports three checks.
    if gr 'mount_procfs /proc /proc 2>/dev/null; test -e /proc/self/status && echo PROC_OK' \
            2>/dev/null | grep -q PROC_OK; then
        ok "procfs MOUNTED -- child_fds.sh can run here (no other non-Linux row can)"
    else
        bad "procfs would not mount; child_fds.sh will decline and the row proves less"
    fi

    # The row runs UNPRIVILEGED on purpose: as root, doctor refuses the hardened
    # posture and the row would measure the posture rather than the platform.
    # NetBSD's useradd puts a user in the shared `users` group -- there is no
    # per-user group to chown to, and naming one fails with "invalid group name".
    gr 'useradd -m -s /bin/sh tierv 2>/dev/null;
        mkdir -p ~tierv/.ssh && cp /root/.ssh/authorized_keys ~tierv/.ssh/ &&
        chown -R tierv:users ~tierv/.ssh && chmod 700 ~tierv/.ssh' 2>/dev/null
    g true 2>/dev/null && ok "unprivileged row user ready" || { bad "no tierv user"; exit 1; }
else
    # --reuse still has to mount procfs: it is not persistent across boots.
    gr 'mount_procfs /proc /proc 2>/dev/null; test -e /proc/self/status && echo PROC_OK' \
        2>/dev/null | grep -q PROC_OK \
        && ok "procfs re-mounted (noauto: it does not survive a boot)" \
        || bad "procfs would not mount on the reused disk"
fi

# GUARD-THEN-INSTALL, on EVERY run including --reuse. The toolchain block above is
# inside the REUSE=0 branch, so a package added to it never reaches a disk that was
# provisioned before the change -- which is how the poppler addition first appeared
# to do nothing: the row still declined `pdf`, from a rig that had been told to
# install pdftotext. Guarding on the command rather than on the package makes this
# idempotent, so it is safe to run every time and it repairs an older disk in place.
if gr "command -v pdftotext >/dev/null 2>&1 ||
       PKG_PATH='$PKGURL' pkg_add -I poppler-utils >/dev/null 2>&1;
       command -v pdftotext >/dev/null" 2>/dev/null; then
    ok "pdftotext present (the pdf/docs_pdf drivers can run)"
else
    note "    pdftotext ABSENT: pdf and docs_pdf will decline (not a jichi \
dependency -- the PDF path shells out and errors actionably without it)"
fi

# ---------------------------------------------------------------- 5. identity
note ""
note "## identity"
g 'uname -a; cc --version 2>&1 | head -1; echo "sh -> $(ls -l /bin/sh)"' >> "$RESULTS" 2>&1
gr 'sysctl -n hw.ncpu hw.physmem64 | tr "\n" " "; echo' >> "$RESULTS" 2>&1
if g 'test -e /proc/self/status && echo HAVE_PROC || echo NO_PROC' 2>/dev/null | grep -q HAVE_PROC; then
    ok "procfs is PRESENT (the axis: the descriptor-fence driver can run)"
fi

# ------------------------------------------------------------------ 6. ship
if [ "$DIRTY" -eq 1 ]; then
    say "ship the WORKING TREE (--dirty: uncommitted changes included)"
else
    say "ship the tree at HEAD"
fi
g 'rm -rf ~/jichi && mkdir -p ~/jichi' 2>/dev/null
_head=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ "$DIRTY" -eq 1 ]; then
    _dirtyn=$(git -C "$REPO" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
    printf 'tree: WORKING TREE (--dirty), %s path(s) differ from %s -- NOT a commit\n' \
           "$_dirtyn" "$_head" >> "$RESULTS"
    git -C "$REPO" ls-files -z \
      | tar -C "$REPO" --null -T - -cf - 2>/dev/null \
      | g 'cd ~/jichi && tar xf -' 2>/dev/null
else
    printf 'tree: HEAD %s (clean archive)\n' "$_head" >> "$RESULTS"
    git -C "$REPO" archive --format=tar HEAD | g 'cd ~/jichi && tar xf -' 2>/dev/null
fi
g 'test -f ~/jichi/Makefile' 2>/dev/null || { bad "tree did not ship"; exit 1; }
if [ "$DIRTY" -eq 1 ]; then
    ok "working tree shipped (${_dirtyn} path(s) uncommitted -- row NOT reproducible from a commit)"
else
    ok "tree shipped (HEAD $_head)"
fi

# ------------------------------------------------------------------ 7. the row
note ""
note "## make info"
# WARN_OPTIONAL is the line to read here: NetBSD is the first non-Linux row
# where it is non-empty, because base cc is GCC.
g 'cd ~/jichi && gmake info 2>&1 | head -10' >> "$RESULTS" 2>&1

say "build (WERROR=1)"
_t0=$(date +%s)
if g 'cd ~/jichi && gmake WERROR=1 >/tmp/build.log 2>&1 && echo BUILD_OK' 2>/dev/null | grep -q BUILD_OK; then
    _t1=$(date +%s); _secs=$((_t1-_t0))
    ok "WERROR=1 build clean on NetBSD (${_secs}s)"
    if _mult=$(jc_rig_mult "$_secs" "$REF_SECS"); then
        note "    JC_SMOKE_TIMEOUT_MULT = ceil(${_secs}s / ${REF_SECS}s) = $_mult"
        printf 'multiplier: ceil(device %ss / ref %ss) = %s\n' \
               "$_secs" "$REF_SECS" "$_mult" >> "$RESULTS"
    else
        bad "multiplier NOT derivable from device='${_secs}' ref='${REF_SECS}' -- no row"
        exit 1
    fi
else
    bad "build failed -- first diagnostics follow"
    # NOT `grep -E "error|warning"`: -Werror is in every command line, so that
    # pattern returns 25 lines of noise and no diagnostics. Measured.
    g 'grep -E "error:|warning:" /tmp/build.log 2>/dev/null | head -20' >> "$RESULTS" 2>&1
    exit 1
fi

say "unit suite"
if g 'cd ~/jichi && gmake test 2>&1 | tail -3' 2>/dev/null | tee -a "$RESULTS" \
        | grep -q '0 failures'; then
    ok "unit suite: 0 failures"
else
    bad "unit suite had failures"
fi

say "smoke tier"
# JC_SMOKE_KEEP_GOING=1 (M466): report EVERY failing driver in this one boot.
# The tier is fail-fast by default, which is right where a fix loop is seconds
# long and wrong here -- a remote row costs a boot, and fail-fast turns an
# N-defect platform into N boots. Omitting it during M480 stopped the tier at
# its first failure and hid the other 190 drivers behind one missing package.
if g "cd ~/jichi && JC_SMOKE_TIMEOUT_MULT=$_mult JC_SMOKE_KEEP_GOING=1 gmake smoke >/tmp/smoke.log 2>&1; echo smoke_rc=\$?" \
        2>/dev/null | tee -a "$RESULTS" | grep -q 'smoke_rc=0'; then
    ok "smoke tier: OK"
    g 'grep -E "^smoke: OK" /tmp/smoke.log' 2>/dev/null | tee -a "$RESULTS" >/dev/null
else
    bad "smoke tier did not pass -- the failing checks follow"
    {
        # NOT anchored: run.sh indents a nested driver's checks as "    | not ok 3".
        echo "--- every failing check, and the driver banners ---"
        g 'grep -nE "not ok|FAILED \(in suite\)|ALSO fails|classify the failure" /tmp/smoke.log | head -40' 2>/dev/null
        echo "--- the standalone re-run, which is the classified diagnosis ---"
        g 'sed -n "/ALSO fails standalone/,\$p" /tmp/smoke.log | head -45' 2>/dev/null
    } | tee -a "$RESULTS"
fi
# Bring the WHOLE log home: the host's tools are GNU and a wrong pattern can be
# retried without booting a VM.
SMOKE_LOG="$DIR/smoke-netbsd.log"
if scp $SSH_OPTS -i "$KEY" -P "$PORT" \
        tierv@127.0.0.1:/tmp/smoke.log "$SMOKE_LOG" >/dev/null 2>&1; then
    note "    full smoke log: $SMOKE_LOG ($(wc -l < "$SMOKE_LOG" | tr -d ' ') lines)"
    # "209 drivers passed" and "209 drivers ran" are different claims, and only
    # the skip list separates them. Unanchored, because of the indent noted above.
    _skipped=$(grep -c '1\.\.0' "$SMOKE_LOG" 2>/dev/null || echo 0)
    {
        printf 'drivers that declined to run (TAP 1..0): %s\n' "$_skipped"
        grep -B3 '1\.\.0' "$SMOKE_LOG" 2>/dev/null \
          | grep -E 'smoke: [a-z_]+$|# skip' | head -40
    } | tee -a "$RESULTS"
    # The row's headline, stated as a check rather than left to a reader: this
    # driver DECLINES on every other non-Linux platform.
    # -e, because the pattern STARTS WITH DASHES and grep parses it as options
    # otherwise. The first cut of this check did not, so grep errored, the pipe
    # produced nothing, and the row reported "child_fds did not run" on a run whose
    # log shows it running green three lines later. It failed CLOSED -- a false red
    # -- which is the safe direction and the opposite of M479's false greens, and it
    # was caught on the first run precisely because it was loud.
    if grep -A4 -e '--- smoke: child_fds' "$SMOKE_LOG" 2>/dev/null | grep -q '^ok 1'; then
        ok "child_fds RAN: M472's descriptor fence verified on a non-Linux kernel"
    else
        bad "child_fds did not run -- procfs mount lost, and the row proves less than OpenBSD's"
    fi
else
    note "    could not copy /tmp/smoke.log back -- skip accounting unavailable"
fi

say "offline surfaces"
for s in "--version" "doctor" "describe" "context"; do
    if g "cd ~/jichi && ./jichi $s >/dev/null 2>&1 && echo SURFACE_OK" 2>/dev/null \
       | grep -q SURFACE_OK; then
        ok "offline surface: $s"
    else
        bad "offline surface failed: $s"
    fi
done

note ""
note "# $N_OK ok, $N_FAIL failed"
echo
echo "tier-v-netbsd: $N_OK ok, $N_FAIL failed -- $RESULTS"
[ "$N_FAIL" -eq 0 ] || exit 1
exit 0
