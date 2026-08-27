#!/bin/sh
# jhub-build-in-image.sh - jichi ships as SOURCE. Measure what it actually costs
# to get it into a JupyterHub single-user image, starting from a pristine one.
#
# WHY THIS EXISTS.  docs/JUPYTERHUB.md told administrators to "install jichi in
# the image" and never said how -- because there is nothing to install. There is
# no package, no wheel, no binary release; whoever deploys jichi builds it. That
# makes the build a deployment step, and a deployment step with no numbers is a
# sentence, not a recipe.
#
# It answers four questions a container image turns on:
#   1. what does a stock image already have?
#   2. what must be added, exactly?
#   3. how long does it take, and how big is the result?
#   4. can the BUILD dependencies then be REMOVED -- i.e. is a two-stage image
#      or an apt-remove worth it, and does the binary still run afterwards?
#
# Uses the SAME pristine Debian 12 cloud image as Tier J2, on a fresh overlay.
#
# Usage:  sh jhub-build-in-image.sh [--dry-run] [--keep]
# Env:    JHUB_DIR, TIER_V_DIR, JHUB_REPO, JHUB_BI_PORT (2298), JHUB_BI_MEM (2048)
# Exit:   0 measured   1 the build failed   4 setup/boot failed
set -eu

DRY=0; KEEP=0
for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        --keep) KEEP=1 ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

DIR="${JHUB_DIR:-$HOME/.cache/jichi-jupyterhub}"
STAGE=$(cd "$(dirname "$0")" && pwd)          # scripts/ -- this file lives there now
TIERV="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}"
REPO="${JHUB_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
PORT="${JHUB_BI_PORT:-2298}"
MEM="${JHUB_BI_MEM:-2048}"

B="$DIR/build-image"
BASE="$TIERV/base-v2e.qcow2"
DISK="$B/bi.qcow2"
SEED="$B/seed.iso"
KEY="$B/id_ed25519"
CONSOLE="$B/console.log"
OUT="${JHUB_BI_OUT:-$DIR/results/build-in-image.txt}"
PIDFILE="$B/qemu.pid"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o LogLevel=ERROR -o ConnectTimeout=5 -o BatchMode=yes"
g() { ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"; }

vm_stop() {
    [ -f "$PIDFILE" ] || return 0
    _p=$(cat "$PIDFILE"); kill "$_p" 2>/dev/null || true
    sleep 2; kill -9 "$_p" 2>/dev/null || true; rm -f "$PIDFILE"
}
cleanup() { [ "$KEEP" = 1 ] || vm_stop; }
trap cleanup EXIT INT TERM

if [ "$DRY" = 1 ]; then
    echo "would measure the build path with:"
    echo "  pristine base: $BASE"
    echo "  overlay:       $DISK (${MEM}MB, ssh 127.0.0.1:$PORT)"
    echo "  source:        git archive HEAD from $REPO"
    echo "  results:       $OUT"
    exit 0
fi

[ -f "$BASE" ] || { echo "no base image at $BASE" >&2; exit 4; }
mkdir -p "$B" "$(dirname "$OUT")"

[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -C jhub-bi -f "$KEY"
_sd="$B/seed"; rm -rf "$_sd"; mkdir -p "$_sd"
{
    echo "#cloud-config"; echo "users:"; echo "  - name: tierv"
    echo "    sudo: 'ALL=(ALL) NOPASSWD:ALL'"; echo "    shell: /bin/bash"
    echo "    lock_passwd: true"; echo "    ssh_authorized_keys:"
    echo "      - $(cat "$KEY.pub")"
    echo "ssh_pwauth: false"
} > "$_sd/user-data"
{ echo "instance-id: jhub-bi"; echo "local-hostname: jhub-bi"; } > "$_sd/meta-data"
xorriso -as mkisofs -volid cidata -joliet -rock \
    -output "$SEED" "$_sd/user-data" "$_sd/meta-data" >/dev/null 2>&1

if [ -e /dev/kvm ] && [ -r /dev/kvm ]; then A="-accel kvm -cpu host"; AN=kvm
else A="-accel tcg"; AN=tcg; fi

rm -f "$DISK"
qemu-img create -q -f qcow2 -F qcow2 -b "$BASE" "$DISK" 20G
: > "$CONSOLE"
qemu-system-x86_64 $A -m "$MEM" -smp 2 \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -drive file="$SEED",if=virtio,format=raw,readonly=on \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
    -device virtio-net-pci,netdev=n0 \
    -display none -monitor none -serial "file:$CONSOLE" &
echo $! > "$PIDFILE"

i=0
until g true 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -gt 200 ] && { echo "guest never answered ssh" >&2; exit 4; }
    sleep 2
done

( cd "$REPO" && git archive --format=tar --prefix=jichi/ HEAD ) > "$B/jichi-src.tar"
scp $SSH_OPTS -i "$KEY" -P "$PORT" -q "$B/jichi-src.tar" tierv@127.0.0.1:/tmp/
scp $SSH_OPTS -i "$KEY" -P "$PORT" -q "$STAGE/jhub-guest/build_measure.sh" tierv@127.0.0.1:/tmp/

{
    echo "Getting jichi into a hub image -- measured from a PRISTINE Debian 12"
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(uname -srm), accel: $AN, guest: ${MEM}MB / 2 cpu"
    echo "source: $(cd "$REPO" && git log --oneline -1)"
    echo
} > "$OUT"

g 'sudo sh /tmp/build_measure.sh' 2>&1 | tee -a "$OUT"
echo
echo "wrote $OUT"
grep -q BUILD_MEASURE_DONE "$OUT" || exit 1
