#!/bin/sh
# tier-v-bsd.sh -- the first NON-LINUX kernel row.
#
# WHY THIS ROW EXISTS. jichi's portability claim is POSIX, not Linux, and until
# now every target that has ever built it has run a Linux kernel: glibc, musl,
# bionic and uClibc are four Linux libcs. PLATFORMS.md's BSD row says "Never
# compiled. No BSD-specific code exists, and no BSD conditional either -- so
# anything Linux-shaped that leaked in (procfs, /proc/self/statm for the memory
# watchdog) simply degrades. Untried."
#
# *Degrades* is a prediction. Four source files read /proc -- jc_meminfo.c,
# jc_proc.c, jc_platform_posix.c and main.c, for /proc/self/status,
# /proc/self/stat and /proc/self/exe -- and FreeBSD does not mount procfs by
# default. This row turns the prediction into a measurement.
#
# WHAT IT UNIQUELY EXERCISES, none of which any Linux row can:
#   * a fifth libc (FreeBSD's), and its headers under -std=c89 -pedantic;
#   * the absence of procfs, on four real code paths;
#   * every Makefile probe (HAVE_VSNPRINTF, HAVE_MALLOC_TRIM, HAVE_CLOCK, the
#     M459 dialect probe) against a non-glibc, non-Linux platform;
#   * GNU make vs BSD make -- FreeBSD's `make` is bmake, so the tree needs
#     `gmake`, and whether the documentation says so is part of the row.
#
# PREDICTIONS, recorded before the first run so the row can refute them:
#   1. the build succeeds with zero diagnostics under WERROR=1;
#   2. HAVE_MALLOC_TRIM probes ABSENT (malloc_trim is glibc);
#   3. the memory watchdog degrades rather than crashing (no procfs);
#   4. doctor reports a machine profile, possibly without a RAM figure;
#   5. the unit suite passes; the smoke tier is the uncertain half, since it
#      shells out constantly and BSD userland differs in the small.
#
# Modelled on scripts/tier-v-vm.sh: same download -> seed -> boot ->
# provision-over-ssh -> in-guest runbook -> results shape, and the same
# exit-code contract. NOTE: there is no tier-v-guix.sh to copy; M458's Guix row
# was hand-driven from a committed image definition.
#
# Exit codes (the tier-v contract):
#   0  the row ran and every check passed
#   1  the row ran and something failed -- a RESULT, read results-bsd.txt
#   2  usage / missing tool on the host
#   3  never reached userspace (no ssh) -- NOT a result, the rig failed
#
# Usage (--ref-secs is REQUIRED: THIS bench's serial `make WERROR=1` seconds, the
# denominator of JC_SMOKE_TIMEOUT_MULT. `make clean && time make WERROR=1`, median
# of three. Never copy a published row's multiplier -- docs/SESSION_RUNBOOK.md §5):
#   scripts/tier-v-bsd.sh --ref-secs 4.38
#   scripts/tier-v-bsd.sh                       # full row
#   scripts/tier-v-bsd.sh --dry-run             # print every step, touch nothing
#   scripts/tier-v-bsd.sh --console             # boot to a serial console
#   scripts/tier-v-bsd.sh --keep                # leave the VM running
#   scripts/tier-v-bsd.sh --release 14.3-RELEASE
#   scripts/tier-v-bsd.sh --dirty               # ship the WORKING tree, not HEAD:
#                                               # the only way to verify a
#                                               # portability fix before committing
set -u

# The one implementation of JC_SMOKE_TIMEOUT_MULT, and why it is not inline here.
. "$(dirname "$0")/_rig_mult.sh"
# The one implementation of "put the tree on the target" (M466), and its --dirty mode.
. "$(dirname "$0")/_rig_ship.sh"

