#!/bin/sh
# tier-v-tiny.sh - run jichi on a WHOLE MACHINE below a stock distro's boot
# floor: a kernel plus a busybox initramfs carrying a static jichi, and nothing
# else. No disk, no distro, no root on the host.
#
# WHY THIS EXISTS. scripts/tier-v-vm.sh took the Debian 12 cloud image down the
# ladder and it stopped well above the tiers that needed measuring (measured
# 2026-08-13, threadwork):
#
#   192 MB  gate ok      256 MB is V2e, already published
#   160 MB  gate ok      <- the lowest ceiling a stock image survives
#   128 MB  PANIC        "System is deadlocked on memory" unpacking its initrd,
#                        after the kernel reserved 59,592K of 130,516K
#    64 MB  no kernel    never leaves GRUB: cannot load kernel+initrd
#    32 MB  no kernel    same
#
# So docs/LOW_MEMORY.md's "~128 MB" and "<=64 MB" tiers CANNOT be graded with a
# distro: what fails is the image, before any userspace exists, and that says
# nothing about jichi. This script removes the distro from the question. What it
# measures is therefore narrower and should be quoted narrowly: **jichi RUNNING**
# on a machine of that size, not jichi building, and not a distro booting.
#
# HONEST SCOPE, stated up front:
#   - The payload is a STATIC MUSL, CURL-FREE jichi, so there is no model call
#     here -- only the offline surfaces (--version, map, context, doctor,
#     describe). A turn at these ceilings needs the minimal-libcurl static build
#     (scripts/minimal-curl.sh) and is a separate rung.
#   - A VM is not a board. The host page cache still holds the images and the
#     kernel's own footprint is inside -m, not outside it, so this is closer to a
#     real small machine than a cgroup ceiling but is still not one.
#   - `make check-target` cannot run: there is no toolchain in the initramfs.
#
# Needs nothing installed: the kernel is the one tier-v-vm.sh already staged
# (~/.cache/jichi-tier-v/v6-vmlinuz), busybox is the host's static
# /usr/bin/busybox, and qemu's user-mode networking needs no root.
#
# Usage:
#   scripts/tier-v-tiny.sh                    # default sweep, descending
#   scripts/tier-v-tiny.sh 64 48 32 16        # explicit ceilings
#   scripts/tier-v-tiny.sh --bin path/to/jichi
#   scripts/tier-v-tiny.sh --dry-run
#
# Env: TIER_V_DIR (default ~/.cache/jichi-tier-v), TINY_KERNEL (override),
#      KVM_ARGS (default "-accel kvm -cpu host"; use "-accel tcg" with no /dev/kvm),
#      TINY_DEADLINE (per-guest seconds, default 120; raise it for TCG).
set -u

DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}"
KERNEL="${TINY_KERNEL:-$DIR/v6-vmlinuz}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=""
DRY=0
TURN=0
CEILINGS=""

while [ $# -gt 0 ]; do
    case "$1" in
        --bin)     shift; BIN="${1:?--bin needs a path}" ;;
        --turn)    TURN=1 ;;
        --dry-run) DRY=1 ;;
        -h|--help) awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        -*) echo "tier-v-tiny: unknown option: $1" >&2; exit 2 ;;
        *)  CEILINGS="$CEILINGS $1" ;;
    esac
    shift
done
[ -n "${CEILINGS# }" ] || CEILINGS="256 192 128 96 64 48 32 24 16"
[ -n "$BIN" ] || BIN="$ROOT/jichi"

BUSYBOX=$(command -v busybox || true)
OUT="$DIR/tiny-initramfs.cpio.gz"
# One results file PER MODE. They are different measurements -- the turn rung
# carries a curl-enabled payload and a NIC driver, so its initramfs is larger and
# its floor is higher -- and a single file truncated per run silently destroyed
# the offline row when the turn sweep ran next (measured 2026-08-13: the 64 MB
# offline COMPLETE was overwritten by the turn sweep's 64 MB PANIC, which reads
# as a contradiction rather than two facts).
if [ "$TURN" -eq 1 ]; then
    RESULTS="$DIR/results-tiny-turn.txt"
