#!/bin/sh
# tier-v-vm.sh - run one Tier V *whole-VM* row (v2e..v2k, see the table below) from
# docs/plans/2026-07-hardware-testing.md, unattended, and write a results file.
#
#   V2e   Debian 12, measured at -m 256 -smp 1   (a whole machine with 256 MB)
#   V2f   Debian 9,  measured at -m 512          (old kernel + old glibc)
#   V2g   Debian 12, measured at -m 128          ) the sub-256 MB ladder: same
#   V2h   Debian 12, measured at -m  64          ) image and runbook as V2e,
#   V2i   Debian 12, measured at -m  32          ) only the ceiling changes
#   V2j   Debian 12, measured at -m 192          ) added to bracket the image's
#   V2k   Debian 12, measured at -m 160          ) own boot floor (V2g panics)
#
# The other Tier V rows need only zig, qemu-user or a container; these two need
# a real guest kernel, which is why they were the last two open. Both run the
# SAME per-run runbook as every other row -- that identity is the whole point:
# numbers from a container, a VM and a Pi only compare if the procedure was.
#
# Usage:
#   scripts/tier-v-vm.sh v2e                  # stage, provision, build, gate
#   scripts/tier-v-vm.sh v2f
#   scripts/tier-v-vm.sh v2e --dry-run        # print every step, touch nothing
#   scripts/tier-v-vm.sh v2e --host-secs 74   # skip the host build baseline
#   scripts/tier-v-vm.sh v2e --console        # boot to an interactive serial
#                                             #   console instead (debugging)
#   scripts/tier-v-vm.sh v2g                  # 128 MB; then v2h (64), v2i (32).
#                                             #   Reuses V2e's staged base image.
#   TIER_V_DIR=/scratch/tierv scripts/tier-v-vm.sh v2f
#
# Env: TIER_V_DIR (default ~/.cache/jichi-tier-v), TIER_V_SSH_PORT (2222),
#      TIER_V_PROVISION_MEM (1024), TIER_V_WORK_GB (12).
#
# Exit: 0 the row ran (read $RESULTS for the gate verdict)
#       1 a portability failure -- the build failed even at the provisioning
#         ceiling, so it is not a memory finding
#       2 usage
#       3 the guest never reached USERSPACE at the row's ceiling: the image's own
#         boot floor is above it. Recorded as a FINDING in $RESULTS. Distinct
#         from 1 because nothing about jichi was measured.
#
# Wants: qemu-system-x86_64, qemu-img, curl, ssh/scp/ssh-keygen, xorriso.
# Does NOT want: libvirt, root on the host, or a network beyond the guest's
# NAT. KVM is used when /dev/kvm exists and the row records which accelerator
# ran -- under TCG the wall-clock half of a row is an emulation artefact, not a
# finding (see the plan's "What is a finding" table), so a row that does not
# say which it was cannot be read correctly.
set -eu

ROW=""
DRY=0
CONSOLE=0
HOST_SECS=""

DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}"
PORT="${TIER_V_SSH_PORT:-2222}"
PROV_MEM="${TIER_V_PROVISION_MEM:-1024}"
WORK_GB="${TIER_V_WORK_GB:-12}"

REPO=$(cd "$(dirname "$0")/.." && pwd)

for arg in "$@"; do
    case "$arg" in
        v2e|v2f|v2g|v2h|v2i|v2j|v2k) ROW="$arg" ;;
        --dry-run)   DRY=1 ;;
        --console)   CONSOLE=1 ;;
        --host-secs) HOST_SECS="PENDING" ;;
        # Print the header comment block, not a hardcoded line range: the range
        # was '2,32p' and silently lost its last line the first time a row was
        # added to the list above. awk stops at the first non-comment line.
        -h|--help)   awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        *)
            if [ "$HOST_SECS" = "PENDING" ]; then HOST_SECS="$arg"; continue; fi
            echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done