REL="${TIER_V_BSD_RELEASE:-15.1-RELEASE}"
DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}"
PORT="${TIER_V_SSH_PORT:-2233}"
MEM="${TIER_V_MEM:-2048}"
SMP="${TIER_V_SMP:-2}"
REF_SECS="${JC_REF_SECS:-}"
DRY=0; CONSOLE=0; KEEP=0; DIRTY=0
REPO=$(cd "$(dirname "$0")/.." && pwd)

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1 ;;
        --console) CONSOLE=1 ;;
        --keep)    KEEP=1 ;;
        --dirty)   DIRTY=1 ;;
        --release) REL="$2"; shift ;;
        --port)    PORT="$2"; shift ;;
        --ref-secs) REF_SECS="$2"; shift ;;
        # The header block, not a hardcoded line range: adding an option above a
        # `sed -n '2,45p'` silently truncates the help (M430 lost a row that way).
        --help|-h) awk 'NR>1 && !/^#/{exit} NR>1' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "tier-v-bsd: unknown option '$1' (try --help)" >&2; exit 2 ;;
    esac
    shift
done

# Refuse before the download and install, not after: a row whose denominator is
# unknown cannot be compared with any other row. --dry-run is exempt so the plan
# can be read without a measurement in hand.
[ "$DRY" -eq 1 ] || jc_rig_ref_or_die "tier-v-bsd" "$REF_SECS" || exit 2

for t in qemu-system-x86_64 qemu-img curl xz ssh scp ssh-keygen xorriso; do
    command -v "$t" >/dev/null 2>&1 || {
        echo "tier-v-bsd: missing host tool: $t" >&2; exit 2; }
done

BASE="FreeBSD-$REL-amd64-BASIC-CLOUDINIT-ufs"
URL="https://download.freebsd.org/releases/VM-IMAGES/$REL/amd64/Latest/$BASE.qcow2.xz"
XZ="$DIR/$BASE.qcow2.xz"
IMG="$DIR/$BASE.qcow2"
WORK="$DIR/bsd-work.qcow2"
SEED="$DIR/bsd-seed.iso"
KEY="$DIR/tier-v-key"
# Into the CACHE, not the repo -- matching tier-v-vm.sh, which writes
# "$DIR/results-$ROW.txt". The first version of this rig wrote into the
# repository root and the artifact promptly got committed by a `git add -A`:
# the third run-artifact-in-git mistake of this campaign, after .fleet-results
# and .tier-b-results. A rig should not be able to dirty the tree it tests.
RESULTS="$DIR/results-bsd.txt"
SSH_OPTS="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8"

mkdir -p "$DIR"
N_OK=0; N_FAIL=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));   echo "ok - $*";     echo "ok   - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL - $*" >> "$RESULTS"; }
note() { echo "$*" >> "$RESULTS"; }
run()  { echo "+ $*"; [ "$DRY" -eq 1 ] || "$@"; }