else
    RESULTS="$DIR/results-tiny-offline.txt"
fi
MARK_V=TINY_VERSION_OK
MARK_M=TINY_MAP_OK
MARK_D=TINY_DOCTOR_OK
MARK_END=TINY_RUN_COMPLETE
MARK_TURN=TINY_TURN_ANSWER_OK
# --turn needs a model. It runs on the HOST, outside -m, so the ceiling measures
# the guest's jichi and not the mock -- the same rule tests/measure/ram_floor.sh
# states. QEMU user-mode networking maps the host's loopback to 10.0.2.2, so a
# mock bound to 127.0.0.1 is reachable from the guest with no bridge and no root.
HOST_GW=10.0.2.2
GUEST_IP=10.0.2.15

say() { echo "== $*"; }
res() { if [ "$DRY" -eq 1 ]; then sed 's/^/[results] /'; else cat >> "$RESULTS"; fi; }

command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "tier-v-tiny: no qemu-system-x86_64" >&2; exit 2; }
command -v cpio >/dev/null 2>&1 || { echo "tier-v-tiny: no cpio" >&2; exit 2; }
[ -n "$BUSYBOX" ] || { echo "tier-v-tiny: no busybox on PATH (apt install busybox-static)" >&2; exit 2; }
[ -r "$KERNEL" ] || { echo "tier-v-tiny: no readable kernel at $KERNEL" >&2
    echo "  (the host's /boot/vmlinuz-* is mode 0600 root; run a tier-v-vm.sh" >&2
    echo "   row first, or point TINY_KERNEL at any bzImage you can read)" >&2; exit 2; }
[ -x "$BIN" ] || { echo "tier-v-tiny: no jichi at $BIN" >&2; exit 2; }

# A DYNAMIC binary cannot work here: the initramfs has no ld.so and no libc.
# Refuse loudly rather than produce a guest that dies with a confusing message.
if ldd "$BIN" 2>&1 | grep -qv 'not a dynamic executable'; then
    echo "tier-v-tiny: $BIN is dynamically linked -- the initramfs has no libc." >&2
    echo "  Build a static one:" >&2
    echo '    make clean && make CC="zig cc -target x86_64-linux-musl" \' >&2
    echo '         HAVE_CURL= SIZE=1 jichi' >&2
    exit 2
fi

say "tiny guest"
echo "   kernel  : $KERNEL"
echo "   jichi   : $BIN ($(wc -c < "$BIN" | tr -d '[:space:]') bytes, static)"
echo "   busybox : $BUSYBOX"
echo "   results : $RESULTS"
echo