[ -n "$ROW" ] || { echo "tier-v-vm: name a row: v2e v2f v2g v2h v2i v2j v2k (--help)" >&2; exit 2; }
[ "$HOST_SECS" = "PENDING" ] && { echo "tier-v-vm: --host-secs needs a number" >&2; exit 2; }
# WHOLE seconds only, and say so here rather than 500 lines later. This rig times
# with `date +%s` and derives the multiplier in shell arithmetic, so a decimal dies
# as "Illegal number: 4.38" at the division, with nothing to connect it to the flag
# that caused it. The BSD rigs DO take a decimal --ref-secs (they divide in awk), so
# the difference is worth naming instead of leaving as a surprise.
case "${HOST_SECS:-}" in
    '') ;;                 # unset: the rig measures its own baseline
    *[!0-9]*) echo "tier-v-vm: --host-secs must be whole seconds (got '$HOST_SECS'); this" >&2
       echo "  rig divides in shell arithmetic. The BSD rigs' --ref-secs takes a" >&2
       echo "  decimal because they divide in awk." >&2
       exit 2 ;;
esac

# Per-row facts. Both images carry cloud-init, so ONE mechanism seeds both: a
# NoCloud CIDATA ISO built with xorriso. (Debian's `nocloud` flavour, which the
# plan first named, ships WITHOUT cloud-init -- empty root password and serial
# autologin -- so it would have to be driven by screen-scraping a console.
# `genericcloud` + a seed keeps V2e and V2f on the identical path.)
case "$ROW" in
    v2e)
        LABEL="V2e -- Debian 12 (bookworm), 256 MB, 1 core"
        URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
        MEM=256
        SMP=1
        ;;
    v2f)
        LABEL="V2f -- Debian 9 (stretch), 512 MB, kernel 4.9"
        URL="https://cloud.debian.org/images/cloud/OpenStack/archive/9.13.9-20201210/debian-9.13.9-20201210-openstack-amd64.qcow2"
        MEM=512
        SMP=1
        ;;
    # The sub-256 MB ladder. SAME image, seed, runbook and gate as V2e -- only
    # the measured ceiling changes -- because docs/LOW_MEMORY.md graded its
    # ~128 MB and <=64 MB tiers B ("a cgroup ceiling on a big box") on the
    # explicit ground that no machine that size had ever run jichi, and a cgroup
    # flatters the number three ways (warm host page cache, the kernel's
    # footprint outside the ceiling, no competing pressure). These rows are the
    # whole-machine answer. They are separate rows rather than a --mem flag on
    # V2e for the reason this script exists: numbers only compare if the
    # procedure did, and a named row keeps its own results file.
    #
    # Expect the bottom of the ladder to FAIL, and read that as the finding:
    # the largest ceiling at which a stock Debian cloud image still reaches
    # userspace is itself the datum those tiers were missing. The build-did-not-
    # survive path below records it and still runs the gate at the ceiling.
    v2g)
        LABEL="V2g -- Debian 12 (bookworm), 128 MB, 1 core"
        URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
        BASE_ROW=v2e
        MEM=128
        SMP=1
        ;;
    v2h)
        LABEL="V2h -- Debian 12 (bookworm), 64 MB, 1 core"
        URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
        BASE_ROW=v2e
        MEM=64
        SMP=1
        ;;
    v2i)
        LABEL="V2i -- Debian 12 (bookworm), 32 MB, 1 core"
        URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
        BASE_ROW=v2e
        MEM=32
        SMP=1
        ;;
    # Added after V2g panicked: 128 does not boot and 256 (V2e) does, so the
    # stock image's own boot floor is somewhere between, and THAT number is what
    # the ~128 MB "Tight" tier actually needs. The letters are allocation order,
    # not memory order.
    v2j)
        LABEL="V2j -- Debian 12 (bookworm), 192 MB, 1 core"
        URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
        BASE_ROW=v2e
        MEM=192
        SMP=1
        ;;
    v2k)
        LABEL="V2k -- Debian 12 (bookworm), 160 MB, 1 core"
        URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"
        BASE_ROW=v2e
        MEM=160
        SMP=1
        ;;
esac

# The base image is shared by row where the URL is identical (the sub-256 MB
# ladder rides V2e's). Sharing the *same bytes* is what comparability wants; it
# is not a procedural shortcut, and a row with no BASE_ROW keeps its own.
BASE="$DIR/base-${BASE_ROW:-$ROW}.qcow2"
DISK="$DIR/$ROW.qcow2"
WORK="$DIR/$ROW-work.qcow2"
SEED="$DIR/$ROW-seed.iso"
KEY="$DIR/id_ed25519"
CONSOLE_LOG="$DIR/console-$ROW.log"
RESULTS="$DIR/results-$ROW.txt"
PIDFILE="$DIR/$ROW.pid"