# shellcheck disable=SC2086
g()  { ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"; }
# shellcheck disable=SC2086
gr() { ssh $SSH_OPTS -i "$KEY" -p "$PORT" root@127.0.0.1 "$@"; }

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-bsd: DRY RUN"
    echo "  release : $REL"
    echo "  image   : $URL"
    echo "  cache   : $DIR"
    echo "  vm      : -m $MEM -smp $SMP, ssh on 127.0.0.1:$PORT"
    echo "  results : $RESULTS"
    echo "  steps   : fetch -> seed -> boot -> pkg install -> ship -> gmake -> gate"
    echo "  NOTE    : FreeBSD's make is bmake; the row uses gmake deliberately."
    exit 0
fi

: > "$RESULTS"
note "# tier-v-bsd row: FreeBSD $REL amd64"
note "# date        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# host        : $(uname -srm)"
note "# revision    : $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
note ""

# ------------------------------------------------------------ 1. stage the image
say "stage $BASE"
if [ ! -f "$IMG" ]; then
    [ -f "$XZ" ] || run curl -fL --no-progress-meter --retry 3 -o "$XZ.part" "$URL" || {
        echo "tier-v-bsd: download failed" >&2; exit 2; }
    [ -f "$XZ" ] || mv "$XZ.part" "$XZ"
    say "decompress (this image is ~600 MB compressed)"
    xz -dk -c "$XZ" > "$IMG.part" && mv "$IMG.part" "$IMG"
fi
[ -f "$IMG" ] || { echo "tier-v-bsd: no image at $IMG" >&2; exit 2; }
ok "image staged: $(du -h "$IMG" | cut -f1)"

rm -f "$WORK"
qemu-img create -q -f qcow2 -F qcow2 -b "$IMG" "$WORK" 20G
ok "overlay created (the base image stays pristine)"

# --------------------------------------------------------------- 2. seed the VM
[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -C tier-v -f "$KEY"
_sd="$DIR/seed-bsd"; rm -rf "$_sd"; mkdir -p "$_sd"
{
    echo "#cloud-config"
    # PROVISION AT BOOT, not over ssh. Two Linux assumptions failed here in
    # turn: FreeBSD ships no `sudo` ("sh: sudo: not found"), and its sshd
    # defaults to PermitRootLogin no, so seeding root's key gets
    # "Permission denied (publickey)". cloud-init itself already runs as root,
    # so `packages:` sidesteps the whole question -- and the row then needs no
    # privileged channel at all, which is a better shape than the Linux rig's.
    #
    # The row runs as the UNPRIVILEGED user deliberately: as root, doctor
    # refuses the posture ("running as root -- the privileged-command policy is
    # moot"), exactly as under proot-distro (M459), and that would measure the
    # posture rather than BSD.
    echo "packages:"
    for _p in git gmake curl pkgconf; do echo "  - $_p"; done
    echo "package_update: true"
    echo "users:"
    echo "  - name: tierv"
    echo "    sudo: 'ALL=(ALL) NOPASSWD:ALL'"
    # /bin/sh, NOT /bin/bash: FreeBSD ships no bash. Copying the Linux rig's
    # user block verbatim would create an account with an absent shell.
    echo "    shell: /bin/sh"
    echo "    lock_passwd: true"
    echo "    ssh_authorized_keys:"
    echo "      - $(cat "$KEY.pub")"
    echo "ssh_pwauth: false"
} > "$_sd/user-data"
{ echo "instance-id: tier-v-bsd"; echo "local-hostname: tier-v-bsd"; } > "$_sd/meta-data"
xorriso -as mkisofs -volid cidata -joliet -rock -output "$SEED" \
        "$_sd/user-data" "$_sd/meta-data" >/dev/null 2>&1
ok "cloud-init seed built"

# ----------------------------------------------------------------- 3. boot
if [ "$CONSOLE" -eq 1 ]; then
    exec qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
        -drive file="$WORK",if=virtio,format=qcow2 \
        -drive file="$SEED",if=virtio,format=raw,readonly=on \
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
        -device virtio-net,netdev=n0 -nographic
fi

# A --keep VM from a previous run holds the pidfile lock, and qemu's complaint
# ("cannot create PID file") names the lock rather than the cause. Check by
# PIDFILE, not by process name: `pgrep -x qemu-system-x86_64` matches NOTHING,
# because Linux truncates comm to 15 characters and pgrep -x compares against
# that -- a blind spot worth knowing about in any -x check.
if [ -f "$DIR/bsd.pid" ] && kill -0 "$(cat "$DIR/bsd.pid" 2>/dev/null)" 2>/dev/null; then
    echo "tier-v-bsd: a VM from a previous --keep run is still up (pid $(cat "$DIR/bsd.pid"))." >&2
    echo "  stop it first:  kill \$(cat $DIR/bsd.pid)" >&2
    exit 2
fi

say "boot (kvm, ${MEM}M, ${SMP} cpu)"
# -display none, NOT -nographic: qemu refuses "-nographic cannot be used with
# -daemonize", and -nographic's only job here (serial to the terminal) is
# already done better by -serial file:. The first run of this rig died on
# exactly that, and -- worse -- discarded qemu's stderr, so the operator got a
# bare exit 2 instead of the one sentence that explains it. Never swallow the
# message that names the cause.
if ! qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
    -drive file="$WORK",if=virtio,format=qcow2 \
    -drive file="$SEED",if=virtio,format=raw,readonly=on \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
    -device virtio-net,netdev=n0 -display none \
    -serial file:"$DIR/bsd-console.log" \
    -daemonize -pidfile "$DIR/bsd.pid" 2>"$DIR/qemu-err.log"; then
    echo "tier-v-bsd: qemu failed to start:" >&2
    sed 's/^/  /' "$DIR/qemu-err.log" >&2
    exit 2
fi

VMPID=$(cat "$DIR/bsd.pid" 2>/dev/null)
stop_vm() {
    [ "$KEEP" -eq 1 ] && { echo "tier-v-bsd: VM left running (pid $VMPID, ssh -p $PORT tierv@127.0.0.1)"; return; }
    [ -n "${VMPID:-}" ] && kill "$VMPID" 2>/dev/null
}
trap stop_vm EXIT INT TERM

say "wait for sshd (cloud-init grows the filesystem first; be patient)"
_up=0
for _i in $(seq 1 90); do
    if g true 2>/dev/null; then _up=1; break; fi
    sleep 5
done
if [ "$_up" -eq 0 ]; then
    bad "never reached userspace -- no ssh after ~7.5 min"
    note ""
    note "last 20 console lines:"
    tail -20 "$DIR/bsd-console.log" 2>/dev/null | sed 's/^/    /' >> "$RESULTS"
    echo "tier-v-bsd: see $DIR/bsd-console.log" >&2
    exit 3      # the rig failed; NOT a result about jichi
fi
ok "ssh reachable: FreeBSD guest is up"

# -------------------------------------------------------------- 4. identity
note ""
note "## identity"
g 'uname -a; cc --version 2>&1 | head -1; echo "sh: $(command -v sh)"' >> "$RESULTS" 2>&1
g 'sysctl -n hw.ncpu hw.physmem 2>/dev/null | tr "\n" " "; echo' >> "$RESULTS" 2>&1
note ""
# The load-bearing question this row exists for.
if g 'test -e /proc/self/status && echo HAVE_PROC || echo NO_PROC' 2>/dev/null | grep -q NO_PROC; then
    ok "procfs is ABSENT (the condition four source files must survive)"
    note "    /proc/self/status does not exist -- as predicted"
else
    note "    NOTE: procfs IS present here; the degradation path is NOT exercised"
    ok "procfs present (unexpected -- prediction 3 not tested by this run)"
fi

# ------------------------------------------------------------ 5. provisioning
say "verify the toolchain cloud-init installed at boot"
# Capture, do not discard. The FIRST version of this block sent pkg's output to
# /dev/null and reported only "pkg install failed", repeating in the same script
# the mistake the qemu block above documents. A rig that hides the message
# naming its own failure is the instrument class this whole tier exists to
# avoid.
_missing=""
for _t in git gmake curl; do
    g "command -v $_t >/dev/null 2>&1" 2>/dev/null || _missing="$_missing $_t"
done
if [ -z "$_missing" ]; then
    ok "toolchain present (gmake, not make: FreeBSD's make is bmake)"
else
    bad "cloud-init did not install:$_missing"
    note ""
    note "cloud-init output tail:"
    g 'tail -30 /var/log/cloud-init-output.log 2>/dev/null' 2>/dev/null \
        | sed 's/^/    /' >> "$RESULTS"
    echo "tier-v-bsd: missing$_missing -- see $RESULTS" >&2
    exit 1
fi

# ---------------------------------------------------------------- 6. ship
say "$(jc_rig_ship_label "$DIRTY")"
g 'rm -rf ~/jichi && mkdir -p ~/jichi' 2>/dev/null
# Provenance in the results file BEFORE the row runs, so a dirty row cannot be
# quoted later as a reproducible one.
jc_rig_ship_stamp "$REPO" "$DIRTY" >> "$RESULTS"
jc_rig_ship_tar "$REPO" "$DIRTY" | g 'cd ~/jichi && tar xf -' 2>/dev/null
if g 'test -f ~/jichi/Makefile && echo SHIPPED' 2>/dev/null | grep -q SHIPPED; then
    # The exact revision is already in RESULTS from the stamp above; this line
    # only has to make a dirty row impossible to miss on the operator's screen.
    if [ "$DIRTY" = 1 ]; then
        ok "working tree shipped -- row is NOT reproducible from a commit"
    else
        ok "tree shipped"
    fi
else
    bad "tree did not ship"; exit 1
fi

# ------------------------------------------------------------ 7. the row
say "make info (which probes fired on a non-Linux libc)"
note ""
note "## make info"
g 'cd ~/jichi && gmake info 2>&1' >> "$RESULTS" 2>&1

say "build (WERROR=1)"
_t0=$(date +%s)
if g 'cd ~/jichi && gmake WERROR=1 >/tmp/build.log 2>&1 && echo BUILD_OK' 2>/dev/null | grep -q BUILD_OK; then
    _t1=$(date +%s); _secs=$((_t1-_t0))
    ok "WERROR=1 build clean on FreeBSD (${_secs}s)"
    # M464: this row computed NO multiplier and ran `gmake smoke` bare, so
    # JC_SMOKE_TIMEOUT_MULT was unset -- i.e. 1, the tightest possible deadlines --
    # on an emulated guest slower than the bench that set those defaults. That is a
    # candidate cause of this row's undiagnosed stop at 185 of 198 drivers, and it
    # had to be ruled out before the stop could be called a finding about jichi.
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
    note ""
    g 'grep -E "error|warning" /tmp/build.log 2>/dev/null | head -20' >> "$RESULTS" 2>&1
    say "the build log is the row; stopping here"
    exit 1
fi

say "unit suite"
if g 'cd ~/jichi && gmake test 2>&1 | tail -3' 2>/dev/null | tee -a "$RESULTS" | grep -qE '[0-9]+ checks, 0 failures'; then
    ok "unit suite: 0 failures"
else
    bad "unit suite did not report '<N> checks, 0 failures'"
fi

say "smoke tier (the uncertain half: it shells out constantly)"
# M465: keep the FAILURES, not the last five lines. `... | tail -5` recorded the
# tail of whichever driver ran last, which for a 28-check driver is four passing
# checks and `gmake: *** Error 1` -- so the row said "did not print its OK marker"
# and never said WHICH check failed. That is why this stop survived as undiagnosable
# through two sessions: the diagnosis was discarded by the capture, not missing from
# the run. Log in the guest, then pull back the failing lines and the summary.
# JC_SMOKE_KEEP_GOING=1 (M466): report EVERY failing driver in this one boot.
# The tier is fail-fast by default, which is right where a fix loop is seconds
# long and wrong here -- a remote row costs a ten-minute install, and fail-fast
# turns an N-defect platform into N boots. It also hid this row's real stop
# behind an unrelated lint that happened to run earlier in the list.
if g "cd ~/jichi && JC_SMOKE_TIMEOUT_MULT=$_mult JC_SMOKE_KEEP_GOING=1 gmake smoke >/tmp/smoke.log 2>&1; echo smoke_rc=\$?" \
        2>/dev/null | tee -a "$RESULTS" | grep -q 'smoke_rc=0'; then
    ok "smoke tier: OK"
    g 'grep -E "^smoke: OK" /tmp/smoke.log' 2>/dev/null | tee -a "$RESULTS" >/dev/null
else
    bad "smoke tier did not pass -- the failing checks follow"
    {
        # NOT anchored: run.sh indents a nested driver's checks as "    | ok 3 - ...",
        # so `^not ok` matches nothing and the capture stays silent about the very
        # thing it exists to record. (Made that mistake here once already.)
        echo "--- every failing check, and the driver banners ---"
        g 'grep -nE "not ok|FAILED \(in suite\)|ALSO fails|classify the failure" /tmp/smoke.log | head -40' 2>/dev/null
        echo "--- the standalone re-run, which is the classified diagnosis ---"
        g 'sed -n "/ALSO fails standalone/,\$p" /tmp/smoke.log | head -45' 2>/dev/null
    } | tee -a "$RESULTS"
fi

say "offline surfaces"
for s in "--version" "doctor" "describe" "context"; do
    if g "cd ~/jichi && ./jichi $s >/dev/null 2>&1 && echo SURFACE_OK" 2>/dev/null | grep -q SURFACE_OK; then
        ok "offline: $s ran"
    else
        bad "offline: $s failed"
    fi
done

note ""
note "== totals: $N_OK ok, $N_FAIL failed"
say "totals: $N_OK ok, $N_FAIL failed -- $RESULTS"
[ "$N_FAIL" -eq 0 ]