# ------------------------------------------------------------- 1. the initramfs
# /init is PID 1. It mounts proc (jichi reads /proc/self/status for the RSS
# gauge), runs the offline surfaces, prints a marker per surface plus a final
# one, then powers the machine off. Markers are how a run is verified: a guest
# that OOMs mid-way prints some and not the last, which is exactly the
# distinction docs/TEST_INTEGRITY.md insists on -- never an exit code.
build_initramfs() {
    _d=$(mktemp -d "${TMPDIR:-/tmp}/tinyfs.XXXXXX") || exit 2
    mkdir -p "$_d/bin" "$_d/proc" "$_d/sys" "$_d/dev" "$_d/tmp" "$_d/work"
    cp "$BUSYBOX" "$_d/bin/busybox"
    for _l in sh mount umount echo cat ls mkdir sleep printf grep head poweroff dmesg free ip insmod; do
        ln -sf busybox "$_d/bin/$_l"
    done
    if [ "$TURN" -eq 1 ] && [ -n "$MODS" ] && [ -d "$MODS" ]; then
        mkdir -p "$_d/modules"
        cp "$MODS"/*.ko "$_d/modules/" 2>/dev/null || true
    fi
    cp "$BIN" "$_d/jichi"
    if [ "$TURN" -eq 1 ]; then
        cat > "$_d/work/config.json" <<CFGEOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://$HOST_GW:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"markdown":false,
"lowResource":true,"maxRetries":0}
CFGEOF
    fi

    # The turn stanza is built here so the heredoc below stays one template.
    # Static IP rather than udhcpc: slirp's addresses are fixed and a DHCP
    # client is one more moving part inside the ceiling being measured.
    TURN_BLOCK=""
    if [ "$TURN" -eq 1 ]; then
        TURN_BLOCK=$(cat <<TBEOF
echo "--- load the NIC driver (insmod in dependency order; busybox has no depmod)"
for m in virtio virtio_ring virtio_pci_modern_dev virtio_pci_legacy_dev virtio_pci failover net_failover virtio_net; do
    [ -f /modules/\$m.ko ] && insmod /modules/\$m.ko 2>/dev/null
done
echo "--- network: interfaces the kernel actually has"
# Printed because the first attempt failed with no eth0 and nothing to say why:
# a distro kernel ships virtio_net as a MODULE, and this initramfs carries no
# modules, so the NIC exists on the PCI bus and has no driver. Naming the
# interfaces turns that into a one-line diagnosis instead of a guess.
ls /sys/class/net 2>/dev/null | tr '\n' ' '; echo
echo "--- network up (slirp: guest $GUEST_IP, host $HOST_GW)"
ip link set eth0 up 2>/dev/null
ip addr add $GUEST_IP/24 dev eth0 2>/dev/null
ip route add default via $HOST_GW 2>/dev/null
echo "--- A REAL TURN against the host's mock model"
/jichi --config /work/config.json --no-session -q --lite -p "tiny turn probe"
TBEOF
)
    fi
    cat > "$_d/init" <<INITEOF
#!/bin/sh
export PATH=/bin
mount -t proc proc /proc 2>/dev/null
mount -t sysfs sysfs /sys 2>/dev/null
export HOME=/work
echo "TINY_BOOT_OK"
echo "--- what the machine has"
free -m 2>/dev/null | head -2
echo "--- jichi --version"
/jichi --version && echo $MARK_V
echo "--- jichi map (a real workspace walk over /work)"
/jichi map >/dev/null 2>&1 && echo $MARK_M
echo "--- jichi doctor (curl-free build: libcurl reported absent, by design)"
/jichi doctor >/dev/null 2>&1; echo "doctor exit=\$? (nonzero is expected without libcurl)"
echo $MARK_D
echo "--- jichi describe"
/jichi describe >/dev/null 2>&1 && echo "TINY_DESCRIBE_OK"
$TURN_BLOCK
echo "--- footprint, from jichi's own gauge"
# busybox has no \`time -v\`, and /proc/self/status here would be the SHELL's
# (a dead child's /proc entry is gone), so use the M180 gauge jichi prints
# itself -- the same instrument the M272 Pi rows fell back to.
/jichi context 2>/dev/null | grep -E "Process:|Arenas:"
echo $MARK_END
poweroff -f
INITEOF
    chmod +x "$_d/init"
    ( cd "$_d" && find . | cpio -o -H newc --quiet | gzip -9 ) > "$OUT"
    rm -rf "$_d"
}

# ------------------------------------------- 1a. a NIC driver, if we need a turn
# A distro kernel ships virtio_net as a MODULE, and this initramfs is built from
# scratch, so the guest saw only `lo` and the turn could not connect (measured
# 2026-08-13 on both the Debian 6.1 and Alpine 6.12 kernels: `ls /sys/class/net`
# printed "lo" alone). Nothing to do with jichi or with memory -- the NIC was on
# the PCI bus with no driver. Alpine publishes a modloop squashfs of modules for
# exactly the netboot kernel, so for the Alpine kernel we can lift virtio_net and
# its dependencies out of it. Only needed for --turn; the offline rungs do not
# touch the network at all.
MODS=""
stage_modules() {
    _kver=$(file -b "$KERNEL" | sed -n 's/.*version \([^ ]*\).*/\1/p')
    [ -n "$_kver" ] || { echo "tier-v-tiny: cannot read the kernel version" >&2; return 1; }
    case "$_kver" in
        *-virt) _rel=$(echo "$_kver" | sed 's/^\([0-9]*\.[0-9]*\).*/\1/') ;;
        *) echo "tier-v-tiny: --turn needs the Alpine 'virt' kernel for modules." >&2
           echo "  This kernel is $_kver; its modules are not published standalone." >&2
           echo "  Offline rungs work with any kernel; only --turn needs a NIC." >&2
           return 1 ;;
    esac
    MODS="$DIR/modules-$_kver"
    [ -d "$MODS" ] && { say "modules already staged for $_kver"; return 0; }
    _ml="$DIR/modloop-virt-$_kver"
    if [ ! -f "$_ml" ]; then
        say "download Alpine modloop for $_kver"
        curl -fL --progress-bar -o "$_ml.part" \
          "https://dl-cdn.alpinelinux.org/alpine/v3.21/releases/x86_64/netboot/modloop-virt" \
          || { rm -f "$_ml.part"; return 1; }
        mv "$_ml.part" "$_ml"
    fi
    say "extract virtio_net + deps"
    _tmp=$(mktemp -d "${TMPDIR:-/tmp}/modloop.XXXXXX") || return 1
    unsquashfs -q -f -d "$_tmp/x" "$_ml" >/dev/null 2>&1 || { rm -rf "$_tmp"; return 1; }
    mkdir -p "$MODS"
    for _m in virtio virtio_ring virtio_pci virtio_pci_modern_dev virtio_pci_legacy_dev \
              failover net_failover virtio_net; do
        _f=$(find "$_tmp/x" -name "$_m.ko*" 2>/dev/null | head -1)
        [ -n "$_f" ] || continue
        case "$_f" in
            *.gz) gunzip -c "$_f" > "$MODS/$_m.ko" ;;
            *.xz) xz -dc "$_f" > "$MODS/$_m.ko" 2>/dev/null || continue ;;
            *)    cp "$_f" "$MODS/$_m.ko" ;;
        esac
    done
    rm -rf "$_tmp"
    [ -f "$MODS/virtio_net.ko" ] || { echo "tier-v-tiny: virtio_net.ko not found in the modloop" >&2
        rm -rf "$MODS"; return 1; }
    echo "   staged: $(ls "$MODS" | tr '\n' ' ')"
}