if [ -e /dev/kvm ] && [ -r /dev/kvm ]; then
    # TIERV_CPU pins an older guest CPU model (e.g. IvyBridge) for guests whose
    # kernel predates the host silicon: a 4.9 kernel panics in text_poke on a
    # 2026 host under -cpu host (V2f, 2026-08-03). Correctness is unaffected --
    # execution stays KVM-native; only the advertised CPUID features shrink.
    ACCEL="kvm,cpu=${TIERV_CPU:-host}"
    ACCEL_ARGS="-accel kvm -cpu ${TIERV_CPU:-host}"
else
    ACCEL="tcg"
    ACCEL_ARGS="-accel tcg"
fi

run() { echo "+ $*"; [ "$DRY" -eq 1 ] || "$@"; }
say() { echo "== $*"; }
# Append stdin to the results file (a no-op narration under --dry-run).
res() { if [ "$DRY" -eq 1 ]; then sed 's/^/[results] /'; else cat >> "$RESULTS"; fi; }

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o LogLevel=ERROR -o ConnectTimeout=5 -o BatchMode=yes"

# ---------------------------------------------------------------- guest access

g() {  # run a command in the guest as tierv
    # shellcheck disable=SC2086
    ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"
}
# gr wraps in single quotes, so its argument must not contain one.
gr() { g "sudo sh -c '$*'"; }

vm_start() {  # vm_start <mem> <smp>
    _mem=$1; _smp=$2
    # Remembered so vm_wait_ssh can name the ceiling a boot failed at.
    BOOT_MEM=$_mem
    say "boot $ROW at -m $_mem -smp $_smp (accel: $ACCEL)"
    if [ "$DRY" -eq 1 ]; then
        echo "+ qemu-system-x86_64 $ACCEL_ARGS -m $_mem -smp $_smp ..."
        return 0
    fi
    # Truncate the console log OURSELVES rather than relying on qemu's `file:`
    # doing it: vm_wait_ssh greps this log for a panic, and a panic left over
    # from the previous boot would be read as this boot's verdict. qemu does
    # truncate on open, but not before we start grepping.
    : > "$CONSOLE_LOG"
    # shellcheck disable=SC2086
    qemu-system-x86_64 $ACCEL_ARGS -m "$_mem" -smp "$_smp" \
        -drive file="$DISK",if=virtio,format=qcow2 \
        -drive file="$WORK",if=virtio,format=qcow2 \
        -drive file="$SEED",if=virtio,format=raw,readonly=on \
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
        -device virtio-net-pci,netdev=n0 \
        -display none -monitor none \
        -serial "file:$CONSOLE_LOG" &
    echo $! > "$PIDFILE"
}

