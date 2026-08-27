#!/bin/sh
# jhub-tier-j2.sh - the multi-user tier: what happens when TWO users share one
# tree, which is the configuration a JupyterHub makes normal.
#
# THE POINT, STATED FIRST.  jichi's advisory workspace lease is written to
# `<home>/.jichi.d/leases/<hash-of-work-tree>.json` (jc_lease_path) and its
# checkpoints to `<home>/.jichi.d/checkpoints/<same-hash>` (jc_snapshot), both
# via jc_home_dir().  Two users on ONE shared directory therefore compute the
# SAME key into DIFFERENT homes: each takes a lease, neither sees the other's,
# and both proceed as if alone.  That is verified by reading the source.  What
# has never been staged is the CONSEQUENCE, and docs/plans/2026-08-jichi-with-
# jupyterhub.md says so: "it would take two accounts and ten minutes, and it is
# the first thing to run if anyone proposes a shared workspace."  This is those
# ten minutes.
#
# WHY A VM.  Two Unix users need root, and this bench has no passwordless sudo.
# The VM is also honest about the blast radius: a test that deliberately makes
# one agent revert another's work should not run on the machine you work on.
#
# WHY THE HUB IS NOT THE MECHANISM.  J2a stages the collision with two users
# and a shared directory and NO JupyterHub at all -- because the hub is how you
# END UP in that configuration, not what causes it.  J2b then checks that a real
# hub does produce it.  Keeping them apart means the finding is not contingent
# on a hub being installed correctly.
#
# Usage:
#   sh jhub-tier-j2.sh                 # seed, boot, provision, J2a, J2b, report
#   sh jhub-tier-j2.sh --dry-run       # print every step, touch nothing
#   sh jhub-tier-j2.sh --keep          # leave the VM running afterwards
#   sh jhub-tier-j2.sh --only j2a      # one stage
#
# Env: JHUB_DIR, TIER_V_DIR (the base image), JHUB_REPO, JHUB_J2_PORT (2299),
#      JHUB_J2_MEM (2048), JHUB_J2_SMP (2)
# Exit: 0 ran (read the results file for verdicts)  1 a check failed
#       2 usage  4 setup/boot failed
set -eu

DRY=0; KEEP=0; ONLY=""
for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        --keep)    KEEP=1 ;;
        --only)    ONLY="__next__" ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *)  case "$ONLY" in __next__) ONLY="$a"; continue ;; esac
            echo "unexpected argument: $a" >&2; exit 2 ;;
    esac
done

DIR="${JHUB_DIR:-$HOME/.cache/jichi-jupyterhub}"
STAGE=$(cd "$(dirname "$0")" && pwd)          # scripts/ -- this file lives there now
TIERV="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}"
REPO="${JHUB_REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
MOCK="${JHUB_MOCKMODEL:-$REPO/tests/tools/mockmodel}"
PORT="${JHUB_J2_PORT:-2299}"
MEM="${JHUB_J2_MEM:-2048}"
SMP="${JHUB_J2_SMP:-2}"

J="$DIR/j2"
BASE="$TIERV/base-v2e.qcow2"
DISK="$J/j2.qcow2"
SEED="$J/seed.iso"
KEY="$J/id_ed25519"
CONSOLE="$J/console.log"
RESULTS="${JHUB_J2_OUT:-$DIR/results/tier-j2.txt}"
PIDFILE="$J/qemu.pid"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
-o LogLevel=ERROR -o ConnectTimeout=5 -o BatchMode=yes"

NFAIL=0
say() { echo "== $*"; }
res() { if [ "$DRY" = 1 ]; then sed 's/^/[results] /'; else tee -a "$RESULTS"; fi; }
ok()   { printf 'ok - %s\n'     "$1" | res; }
nok()  { printf 'not ok - %s\n' "$1" | res; NFAIL=$((NFAIL + 1)); }

