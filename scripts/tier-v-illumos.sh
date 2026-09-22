#!/bin/sh
# tier-v-illumos.sh -- the illumos row: a non-Linux, non-BSD kernel.
#
# WHY THIS EXISTS, AND WHY IT WAS NOT WRITTEN EARLIER. `docs/DEFERRED.md` carried
# the Guix rig's rule for four months: "scripts/tier-v-guix.sh is deliberately NOT
# written blind -- an untested rig for a platform nobody can run here would be a
# fourth never-executed artifact." That rule is why this script exists only now:
# every step below was performed by hand first, on 2026-09-18 (M658), and this is
# the transcript of a row that ran, not a guess at one.
#
# WHAT IT UNIQUELY EXERCISES, none of which any Linux or BSD row can:
#   * a SysV-derived kernel and libc, and its headers under -std=c89 -pedantic;
#   * procfs PRESENT BUT DIFFERENT -- the hazard PLATFORMS.md named from the
#     source at M469 and M647 measured here: /proc/self/stat and /proc/self/exe
#     are absent, and /proc/self/status is a BINARY pstatus_t, so `fopen`
#     SUCCEEDS where a reader might hope it failed;
#   * ksh93 as /bin/sh -- the axis only OpenBSD otherwise covers, and the one
#     that produced M467's harness defect;
#   * the legacy Solaris grep/sed/awk, which is what killed `search_code` here
#     (M658: illumos grep accepts -r and rejects -I).
#
# WHAT IT NEEDS THAT THE BSD RIG DOES NOT:
#   * `CC=gcc` on every remote command. There is no `cc` and no `c99`, so the
#     Makefile's `CC ?= cc` default makes every capability probe report absent --
#     "no vsnprintf, no curl" -- which is a far more misleading row than "no
#     compiler" (M458, measured again here).
#   * `gmake`. illumos HAS a `make`, and it is Sun make, not GNU. That is worse
#     than FreeBSD's case, where the wrong make is at least differently named.
#   * TMPDIR=/var/tmp, on the same reasoning as every other row.
#
# Exit codes (the tier-v contract):
#   0  the row ran and every check passed
#   1  the row ran and something failed -- a RESULT, read results-illumos.txt
#   2  usage / missing tool on the host
#   3  never reached userspace (no ssh) -- NOT a result, the rig failed
#
# Usage (--ref-secs is REQUIRED: THIS bench's serial `make WERROR=1` seconds):
#   scripts/tier-v-illumos.sh --ref-secs 6.19
#   scripts/tier-v-illumos.sh --dry-run
#   scripts/tier-v-illumos.sh --keep          # leave the VM up for poking
#   scripts/tier-v-illumos.sh --dirty         # ship the WORKING tree, not HEAD
set -u

. "$(dirname "$0")/_rig_mult.sh"
. "$(dirname "$0")/_rig_ship.sh"
. "$(dirname "$0")/_rig_live.sh"

REL="r151058"
REF_SECS="${JC_REF_SECS:-}"
DRY=0; KEEP=0; DIRTY=0; CONSOLE=0
MEM="${JC_ILLUMOS_MEM:-4096}"; SMP="${JC_ILLUMOS_SMP:-4}"
PORT="${JC_ILLUMOS_PORT:-2299}"
LIVE_PORT=""
LIVE_MODEL="local"
# REPO is derived from THIS SCRIPT'S location, which means the rig must be run
# from the repository -- `sh /elsewhere/tier-v-illumos.sh` ships an empty
# archive and fails with "tree did not ship" (measured, M660, while trying to
# run a frozen copy so that editing the tree could not shift the interpreter
# under a live script). Both hazards are real; the resolution is the discipline
# that already applies to `make ci`: run it from the tree and do not edit the
# tree while it runs. A JICHI_REPO knob was considered and rejected -- it would
# exist only to make that discipline optional.
REPO=$(cd "$(dirname "$0")/.." && pwd)
DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}/illumos"