vm_wait_ssh() {  # generous: a 256 MB guest under TCG boots slowly
    [ "$DRY" -eq 1 ] && { echo "+ wait for ssh on 127.0.0.1:$PORT"; return 0; }
    _i=0
    until g true 2>/dev/null; do
        # A CEILING ROW CAN FAIL BEFORE USERSPACE, and that is a different
        # finding from a slow boot or a failed build. Measured 2026-08-13: the
        # Debian 12 cloud image at -m 128 reserves 59,592K of its 130,516K and
        # panics 0.91s in, unpacking its own initrd -- "System is deadlocked on
        # memory", inside do_populate_rootfs. Without this branch the run sat in
        # this loop for the full 600s and then reported a TIMEOUT, which reads
        # as "the guest was slow" when the truth is "the image cannot boot that
        # small". Ten wasted minutes per rung, and the wrong diagnosis recorded.
        if grep -q 'Kernel panic' "$CONSOLE_LOG" 2>/dev/null; then
            _why=$(grep -m1 'Kernel panic' "$CONSOLE_LOG" \
                   | sed 's/.*Kernel panic[^:]*: //')
            _saw=$(grep -m1 'Memory: ' "$CONSOLE_LOG" | sed 's/.*Memory: //')
            {
                echo "FINDING: the guest never reached userspace at -m ${BOOT_MEM:-?}."
                echo "  kernel panic: $_why"
                [ -n "$_saw" ] && echo "  kernel saw:   $_saw"
                echo "  No build and no gate ran, so this rung measures the IMAGE,"
                echo "  not jichi: a stock cloud kernel + initrd needs more than"
                echo "  this ceiling before any userspace exists. For tiers below"
                echo "  the image's own floor, use a minimal guest instead."
                echo "  Console: $CONSOLE_LOG"
                echo
            } | res
            echo "tier-v-vm: guest panicked at -m ${BOOT_MEM:-?} -- $_why" >&2
            echo "  recorded in $RESULTS; this is a boot floor, not a jichi result" >&2
            vm_stop_hard
            exit 3
        fi
        _i=$((_i + 1))
        # THE SECOND WAY A CEILING ROW DIES, and it leaves no panic to grep.
        # Measured 2026-08-13: at -m 64 and -m 32 the Debian 12 image never
        # starts the kernel at all -- it sits in the GRUB menu forever, because
        # the bootloader cannot load a 8 MB kernel plus a 32 MB initrd into that
        # much RAM. The console log holds GRUB's menu and no "Linux version"
        # line, so the panic branch above never fires and the run burned the full
        # 600s. A KVM guest that boots at all prints "Linux version" within a
        # second or two, so absence of it well past that is conclusive.
        if [ "$_i" -eq 120 ] && ! grep -q 'Linux version' "$CONSOLE_LOG" 2>/dev/null; then
            {
                echo "FINDING: the kernel never STARTED at -m ${BOOT_MEM:-?}."
                echo "  No \"Linux version\" line after 120s. The console holds"
                if grep -q 'GRUB' "$CONSOLE_LOG" 2>/dev/null; then
                    echo "  GRUB's menu and nothing further: the bootloader could"
                    echo "  not load kernel+initrd into this ceiling."
                else
                    echo "  no kernel banner: the guest died before Linux ran."
                fi
                echo "  Below the panic seen at higher ceilings -- an even harder"
                echo "  floor, and again a property of the IMAGE, not of jichi."
                echo "  Console: $CONSOLE_LOG"
                echo
            } | res
            echo "tier-v-vm: kernel never started at -m ${BOOT_MEM:-?} (stalled in the bootloader)" >&2
            echo "  recorded in $RESULTS; this is a boot floor, not a jichi result" >&2
            vm_stop_hard
            exit 3
        fi
        if [ "$_i" -gt 600 ]; then
            echo "tier-v-vm: no ssh after 600s -- see $CONSOLE_LOG" >&2
            vm_stop_hard
            exit 1
        fi
        sleep 1
    done
    say "ssh up after ${_i}s"
}