MM_PORT=""
MM_PID=""
if [ "$TURN" -eq 1 ] && [ "$DRY" -eq 0 ]; then
    stage_modules || {
        echo "tier-v-tiny: could not stage a NIC driver -- the turn rung is UNRUN." >&2
        echo "  The offline sweep (without --turn) is unaffected and still valid." >&2
        exit 2
    }
    MOCK="$ROOT/tests/tools/mockmodel"
    [ -x "$MOCK" ] || { echo "tier-v-tiny: --turn needs $MOCK (run 'make smoke-tools')" >&2; exit 2; }
    MMDIR=$(mktemp -d "${TMPDIR:-/tmp}/tinymm.XXXXXX") || exit 2
    trap 'kill "$MM_PID" 2>/dev/null; rm -rf "$MMDIR"' EXIT INT TERM
    mkdir -p "$MMDIR/cap"
    printf 'wire openai\nrule\n  text %s\n' "$MARK_TURN" > "$MMDIR/r.mm"
    # --max-requests unset and a long deadline: one mock serves the whole sweep.
    "$MOCK" --script "$MMDIR/r.mm" --capture "$MMDIR/cap" \
            --port-file "$MMDIR/.port" --deadline 3600 >/dev/null 2>&1 &
    MM_PID=$!
    _i=0
    while [ ! -s "$MMDIR/.port" ]; do
        kill -0 "$MM_PID" 2>/dev/null || { echo "tier-v-tiny: mock died" >&2; exit 2; }
        _i=$((_i + 1)); [ "$_i" -gt 10 ] && { echo "tier-v-tiny: mock silent" >&2; exit 2; }
        sleep 1
    done
    MM_PORT=$(cat "$MMDIR/.port")
    say "host mock model on 127.0.0.1:$MM_PORT (reachable in-guest as $HOST_GW)"