while [ $# -gt 0 ]; do
    case "$1" in
        --ref-secs) REF_SECS="$2"; shift ;;
        --live-port)  LIVE_PORT="$2";  shift ;;
        --live-model) LIVE_MODEL="$2"; shift ;;
        --release)  REL="$2"; shift ;;
        --dry-run)  DRY=1 ;;
        --keep)     KEEP=1 ;;
        --dirty)    DIRTY=1 ;;
        --console)  CONSOLE=1 ;;
        -h|--help)  awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        *) echo "tier-v-illumos: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

URL="https://downloads.omnios.org/media/stable/omnios-$REL.cloud.qcow2"
IMG="$DIR/omnios-$REL.cloud.qcow2"
WORK="$DIR/omni-work.qcow2"
SEED="$DIR/omni-seed.iso"
KEY="$DIR/tier-v-key"
RESULTS="$DIR/results-illumos.txt"
SSH_OPTS="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8"

mkdir -p "$DIR"
N_OK=0; N_FAIL=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));     echo "ok - $*";     echo "ok   - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL - $*" >> "$RESULTS"; }
note() { echo "$*" >> "$RESULTS"; }

# shellcheck disable=SC2086
# ONE RETRY ON A TRANSPORT ERROR, and it is not belt-and-braces (M660). ssh
# exits 255 for "I could not talk to the host" -- which cloud-init produces on
# this image by bouncing sshd shortly after it first answers, so a command that
# ran fine seconds earlier comes back `kex_exchange_identification: read:
# Connection reset by peer`. The second run of this rig lost TWO checks that way
# and reported them as findings about illumos: "the procfs shape is not what
# PLATFORMS.md predicts". It was a dropped connection, and a rig that reports a
# transport failure as a platform verdict is the exact instrument defect this
# tier exists to avoid (M470: "a row that blames the program for its emulator's
# gap is worse than no row"). 255 only: a real non-zero from the remote command
# is an answer and must pass through untouched.
g() {
    _g_out=$(ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"); _g_rc=$?
    if [ "$_g_rc" -eq 255 ]; then
        sleep 5
        _g_out=$(ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"); _g_rc=$?
    fi
    printf '%s\n' "$_g_out"
    return $_g_rc
}

# gl -- g's tunnelling twin, for the live step only. Deliberately WITHOUT g's
# 255-retry: a retried model call is a second turn against a fresh nonce, and
# the honest report of a transport failure there is a failure, not a do-over.
# shellcheck disable=SC2086
# -o ExitOnForwardFailure=yes IS THE POINT, and it was missing (M677). `ssh -R`
# whose forward cannot bind prints "Warning: remote port forwarding failed for
# listen port N" and RUNS THE COMMAND ANYWAY, exit 0. Measured on the Pi 400
# this session: the rig's own forward failed, the turn answered regardless --
# through a STALE forward left bound by an ssh session more than a day old --
# and the row reported two green live turns. The model calls were real; the
# transport under test was never exercised, and on a machine without that
# leftover the same rig would have failed. A pass that depends on something
# nobody knew was there is the shape of evidence this project refuses.
gl() {
    ssh $SSH_OPTS -o ExitOnForwardFailure=yes -R "$LIVE_PORT:127.0.0.1:$LIVE_PORT" -i "$KEY" -p "$PORT" \
        tierv@127.0.0.1 "$@"
}

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-illumos: DRY RUN"
    echo "  release : OmniOS CE $REL"
    echo "  image   : $URL"
    echo "  cache   : $DIR"
    echo "  vm      : -m $MEM -smp $SMP, ssh on 127.0.0.1:$PORT"
    echo "  results : $RESULTS"
    echo "  steps   : fetch -> seed -> boot -> pkg install -> ship -> gmake -> gate"
    echo "  NOTE    : CC=gcc on every command (no cc, no c99); gmake, not make."
    exit 0
fi

# The one guard every rig taking a reference must run, before any work.
jc_rig_ref_or_die "tier-v-illumos" "$REF_SECS" || exit 2

: > "$RESULTS"
note "# tier-v-illumos row: OmniOS CE $REL amd64"
note "# date        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# host        : $(uname -srm)"
note ""

# ------------------------------------------------------------ 1. stage the image
say "stage omnios-$REL.cloud.qcow2 (~1.1 GB)"
if [ ! -f "$IMG" ]; then
    curl -fL --no-progress-meter --retry 3 -o "$IMG.part" "$URL" || {
        echo "tier-v-illumos: download failed" >&2; exit 2; }
    mv "$IMG.part" "$IMG"
fi
# The published sha256 is a bare digest, not a `sha256sum -c` line.
if curl -fsL "$URL.sha256" -o "$DIR/img.sha256" 2>/dev/null; then
    want=$(tr -d ' \t\n\r' < "$DIR/img.sha256")
    have=$(sha256sum "$IMG" | cut -d' ' -f1)
    if [ "$want" = "$have" ]; then ok "image staged and checksum matches"
    else bad "image checksum MISMATCH (want $want, have $have)"; exit 2; fi
else
    ok "image staged (checksum not fetched)"
fi

rm -f "$WORK"
qemu-img create -q -f qcow2 -F qcow2 -b "$IMG" "$WORK" 20G
ok "overlay created (the base image stays pristine)"

# --------------------------------------------------------------- 2. seed the VM
[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -C tier-v-illumos -f "$KEY"
_sd="$DIR/seed"; rm -rf "$_sd"; mkdir -p "$_sd"
{
    echo "#cloud-config"
    # NOT `packages:`. OmniOS uses IPS, and cloud-init's package handling is not
    # the reliable half of its support here; the toolchain goes on over ssh in
    # step 4, where a failure is visible and its message is kept. sudo IS present
    # in the cloud image, and cloud-init's sudoers entry works -- `pfexec` does
    # not, because the account has no RBAC profile.
    echo "users:"
    echo "  - name: tierv"
    echo "    shell: /bin/bash"
    echo "    lock_passwd: true"
    echo "    sudo: 'ALL=(ALL) NOPASSWD:ALL'"
    echo "    ssh_authorized_keys:"
    echo "      - $(cat "$KEY.pub")"
    echo "ssh_pwauth: false"
} > "$_sd/user-data"
{ echo "instance-id: tier-v-illumos"; echo "local-hostname: tier-v-illumos"; } > "$_sd/meta-data"
xorriso -as mkisofs -volid cidata -joliet -rock -output "$SEED" \
        "$_sd/user-data" "$_sd/meta-data" >/dev/null 2>&1
ok "cloud-init seed built"

# ----------------------------------------------------------------- 3. boot
if [ -f "$DIR/omni.pid" ] && kill -0 "$(cat "$DIR/omni.pid" 2>/dev/null)" 2>/dev/null; then
    echo "tier-v-illumos: a VM from a previous --keep run is still up (pid $(cat "$DIR/omni.pid"))." >&2
    echo "  stop it first:  kill \$(cat $DIR/omni.pid)" >&2
    exit 2
fi

if [ "$CONSOLE" -eq 1 ]; then
    exec qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
        -drive file="$WORK",if=virtio,format=qcow2 \
        -drive file="$SEED",if=virtio,format=raw,readonly=on \
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
        -device virtio-net,netdev=n0 -nographic
fi

say "boot (kvm, ${MEM}M, ${SMP} cpu)"
if ! qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
    -drive file="$WORK",if=virtio,format=qcow2 \
    -drive file="$SEED",if=virtio,format=raw,readonly=on \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
    -device virtio-net,netdev=n0 -display none \
    -serial file:"$DIR/omni-console.log" \
    -daemonize -pidfile "$DIR/omni.pid" 2>"$DIR/qemu-err.log"; then
    echo "tier-v-illumos: qemu failed to start:" >&2
    sed 's/^/  /' "$DIR/qemu-err.log" >&2
    exit 2
fi
VMPID=$(cat "$DIR/omni.pid" 2>/dev/null)
stop_vm() {
    [ "$KEEP" -eq 1 ] && { echo "tier-v-illumos: VM left running (pid $VMPID, ssh -i $KEY -p $PORT tierv@127.0.0.1)"; return; }
    [ -n "${VMPID:-}" ] && kill "$VMPID" 2>/dev/null
}
trap stop_vm EXIT INT TERM

say "wait for sshd (~30 s measured; the budget is generous)"
# TWO CONSECUTIVE successes, not one. sshd on this image answers and is then
# restarted by cloud-init, so a single probe proves the port was open once --
# which is not the same as settled, and the difference cost two checks on the
# second run of this rig.
_up=0; _hits=0
for _i in $(seq 1 60); do
    if g true >/dev/null 2>&1; then
        _hits=$((_hits+1))
        [ "$_hits" -ge 2 ] && { _up=1; break; }
    else
        _hits=0
    fi
    sleep 5
done
if [ "$_up" -eq 0 ]; then
    bad "never reached userspace -- no ssh after ~5 min"
    note ""; note "last 20 console lines:"
    tail -20 "$DIR/omni-console.log" 2>/dev/null | sed 's/^/    /' >> "$RESULTS"
    exit 3
fi
ok "ssh reachable: illumos guest is up"

# -------------------------------------------------------------- 4. identity
note ""; note "## identity"
g 'uname -a; head -2 /etc/release; echo "sh -> $(ls -l /bin/sh)"' >> "$RESULTS" 2>&1
note ""

# The load-bearing question this row exists for: procfs PRESENT BUT DIFFERENT.
note "## procfs shape (the M469 hazard, measured)"
g 'for f in /proc/self/stat /proc/self/exe /proc/self/status /proc/self/path/a.out; do
       if [ -e "$f" ] || [ -L "$f" ]; then echo "PRESENT $f"; else echo "ABSENT  $f"; fi
   done
   echo "VmRSS occurrences in /proc/self/status: $(grep -c VmRSS /proc/self/status 2>/dev/null || echo 0)"' \
    >> "$RESULTS" 2>&1
if g 'test -e /proc/self/status && ! grep -q VmRSS /proc/self/status' 2>/dev/null; then
    ok "/proc/self/status EXISTS and holds no VmRSS -- fopen succeeds, the parse finds nothing (no data, not wrong data)"
else
    bad "the procfs shape is not what PLATFORMS.md predicts -- read the block above"
fi

# ------------------------------------------------------------ 5. provisioning
say "install the toolchain (IPS; gcc14 + GNU make)"
# git is in the list for COMPARABILITY, not for the build. The sibling device
# rig learned this the hard way: without a git repository the shipped tree runs
# four checks fewer than a host run, and "an unexplained delta in a comparison
# table is the thing this whole rig exists to prevent". On the first illumos run
# its absence also gave doctor a real problem to report (`snapshots enabled but
# git is not on PATH`), which is correct of doctor and noise in a platform row.
# A NETWORK INSTALL WITH NO DEADLINE CAN HANG THIS RIG FOREVER (M685).
# Measured on the OpenBSD row the same day: `ftp` sat on one package for
# 22m47s at 0.0%% CPU with the connection ESTABLISHED and both queues at
# zero, and the rig printed nothing between `== pkg_add` and never. Bounded
# here for the same reason; `timeout` is probed rather than assumed, so a
# system without it runs unbounded and is no worse off than before.
if g 'T=""; command -v timeout >/dev/null 2>&1 && T="timeout 900"; \
      sudo $T pkg install -q developer/gcc14 developer/build/gnu-make developer/versioning/git >/dev/null 2>&1; \
      command -v gcc >/dev/null && command -v gmake >/dev/null' 2>/dev/null; then
    # Single quotes around the word: a backtick inside a DOUBLE-quoted shell
    # string is a command substitution, and the first version of this line ran
    # `make` on the host and pasted its output into the check. Harmless, and the
    # third quoting mistake in a rig in one night (M658's two were $HOME).
    ok 'toolchain present (gcc + gmake; illumos has a make, and it is Sun make)'
    g 'gcc --version | head -1; gmake --version | head -1' >> "$RESULTS" 2>&1
else
    bad "pkg install did not produce gcc and gmake"
    g 'sudo pkg install developer/gcc14 developer/build/gnu-make 2>&1 | tail -20' >> "$RESULTS" 2>&1
    exit 1
fi

# ---------------------------------------------------------------- 6. ship
say "$(jc_rig_ship_label "$DIRTY")"
g 'rm -rf ~/jichi && mkdir -p ~/jichi' 2>/dev/null
jc_rig_ship_stamp "$REPO" "$DIRTY" >> "$RESULTS"
jc_rig_ship_tar "$REPO" "$DIRTY" | g 'cd ~/jichi && tar xf -' 2>/dev/null
# Same reason as the device rig: a real repository at a known state, so the
# git-tool checks run and the row compares to a host run.
g 'cd ~/jichi && git init -q . >/dev/null 2>&1 && \
   git -c user.email=bench@invalid -c user.name=bench add -A >/dev/null 2>&1 && \
   git -c user.email=bench@invalid -c user.name=bench commit -qm shipped >/dev/null 2>&1' \
   2>/dev/null && ok "shipped tree made a git repo (so the git-tool checks run)"
if g 'test -f ~/jichi/Makefile' 2>/dev/null; then
    [ "$DIRTY" = 1 ] && ok "working tree shipped -- row is NOT reproducible from a commit" \
                     || ok "tree shipped"
else
    bad "tree did not ship"; exit 1
fi

# ------------------------------------------------------------ 7. the row
say "gmake info (which probes fired on a SysV libc)"
note ""; note "## gmake info"
g 'cd ~/jichi && CC=gcc gmake info 2>&1' >> "$RESULTS" 2>&1
# The two facts this platform needs, and the probes that find them.
if g 'cd ~/jichi && CC=gcc gmake info 2>&1 | grep -q "needs -D__EXTENSIONS__"' 2>/dev/null; then
    ok "the winsize probe reports __EXTENSIONS__ is required (as measured at M658)"
else
    note "    NOTE: the winsize probe did not report __EXTENSIONS__ here"
fi
if g 'cd ~/jichi && CC=gcc gmake info 2>&1 | grep -q "SOCKET_LIBS    = -lsocket"' 2>/dev/null; then
    ok "the socket probe reports -lsocket -lnsl (sockets are not in this libc)"
else
    note "    NOTE: the socket probe did not report -lsocket here"
fi

say "build (WERROR=1)"
_t0=$(date +%s)
if g 'cd ~/jichi && LC_ALL=C CC=gcc gmake WERROR=1 >/tmp/build.log 2>&1 && echo BUILD_OK' 2>/dev/null | grep -q BUILD_OK; then
    _t1=$(date +%s); _secs=$((_t1-_t0))
    ok "WERROR=1 build clean on illumos (${_secs}s)"
    if _mult=$(jc_rig_mult "$_secs" "$REF_SECS"); then
        note "    JC_SMOKE_TIMEOUT_MULT = ceil(${_secs}s / ${REF_SECS}s) = $_mult"
    else
        bad "multiplier NOT derivable from device='${_secs}' ref='${REF_SECS}' -- no row"
        exit 1
    fi
else
    bad "build failed -- first diagnostics follow"
    g 'grep -E "error|warning" /tmp/build.log | head -20' >> "$RESULTS" 2>&1
    exit 1
fi

say "unit suite"
if g 'cd ~/jichi && LC_ALL=C CC=gcc gmake test 2>&1 | tail -3' 2>/dev/null \
        | tee -a "$RESULTS" | grep -qE '[0-9]+ checks, 0 failures'; then
    ok "unit suite: 0 failures"
else
    bad "unit suite did not report '<N> checks, 0 failures'"
fi

say "smoke tier (KEEP_GOING: a remote row costs a boot, so report every failure)"
if g "cd ~/jichi && LC_ALL=C CC=gcc TMPDIR=/var/tmp JC_SMOKE_TIMEOUT_MULT=$_mult \
      JC_SMOKE_KEEP_GOING=1 gmake smoke >/tmp/smoke.log 2>&1; echo smoke_rc=\$?" \
        2>/dev/null | tee -a "$RESULTS" | grep -q 'smoke_rc=0'; then
    # THE DENOMINATOR ON THE SUCCESS PATH TOO. M665's finding was that a rig
    # which fails must still report its driver count, because the count is free
    # at the time and unrecoverable afterwards. The fix went into the FAILURE
    # branch only, and the inverse gap survived: a row that PASSES came back
    # "smoke tier: OK" with no numbers at all, so the one thing a green row is
    # cited for -- how many drivers it ran, which is the coverage-debt
    # denominator in docs/PLATFORM_RETEST.md -- had to be read out of a guest
    # that no longer exists. Measured 2026-09-22: the first fully green illumos
    # run in the project's history reported no count.
    _line=$(g 'grep -E "^smoke: OK" /tmp/smoke.log | tail -1' 2>/dev/null)
    ok "smoke tier: OK -- ${_line:-count not extracted}"
else
    # NOT a bare failure: at M658 this row was 238 of 300, which is a RESULT.
    _line=$(g 'grep -E "^smoke: \(|^smoke: OK" /tmp/smoke.log | tail -1' 2>/dev/null)
    bad "smoke tier did not pass -- $_line"
    note ""
    note "--- failing drivers ---"
    g 'grep "^smoke: FAILED -- " /tmp/smoke.log | tail -1' >> "$RESULTS" 2>&1
    # AND THE CHECKS, not just the driver names. M665 made this rig report its
    # DENOMINATOR on failure, for the reason that a row which failed is the one
    # whose count a reader wants. The same argument applies one level down and
    # was not made: on 2026-09-22 this row came back "2 driver(s): snapshot_lint
    # doctor" and nothing else, so diagnosing it needed a SECOND BOOT of a guest
    # that had just been torn down -- and the guest's /tmp/smoke.log went with
    # it. The failing check text is free at this moment and unrecoverable after.
    # Bounded with head: a tier that fails wholesale must not paste itself into
    # the results file.
    note ""
    note "--- failing checks (free now, gone with the guest) ---"
    g 'grep -E "not ok|KILLED at its" /tmp/smoke.log | head -40' >> "$RESULTS" 2>&1
fi

say "offline surfaces"
# DOCTOR IS READ BY ITS SUMMARY LINE, NOT BY ITS EXIT CODE (M660), and the rule
# was already written down in the sibling rig's header: "Verdicts are read from
# POSITIVE MARKERS in the output, never from an exit code (docs/BUILD.md, M368:
# a `grep -c '^not ok'` pipe read green over a red driver for thirteen
# milestones)." This rig's first version broke it, and the first run of this rig
# duly reported "offline: doctor failed" on illumos. doctor had not failed: it
# exits non-zero when it FINDS something, and on a guest without git it correctly
# reports `snapshots enabled but git is not on PATH`. An exit code that means
# "I did my job and found a problem" cannot be read as "the surface is broken".
# The other three are pure introspection with no such semantics, so an exit code
# is the right question for them.
for s in "--version" "describe" "context"; do
    if g "cd ~/jichi && ./jichi $s >/dev/null 2>&1 && echo SURFACE_OK" 2>/dev/null | grep -q SURFACE_OK; then
        ok "offline: $s ran"
    else
        bad "offline: $s failed"
    fi
done
_doc=$(g 'cd ~/jichi && ./jichi doctor 2>&1' 2>/dev/null)
_sum=$(printf '%s' "$_doc" | grep -E '[0-9]+ ok, [0-9]+ warning' | tail -1)
if [ -n "$_sum" ]; then
    ok "offline: doctor ran and printed its summary -- $_sum"
    note "    doctor: $_sum"
    # The findings themselves are a RECORD of the platform, not a gate on it.
    printf '%s' "$_doc" | grep -E '^[!x]|not recognised' | sed 's/^/    /' >> "$RESULTS"
else
    bad "offline: doctor printed no summary line at all"
fi

# ------------------------------------------------------------- live turns
# The DRIVEN step. Its definition, and every reason behind it, is in ONE place:
# scripts/_rig_live.sh. Until M677 this row was driven BY HAND (M663, a text
# turn only, no tool call), which is why PLATFORMS.md could say "Driven (text
# turn only)" and nothing more -- a hand-run turn is not a row anyone else can
# reproduce, and the tool half is the half the verdict is named for.
note ""
say "live turns"
if [ -z "${LIVE_PORT:-}" ]; then
    jc_rig_live_skip
else
    jc_rig_live illumos "$LIVE_PORT" "$LIVE_MODEL" "$DIR"
fi

note ""
note "== totals: $N_OK ok, $N_FAIL failed"
say "totals: $N_OK ok, $N_FAIL failed -- $RESULTS"
[ "$N_FAIL" -eq 0 ]