g()  { ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"; }
gr() { g "sudo sh -c '$*'"; }

skipped() { [ -n "$ONLY" ] && [ "$ONLY" != "$1" ]; }

vm_stop() {
    [ -f "$PIDFILE" ] || return 0
    _p=$(cat "$PIDFILE")
    kill "$_p" 2>/dev/null || true
    sleep 2
    kill -9 "$_p" 2>/dev/null || true
    rm -f "$PIDFILE"
}
cleanup() { [ "$KEEP" = 1 ] || vm_stop; }
trap cleanup EXIT INT TERM

if [ "$DRY" = 1 ]; then
    echo "would run tier J2 with:"
    echo "  base image: $BASE"
    echo "  overlay:    $DISK   (${MEM}MB, ${SMP} cpu, ssh on 127.0.0.1:$PORT)"
    echo "  repo:       $REPO   (built INSIDE the guest -- a Debian 12 glibc)"
    echo "  mock model: $MOCK on the host, reached through an ssh -R tunnel"
    echo "  results:    $RESULTS"
    echo "stages: seed boot provision j2a j2b"
    echo "J2a stages the collision with two users and NO hub; J2b adds the hub."
    exit 0
fi

for t in qemu-system-x86_64 qemu-img xorriso ssh scp ssh-keygen; do
    command -v "$t" >/dev/null 2>&1 || { echo "missing: $t" >&2; exit 4; }
done
[ -f "$BASE" ] || { echo "no base image at $BASE -- run scripts/tier-v-vm.sh v2e \
once, or point TIER_V_DIR at one" >&2; exit 4; }
[ -x "$MOCK" ] || { echo "missing mockmodel: $MOCK" >&2; exit 4; }

mkdir -p "$J" "$(dirname "$RESULTS")"
: > "$RESULTS"
{
    echo "Tier J2 -- two users, one tree: the configuration a hub makes normal"
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(uname -srm)"
    echo "base: $BASE"
    echo
} >> "$RESULTS"

if [ -e /dev/kvm ] && [ -r /dev/kvm ]; then
    ACCEL_ARGS="-accel kvm -cpu host"; ACCEL=kvm
else
    ACCEL_ARGS="-accel tcg"; ACCEL=tcg
fi
echo "accelerator: $ACCEL" >> "$RESULTS"

# ------------------------------------------------------------------ seed -----
say "seed"
[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -C jhub-j2 -f "$KEY"
_sd="$J/seed"; rm -rf "$_sd"; mkdir -p "$_sd"
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
} > "$_sd/user-data"
{
    echo "instance-id: jhub-j2"
    echo "local-hostname: jhub-j2"
} > "$_sd/meta-data"
xorriso -as mkisofs -volid cidata -joliet -rock \
    -output "$SEED" "$_sd/user-data" "$_sd/meta-data" >/dev/null 2>&1

# ------------------------------------------------------------------ boot -----
say "boot (${MEM}MB, $ACCEL)"
rm -f "$DISK"
qemu-img create -q -f qcow2 -F qcow2 -b "$BASE" "$DISK" 20G
: > "$CONSOLE"
qemu-system-x86_64 $ACCEL_ARGS -m "$MEM" -smp "$SMP" \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -drive file="$SEED",if=virtio,format=raw,readonly=on \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
    -device virtio-net-pci,netdev=n0 \
    -display none -monitor none -serial "file:$CONSOLE" &
echo $! > "$PIDFILE"

i=0
until g true 2>/dev/null; do
    i=$((i + 1))
    if grep -q 'Kernel panic' "$CONSOLE" 2>/dev/null; then
        echo "guest panicked before userspace -- see $CONSOLE" >&2; exit 4
    fi
    [ "$i" -gt 300 ] && { echo "guest never answered ssh" >&2; tail -20 "$CONSOLE" >&2; exit 4; }
    sleep 2
done
say "ssh up after ~$((i * 2))s"

# ------------------------------------------------------------- provision -----
# EVERY guest-side action is a FILE, staged once and executed there. It is not
# style: `gr()` wraps its argument in single quotes, so a heredoc containing
# 'stud1' terminates it early. tier-v-vm.sh states that rule; ignoring it
# produced three checks that failed for quoting reasons and read as findings.
scp $SSH_OPTS -i "$KEY" -P "$PORT" -qr "$STAGE/jhub-guest" tierv@127.0.0.1:/tmp/guest
g 'chmod +x /tmp/guest/*.sh'

if ! skipped provision; then
    say "provision: two users, a shared tree, and jichi built in the guest"
    gr 'DEBIAN_FRONTEND=noninteractive apt-get update -qq' >/dev/null 2>&1 || true
    gr 'DEBIAN_FRONTEND=noninteractive apt-get install -y -qq git make gcc libcurl4-openssl-dev >/dev/null 2>&1' || true
    g 'sudo sh /tmp/guest/provision.sh' >/dev/null

    # jichi is BUILT IN THE GUEST: the host binary links a newer glibc than
    # Debian 12 ships, so copying it would test the wrong thing or nothing.
    ( cd "$REPO" && git archive --format=tar --prefix=jichi/ HEAD ) > "$J/jichi-src.tar"
    scp $SSH_OPTS -i "$KEY" -P "$PORT" -q "$J/jichi-src.tar" tierv@127.0.0.1:/tmp/
    g 'rm -rf ~/jichi && tar -xf /tmp/jichi-src.tar -C ~ && cd ~/jichi && make -s jichi 2>&1 | tail -3'
    if g 'test -x ~/jichi/jichi'; then
        gr 'cp /home/tierv/jichi/jichi /usr/local/bin/jichi && chmod 0755 /usr/local/bin/jichi'
        ok "jichi builds in the guest and installs ($(g '/usr/local/bin/jichi --version'))"
    else
        nok "jichi did not build in the guest -- nothing below can be measured"
        exit 4
    fi
fi

# --------------------------------------------------------------- the mock ----
# On the HOST, reached from the guest through an ssh reverse tunnel.
#
# mockmodel binds LOOPBACK ONLY and has no --bind: INADDR_LOOPBACK is hard-coded
# (deliberately -- a test server that listens on the network is a hazard). So the
# guest cannot reach it at the qemu gateway. An ssh REVERSE tunnel gives the guest
# a loopback route to it instead, which needs no change to a repo file.
cat > "$J/replies.mm" <<'EOF'
wire openai
# A run that HANGS on purpose -- with `timeouts.stall` raised in the per-user
# config, because at the default the run DIED of its own stall detector before
# the lease could be observed ("error: model stalled (timed out)"), which reads
# as "no lease was taken" when the truth is "taken, then released".
rule
  match "HOLD_THE_LEASE"
  stall header
rule
  match "\"role\":\"tool\""
  text Done.
rule
  tool write_file {"path":"a-owned-by-the-run.txt","content":"written by the run\n"}
EOF
# stud2 gets its OWN mock: a normal one, with no stalling rule at all.
cat > "$J/replies2.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text Done.
rule
  tool write_file {"path":"a-owned-by-the-run.txt","content":"written by the run\n"}
EOF

start_mock() {  # start_mock <script> <portfile>
    "$MOCK" --script "$1" --capture "$J/cap" --port-file "$2" \
            --deadline 1800 >/dev/null 2>&1 &
    _i=0
    while [ ! -s "$2" ]; do
        _i=$((_i + 1)); [ "$_i" -gt 15 ] && break; sleep 1
    done
}
start_mock "$J/replies.mm"  "$J/.port"  ; MMPID=$!
start_mock "$J/replies2.mm" "$J/.port2" ; MM2PID=$!
MPORT=$(cat "$J/.port"  2>/dev/null || echo "")
MPORT2=$(cat "$J/.port2" 2>/dev/null || echo "")
GUEST_MOCK_PORT=9000
GUEST_MOCK_PORT2=9001
ssh $SSH_OPTS -i "$KEY" -p "$PORT" -N \
    -R "${GUEST_MOCK_PORT}:127.0.0.1:${MPORT}" \
    -R "${GUEST_MOCK_PORT2}:127.0.0.1:${MPORT2}" tierv@127.0.0.1 &
TUNPID=$!
sleep 2
trap 'kill $MMPID $MM2PID $TUNPID 2>/dev/null; cleanup' EXIT INT TERM

# ------------------------------------------------------------------- J2a -----
if ! skipped j2a; then
    say "J2a -- the collision, with NO hub involved"

    if [ -n "$MPORT" ] && g "curl -s -o /dev/null --max-time 5 \
http://127.0.0.1:$GUEST_MOCK_PORT/v1/models" 2>/dev/null; then
        ok "the guest reaches the host's mock model through the reverse tunnel"
    else
        nok "the guest cannot reach the mock model -- every run below would fail \
for that reason and not for a jichi one"
    fi
    g "sudo sh /tmp/guest/mkconfig.sh $GUEST_MOCK_PORT $GUEST_MOCK_PORT2" >/dev/null

    g 'sudo -u stud1 sh -c "cd /srv/shared && echo mine > a-stud1.txt"' || true
    g 'sudo -u stud2 sh -c "cd /srv/shared && echo mine > b-stud2.txt"' || true

    g 'sudo sh /tmp/guest/j2a.sh' > "$J/j2a.out" 2>&1 || true
    sed 's/^/    /' "$J/j2a.out" >> "$RESULTS"

    L1=$(sed -n '0,/leases with BOTH/{s/^stud1: \(.*\)$/\1/p}' "$J/j2a.out" | head -1 | tr -d ' ')
    ALIVE=$(sed -n 's/^stud1 alive: \(.*\)$/\1/p' "$J/j2a.out" | head -1)
    C1=$(sed -n '/--- checkpoints ---/,$p' "$J/j2a.out" | sed -n 's/^stud1: \(.*\)$/\1/p' | tr -d ' ')
    C2=$(sed -n '/--- checkpoints ---/,$p' "$J/j2a.out" | sed -n 's/^stud2: \(.*\)$/\1/p' | tr -d ' ')

    if [ -n "$L1" ]; then
        ok "while running (stud1 processes alive: $ALIVE) stud1 holds a lease in \
its OWN home: /home/stud1/.jichi.d/leases/$L1"
    else
        nok "stud1 held no lease mid-run (alive: $ALIVE) -- see the transcript above"
    fi

    if grep -q "already held by a live jichi run" "$J/j2a.out"; then
        ok "stud2 was STOPPED by stud1's lease -- the protection DOES cross users"
    elif grep -qE "^Done\.|write the file" "$J/j2a.out" && [ -n "$L1" ]; then
        ok "THE FINDING: with stud1 holding a lease under --lease fail, stud2 ran \
to completion on the SAME tree. The lease is per-\$HOME: it never sees another \
user, so it does not protect a shared directory"
    else
        nok "stud2's run was inconclusive -- see the transcript above"
    fi

    if [ -n "$C1" ] && [ "$C1" = "$C2" ]; then
        ok "two INDEPENDENT checkpoint histories over one tree (both keyed $C1, \
one per home) -- /undo restores YOUR view of a tree someone else has changed"
    else
        nok "checkpoint keys: stud1=[$C1] stud2=[$C2]"
    fi
fi

# ------------------------------------------------------------------- J2b -----
if ! skipped j2b; then
    say "J2b -- a real JupyterHub, spawning those same two users"
    if [ -d "$DIR/wheelhouse-guest" ]; then
        tar -cf "$J/wh.tar" -C "$DIR" wheelhouse-guest
        scp $SSH_OPTS -i "$KEY" -P "$PORT" -q "$J/wh.tar" tierv@127.0.0.1:/tmp/
        g 'rm -rf /tmp/wheelhouse-guest && tar -xf /tmp/wh.tar -C /tmp'
    fi
    g 'sudo sh /tmp/guest/hub_install.sh' > "$J/hub_install.out" 2>&1 || true
    sed 's/^/    /' "$J/hub_install.out" >> "$RESULTS"

    HUBV=$(sed -n 's/^jupyterhub: \(.*\)$/\1/p' "$J/hub_install.out" | head -1)
    CHP=$(sed -n 's/^chp: \(.*\)$/\1/p' "$J/hub_install.out" | head -1)
    if [ -n "$HUBV" ] && [ "$HUBV" != none ]; then
        ok "the cp311 guest wheelhouse installs JupyterHub $HUBV OFFLINE in the \
guest -- the second wheelhouse was not a theoretical precaution"
    else
        nok "JupyterHub did not install in the guest"
    fi
    if [ -n "$CHP" ] && [ "$CHP" != none ]; then
        ok "configurable-http-proxy present ($CHP)"
    else
        nok "configurable-http-proxy absent -- the hub cannot route"
    fi

    if [ "$HUBV" != none ] && [ -n "$HUBV" ]; then
        g 'sudo sh /tmp/guest/hub_config.sh' > "$J/hub_config.out" 2>&1 || true
        sed 's/^/    /' "$J/hub_config.out" >> "$RESULTS"
        if grep -qiE "JupyterHub is now running|Hub API listening" "$J/hub_config.out"; then
            ok "the hub starts with PAM auth + LocalProcessSpawner and its own config"
        else
            nok "the hub did not report itself running -- see the log above"
        fi
        if grep -q READABLE "$J/hub_config.out" && grep -q NAMES_ENV_VAR "$J/hub_config.out" \
           && grep -q NO_LITERAL_KEY "$J/hub_config.out"; then
            ok "the hub-wide jichi config is readable by a spawned user and names \
an env var, never a literal key"
        else
            nok "the hub-wide config check failed -- see the log above"
        fi
    fi
fi

kill $MMPID $MM2PID $TUNPID 2>/dev/null || true
printf '\n%s check(s) failed\n' "$NFAIL" >> "$RESULTS"
say "results: $RESULTS"
[ "$NFAIL" -eq 0 ] || exit 1