vm_stop() {
    [ "$DRY" -eq 1 ] && { echo "+ poweroff guest"; return 0; }
    say "poweroff"
    gr poweroff >/dev/null 2>&1 || true
    _i=0
    while [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; do
        _i=$((_i + 1))
        [ "$_i" -gt 120 ] && { vm_stop_hard; break; }
        sleep 1
    done
    rm -f "$PIDFILE"
}

vm_stop_hard() {
    [ -f "$PIDFILE" ] || return 0
    kill "$(cat "$PIDFILE")" 2>/dev/null || true
    sleep 2
    kill -9 "$(cat "$PIDFILE")" 2>/dev/null || true
    rm -f "$PIDFILE"
}

trap 'vm_stop_hard' INT TERM EXIT

# ------------------------------------------------------------------- preflight

say "$LABEL"
echo "   images   : $DIR"
echo "   accel    : $ACCEL$([ "$ACCEL" = tcg ] && echo '  (no /dev/kvm -- timings are emulation artefacts, not findings)')"
echo "   measured : -m $MEM -smp $SMP        provisioned at -m $PROV_MEM"
echo "   results  : $RESULTS"
echo

missing=""
for t in qemu-system-x86_64 qemu-img curl ssh scp ssh-keygen xorriso; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
[ -z "$missing" ] || { echo "tier-v-vm: missing tools:$missing" >&2; exit 1; }

run mkdir -p "$DIR"

# ------------------------------------------------- 1. host build-time baseline
# The multiplier is measured, never guessed: guest build seconds / host build
# seconds, rounded up (M220 precedent -- the passing multiplier IS a
# deliverable). The host baseline is a serial build, because the guest's is.
if [ -z "$HOST_SECS" ]; then
    say "host baseline: make clean && make WERROR=1 (serial, in $REPO)"
    if [ "$DRY" -eq 1 ]; then
        echo "+ time make -C $REPO clean WERROR=1"
        HOST_SECS=60
    else
        _t0=$(date +%s)
        make -C "$REPO" clean >/dev/null 2>&1 || true
        make -C "$REPO" WERROR=1 >/dev/null 2>&1 || {
            echo "tier-v-vm: the HOST build failed -- fix that before a VM row" >&2
            exit 1; }
        HOST_SECS=$(( $(date +%s) - _t0 ))
        [ "$HOST_SECS" -gt 0 ] || HOST_SECS=1
    fi
fi
echo "   host build: ${HOST_SECS}s"

# ---------------------------------------------------------- 2. stage the image

if [ ! -f "$BASE" ]; then
    say "download base image"
    run curl -fL --progress-bar -o "$BASE.part" "$URL"
    run mv "$BASE.part" "$BASE"
else
    say "base image already staged: $BASE"
fi

# A fresh overlay per run, so a row always starts from the pristine base.
run rm -f "$DISK" "$WORK"
run qemu-img create -f qcow2 -b "$BASE" -F qcow2 "$DISK" 16G
run qemu-img create -f qcow2 "$WORK" "${WORK_GB}G"

# The 2 GB root of a cloud image does not hold build-essential + the tree +
# objects, and without cloud-init's growpart the root partition will not grow;
# a second disk mounted at /work sidesteps partition surgery entirely.

# ------------------------------------------------------------- 3. seed the VM

if [ ! -f "$KEY" ]; then
    run ssh-keygen -q -t ed25519 -N "" -C tier-v -f "$KEY"
fi

say "build the cloud-init seed"
if [ "$DRY" -eq 1 ]; then
    echo "+ xorriso -as mkisofs -volid cidata -joliet -rock -output $SEED user-data meta-data"
else
    _seedd="$DIR/seed-$ROW"
    rm -rf "$_seedd"; mkdir -p "$_seedd"
    {
        echo "#cloud-config"
        echo "users:"
        echo "  - name: tierv"
        echo "    sudo: 'ALL=(ALL) NOPASSWD:ALL'"
        echo "    shell: /bin/bash"
        echo "    lock_passwd: true"
        echo "    ssh_authorized_keys:"
        echo "      - $(cat "$KEY.pub")"
        echo "ssh_pwauth: false"
    } > "$_seedd/user-data"
    {
        echo "instance-id: tier-v-$ROW"
        echo "local-hostname: tier-v-$ROW"
    } > "$_seedd/meta-data"
    xorriso -as mkisofs -volid cidata -joliet -rock \
        -output "$SEED" "$_seedd/user-data" "$_seedd/meta-data" >/dev/null 2>&1
fi

# ----------------------------------------------------- 3b. interactive console

if [ "$CONSOLE" -eq 1 ]; then
    say "interactive console (Ctrl-A X to quit qemu)"
    [ "$DRY" -eq 1 ] && exit 0
    # shellcheck disable=SC2086
    exec qemu-system-x86_64 $ACCEL_ARGS -m "$PROV_MEM" -smp "$SMP" \
        -drive file="$DISK",if=virtio,format=qcow2 \
        -drive file="$WORK",if=virtio,format=qcow2 \
        -drive file="$SEED",if=virtio,format=raw,readonly=on \
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
        -device virtio-net-pci,netdev=n0 -nographic
fi

# ------------------------------------------------------------ 4. provisioning
# Deliberately at generous RAM: dpkg unpacking is not the measurement. The
# MEASURED pass below reboots at the row's real ceiling.

vm_start "$PROV_MEM" "$SMP"
vm_wait_ssh

if [ "$ROW" = v2f ]; then
    say "repoint apt at archive.debian.org (stretch left the mirrors)"
    if [ "$DRY" -eq 1 ]; then
        echo "+ guest: rewrite /etc/apt/sources.list -> archive.debian.org"
    else
        gr 'printf "deb http://archive.debian.org/debian stretch main\n" > /etc/apt/sources.list'
        gr 'printf "Acquire::Check-Valid-Until \"false\";\n" > /etc/apt/apt.conf.d/99no-check-valid'
    fi
fi

say "install the toolchain in the guest"
if [ "$DRY" -eq 1 ]; then
    echo "+ guest: apt-get install build-essential libcurl4-openssl-dev git"
else
    gr 'DEBIAN_FRONTEND=noninteractive apt-get -y -q update' >/dev/null
    # `time` is GNU time, i.e. /usr/bin/time -- NOT the shell builtin, and the
    # Debian cloud image ships without it. Without it tv-gate.sh's step 3 prints
    # "(no /usr/bin/time -v on this target)" and the row has no FOOTPRINT, which
    # for a docs/LOW_MEMORY.md row is the half that matters (measured 2026-08-13
    # on V2j/V2k, whose gates passed and whose footprint sections were empty).
    gr 'DEBIAN_FRONTEND=noninteractive apt-get -y -q install \
        build-essential libcurl4-openssl-dev git time' >/dev/null
fi

say "format and mount the work disk"
if [ "$DRY" -eq 1 ]; then
    echo "+ guest: mkfs.ext4 /dev/vdb && mount /work"
else
    gr 'mkfs.ext4 -q -F /dev/vdb && mkdir -p /work && mount /dev/vdb /work \
        && chown tierv:tierv /work'
    # Persist across the reboot into the measured pass.
    gr 'printf "/dev/vdb /work ext4 defaults 0 0\n" >> /etc/fstab'
fi

say "copy the source tree in"
# Tracked files only: no build artifacts, but local (uncommitted) edits DO
# travel -- a portability row should test the tree you are holding, not the
# last commit. tar over the ssh channel we already have; no 9p, so no
# guest-kernel module dependency and no build on a network filesystem.
if [ "$DRY" -eq 1 ]; then
    echo "+ git -C $REPO ls-files -z | tar --null -T - -c | ssh guest tar -x -C /work/jichi"
else
    g 'mkdir -p /work/jichi'
    if git -C "$REPO" rev-parse --git-dir >/dev/null 2>&1; then
        # ls-files ships TRACKED files -- uncommitted edits travel, but a NEW
        # file that was never `git add`ed does not, and the guest then fails
        # in a way that looks nothing like the cause (M273: a missing
        # tests/tools/*.c aborted the build ten minutes into a run). Warn
        # rather than ship silently; --exclude-standard keeps build output out.
        _untracked=$( cd "$REPO" && git ls-files --others --exclude-standard )
        if [ -n "$_untracked" ]; then
            echo "tier-v-vm: WARNING -- untracked files will NOT be shipped:" >&2
            printf '%s\n' "$_untracked" | sed 's/^/  /' >&2
            echo "  (git add them first if the row needs them)" >&2
        fi
        ( cd "$REPO" && git ls-files -z | tar --null -T - -cf - ) | g 'tar -xf - -C /work/jichi'
    else
        ( cd "$REPO" && tar -cf - --exclude='*.o' --exclude=.git . ) | g 'tar -xf - -C /work/jichi'
    fi
fi

# ------------------------------------------------- 5. the guest-side runbooks
# Split in two so the host can read the build time, compute the multiplier, and
# only then run the gate. Steps and order are the plan's "per-run runbook".

say "install the runbook scripts"
if [ "$DRY" -eq 1 ]; then
    echo "+ scp tv-build.sh tv-gate.sh -> guest:/work"
else
    cat > "$DIR/tv-build.sh" <<'GUESTEOF'
#!/bin/sh
# Per-run runbook, steps 0-1: identity, configure, timed build. Emits
# TIERV_BUILD_SECS / TIERV_BUILD_STATUS for the host to parse.
set -u
cd /work/jichi || exit 1

echo "--- 0. identity"
uname -a
head -2 /etc/os-release
gcc --version | head -1
ldd --version 2>&1 | head -1
echo "nproc: $(nproc)"
free -m | head -2

echo
echo "--- 1. configure"
make clean >/dev/null 2>&1 || true
make info

echo
echo "--- size-optimized build (measured first, then cleaned away)"
if make SIZE=1 >/work/size.log 2>&1; then size jichi; ls -l jichi; else
    echo "SIZE=1 build FAILED"; tail -20 /work/size.log; fi
make clean >/dev/null 2>&1 || true

echo
echo "--- timed build: make WERROR=1"
_t0=$(date +%s)
if make WERROR=1 >/work/build.log 2>&1; then _st=ok; else _st=FAIL; fi
_secs=$(( $(date +%s) - _t0 ))
echo "TIERV_BUILD_STATUS=$_st"
echo "TIERV_BUILD_SECS=$_secs"
if [ "$_st" = FAIL ]; then
    echo "--- build.log tail"
    tail -40 /work/build.log
    echo "--- kernel OOM evidence, if any"
    dmesg 2>/dev/null | grep -i -e "out of memory" -e "oom-kill" | tail -5
    exit 1
fi

# Compile everything the gate needs HERE, so the gate step only *runs*.
# check-target = test + smoke, and both build first; without this, a build
# that fell back to a larger ceiling would meet the same ceiling again inside
# the gate. make is incremental, so this reuses the objects just built.
echo
echo "--- gate prerequisites: run_tests + smoke-tools"
if make WERROR=1 run_tests smoke-tools >>/work/build.log 2>&1; then
    echo "gate prerequisites: built"
else
    echo "TIERV_BUILD_STATUS=FAIL"
    echo "gate prerequisites FAILED"; tail -40 /work/build.log; exit 1
fi
GUESTEOF

    cat > "$DIR/tv-gate.sh" <<'GUESTEOF'
#!/bin/sh
# Per-run runbook, steps 2-4: the portable gate, footprint, offline surfaces.
# $1 = the measured JC_SMOKE_TIMEOUT_MULT.
set -u
MULT="${1:-1}"
cd /work/jichi || exit 1

echo "--- 2. the portable gate: JC_SMOKE_TIMEOUT_MULT=$MULT make check-target"
_t0=$(date +%s)
if JC_SMOKE_TIMEOUT_MULT="$MULT" make check-target 2>&1; then _st=ok; else _st=FAIL; fi
echo "TIERV_GATE_STATUS=$_st"
echo "TIERV_GATE_SECS=$(( $(date +%s) - _t0 ))"

echo
echo "--- 3. footprint"
/usr/bin/time -v ./jichi --version 2>&1 | grep -E "Maximum resident" \
    || echo "(no /usr/bin/time -v on this target -- itself a finding for the runbook)"
/usr/bin/time -v ./jichi doctor 2>&1 | grep -E "Maximum resident" || true

echo
echo "--- 4. offline surfaces (no network, no key)"
./jichi doctor  || echo "(doctor exit $?)"
./jichi context || echo "(context exit $?)"
./jichi map | head -20
./jichi describe >/dev/null && echo "describe: ok"
GUESTEOF

    scp $SSH_OPTS -i "$KEY" -P "$PORT" -q \
        "$DIR/tv-build.sh" "$DIR/tv-gate.sh" tierv@127.0.0.1:/work/
    g 'chmod +x /work/tv-build.sh /work/tv-gate.sh'
fi

vm_stop

# ------------------------------------------------- 6. the measured build pass
# At the row's REAL ceiling first. Whether gcc survives a 256 MB machine is
# itself the V2e datum; only if it does not do we fall back.

[ "$DRY" -eq 1 ] || : > "$RESULTS"
{
    echo "Tier V row $ROW -- $LABEL"
    echo "run: $(date -u '+%Y-%m-%dT%H:%M:%SZ')  host: $(uname -n)  accel: $ACCEL"
    echo "host serial build: ${HOST_SECS}s"
    echo "measured at: -m $MEM -smp $SMP   (provisioned at -m $PROV_MEM)"
    echo "image: $URL"
    echo
} | res

BUILD_MEM=$MEM
BUILD_FELL_BACK=no

say "measured build pass at -m $MEM"
vm_start "$MEM" "$SMP"
vm_wait_ssh

if [ "$DRY" -eq 1 ]; then
    echo "+ guest: /work/tv-build.sh   (tee -> $RESULTS)"
    GUEST_SECS=$((HOST_SECS * 8))
else
    # NOT keyed off the pipeline's exit status -- `| tee` would swallow ssh's.
    # tv-build.sh prints TIERV_BUILD_STATUS, which is the signal we trust.
    g '/work/tv-build.sh' 2>&1 | tee -a "$RESULTS" || true
    if [ "$(grep -h '^TIERV_BUILD_STATUS=' "$RESULTS" | tail -1)" != "TIERV_BUILD_STATUS=ok" ]; then
        say "build did NOT survive ${MEM} MB -- recording that, then falling back"
        BUILD_FELL_BACK=yes
        BUILD_MEM=$PROV_MEM
        {
            echo
            echo "FINDING: make WERROR=1 did not complete at -m $MEM."
            echo "Rebuilt at -m $PROV_MEM; the gate below still ran at -m $MEM."
            echo
        } | res
        vm_stop
        vm_start "$PROV_MEM" "$SMP"
        vm_wait_ssh
        g '/work/tv-build.sh' 2>&1 | tee -a "$RESULTS" || true
        if [ "$(grep -h '^TIERV_BUILD_STATUS=' "$RESULTS" | tail -1)" != "TIERV_BUILD_STATUS=ok" ]; then
            echo "tier-v-vm: the build failed at -m $PROV_MEM too -- this is not" >&2
            echo "  a memory ceiling, it is a portability finding. See $RESULTS." >&2
            vm_stop
            exit 1
        fi
    fi
    GUEST_SECS=$(grep -h '^TIERV_BUILD_SECS=' "$RESULTS" | tail -1 | cut -d= -f2)
    [ -n "${GUEST_SECS:-}" ] || GUEST_SECS=$((HOST_SECS * 8))
fi

# The multiplier: ceil(guest / host), never below 2 -- a slow guest that
# happens to build fast still runs the PTY drivers slowly. TIERV_MULT
# overrides the measurement when the build-speed proxy understates a guest's
# INTERACTIVE cost: on V2f (kernel 4.9) a PTY turn round-trip costs ~10x what
# the build ratio suggests (M272) -- the override is stamped into the row as
# manual, so the deviation is itself recorded.
# This row deliberately does NOT use scripts/_rig_mult.sh, and the divergence is
# stated rather than left for someone to discover (M464). Two real differences:
# this rig MEASURES both terms itself in the same run (a host baseline plus the
# guest build), so there is no reference to pass in and nothing to refuse; and its
# floor is 2, not 1, because a guest that happens to build fast still runs the PTY
# drivers slowly. The shared helper is for rigs given a reference measured on
# another occasion -- the case where copying a number instead of the formula is
# possible, which is the mistake it exists to prevent.
MULT=$(( (GUEST_SECS + HOST_SECS - 1) / HOST_SECS ))
[ "$MULT" -lt 2 ] && MULT=2
if [ -n "${TIERV_MULT:-}" ]; then
    say "JC_SMOKE_TIMEOUT_MULT=$TIERV_MULT (manual TIERV_MULT; measured would be $MULT)"
    MULT="$TIERV_MULT"
else
    say "measured JC_SMOKE_TIMEOUT_MULT=$MULT  (guest ${GUEST_SECS}s / host ${HOST_SECS}s)"
fi

if [ "$BUILD_FELL_BACK" = yes ]; then
    vm_stop
    say "back down to -m $MEM for the gate"
    vm_start "$MEM" "$SMP"
    vm_wait_ssh
fi

# ------------------------------------------------------------ 7. the full gate

{
    echo
    if [ -n "${TIERV_MULT:-}" ]; then
        echo "JC_SMOKE_TIMEOUT_MULT=$MULT (MANUAL TIERV_MULT; build-ratio" \
             "measurement: guest ${GUEST_SECS}s / host ${HOST_SECS}s)"
    else
        echo "JC_SMOKE_TIMEOUT_MULT=$MULT (measured: guest ${GUEST_SECS}s / host ${HOST_SECS}s)"
    fi
    echo "build ran at -m $BUILD_MEM; gate runs at -m $MEM -smp $SMP"
    echo
} | res

if [ "$DRY" -eq 1 ]; then
    echo "+ guest: /work/tv-gate.sh $MULT   (tee -> $RESULTS)"
else
    g "/work/tv-gate.sh $MULT" 2>&1 | tee -a "$RESULTS" || true
fi

vm_stop

echo
say "row $ROW done -- $RESULTS"
if [ "$DRY" -eq 0 ]; then
    echo
    grep -E '^(TIERV_|smoke:|.*checks,.*failures)' "$RESULTS" || true
    echo
    echo "Write the row into docs/plans/2026-07-hardware-testing.md (results table)"
    echo "and a machine-stamped row into docs/LOW_MEMORY.md -- never overwriting"
    echo "an existing row (the M259 lesson)."
fi