fi

if [ "$DRY" -eq 1 ]; then
    echo "+ build initramfs -> $OUT (busybox + static jichi + /init)"
else
    say "build the initramfs"
    build_initramfs
    echo "   initramfs: $(wc -c < "$OUT" | tr -d '[:space:]') bytes"
fi
echo

# ------------------------------------------------------------------ 2. the sweep
[ "$DRY" -eq 1 ] || {
    : > "$RESULTS"
    {
        echo "Tier V tiny -- kernel + busybox initramfs + static musl jichi"
        echo "run: $(date -u '+%Y-%m-%dT%H:%M:%SZ')  host: $(uname -n)"
        echo "kernel: $(file -b "$KERNEL" | cut -c1-80)"
        echo "jichi: $(wc -c < "$BIN" | tr -d '[:space:]') bytes static; initramfs $(wc -c < "$OUT" | tr -d '[:space:]') bytes"
        echo "NOTE: curl-free payload -- offline surfaces only, no model call."
        echo
    } | res
}

printf 'ceiling  verdict     detail\n'
floor=""
for mb in $CEILINGS; do
    LOG="$DIR/console-tiny-$mb.log"
    if [ "$DRY" -eq 1 ]; then
        echo "+ qemu-system-x86_64 -m $mb -kernel $KERNEL -initrd $OUT ..."
        continue
    fi
    : > "$LOG"
    # No -drive at all: the initramfs IS the root. `-no-reboot` so a poweroff
    # ends the process instead of looping.
    NET_ARGS=""
    [ "$TURN" -eq 1 ] && NET_ARGS="-netdev user,id=n0 -device virtio-net-pci,netdev=n0"
    # shellcheck disable=SC2086
    qemu-system-x86_64 ${KVM_ARGS:--accel kvm -cpu host} -m "$mb" -smp 1 \
        -kernel "$KERNEL" -initrd "$OUT" $NET_ARGS \
        -append "console=ttyS0 panic=1 quiet" \
        -display none -monitor none -no-reboot \
        -serial "file:$LOG" >/dev/null 2>"$LOG.qemu" &
    _pid=$!
    _i=0
    while kill -0 "$_pid" 2>/dev/null; do
        grep -q "$MARK_END" "$LOG" 2>/dev/null && break
        grep -q 'Kernel panic' "$LOG" 2>/dev/null && break
        # 120 s is ample under KVM and NOT under TCG, where the offline surfaces
        # (`map` walks a workspace) run an order of magnitude slower. The script
        # already lets you choose the accelerator via KVM_ARGS, so it has to let
        # you choose the deadline too -- otherwise KVM_ARGS="-accel tcg" can only
        # ever report PARTIAL, which reads as a jichi result and is a rig one.
        # Measured: a 768 MB guest under TCG on a Ryzen 9 3900X times out during
        # `map` at 120 s.
        _i=$((_i + 1)); [ "$_i" -gt "${TINY_DEADLINE:-120}" ] && break
        sleep 1
    done
    kill "$_pid" 2>/dev/null; wait "$_pid" 2>/dev/null

    # With --turn, reaching the END marker is NOT enough: the turn itself must
    # have produced the model's answer. Otherwise a guest whose network never
    # came up would run every other surface and score COMPLETE, which is exactly
    # the "assertion matched, but not the thing it named" failure.
    if [ "$TURN" -eq 1 ] && grep -q "$MARK_END" "$LOG" 2>/dev/null \
       && ! grep -q "$MARK_TURN" "$LOG" 2>/dev/null; then
        printf '%5s MB  NO TURN     surfaces ran, but the model call did not answer\n' "$mb"
        { echo "$mb MB: NO TURN -- offline surfaces ran; no $MARK_TURN in the output"; } | res
        break
    fi
    if grep -q "$MARK_END" "$LOG" 2>/dev/null; then
        _hwm=$(grep -o 'Process:.*peak [0-9]* KB' "$LOG" | tail -1 | tr -s ' ')
        _turn=""
        [ "$TURN" -eq 1 ] && _turn=" + REAL TURN"
        printf '%5s MB  COMPLETE    all surfaces ran%s%s\n' "$mb" "$_turn" \
               "${_hwm:+, $_hwm}"
        floor=$mb
        { echo "$mb MB: COMPLETE$_turn -- $( grep -c 'TINY_.*_OK' "$LOG" ) markers${_hwm:+, $_hwm}"; } | res
    elif grep -q 'Kernel panic' "$LOG" 2>/dev/null; then
        _why=$(grep -m1 'Kernel panic' "$LOG" | sed 's/.*Kernel panic[^:]*: //')
        printf '%5s MB  PANIC       %s\n' "$mb" "$_why"
        { echo "$mb MB: PANIC -- $_why"; } | res
        break
    elif ! grep -q 'TINY_BOOT_OK' "$LOG" 2>/dev/null; then
        # `grep -c` already prints 0 and exits 1 when there is no match, so the
        # obvious `|| echo 0` appends a SECOND line and `[ "0 0" -eq 0 ]` dies
        # with "Illegal number" -- which then took the else branch and reported
        # NO INIT for a guest whose console log was completely empty, i.e. the
        # wrong verdict. Measured that on the 64 MB rung, 2026-08-13.
        if ! grep -q 'Linux version' "$LOG" 2>/dev/null; then
            _qe=$(dd if="$LOG.qemu" bs=120 count=1 2>/dev/null | tr '\n' ' ')
            printf '%5s MB  NO KERNEL   never started%s\n' "$mb" \
                   "${_qe:+ -- qemu: $_qe}"
            { echo "$mb MB: NO KERNEL -- no \"Linux version\" banner${_qe:+; qemu said: $_qe}"; } | res
        else
            printf '%5s MB  NO INIT     kernel booted, /init never ran\n' "$mb"
            { echo "$mb MB: NO INIT -- kernel started, init did not"; } | res
        fi
        break
    else
        printf '%5s MB  PARTIAL     booted, did not finish all surfaces\n' "$mb"
        { echo "$mb MB: PARTIAL -- booted, incomplete"; } | res
        break
    fi
done

echo
if [ "$DRY" -eq 1 ]; then exit 0; fi
if [ -n "$floor" ]; then
    if [ "$TURN" -eq 1 ]; then
        echo "lowest ceiling with every offline surface AND a verified model turn: ${floor} MB"
        echo "scope: jichi RUNNING on a whole machine that size -- not building."
        echo "       The turn is HTTP against a host-side mock, so the TLS handshake"
        echo "       path is not exercised; a real HTTPS endpoint would cost more."
        { echo; echo "FLOOR: ${floor} MB (offline surfaces + a verified turn, static musl)"; } | res
    else
        echo "lowest ceiling where every offline surface ran: ${floor} MB"
        echo "scope: jichi RUNNING on a whole machine that size -- not building,"
        echo "       and no model call was attempted (pass --turn for that)."
        { echo; echo "FLOOR: ${floor} MB (offline surfaces only)"; } | res
    fi
else
    echo "no ceiling in the sweep completed -- widen it upward"
    { echo; echo "FLOOR: none in sweep"; } | res
fi
echo "results: $RESULTS"
