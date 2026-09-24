#!/bin/sh
# tier-v-freemint.sh -- the FreeMiNT row: an Atari MiNT kernel and MiNTLib, under ARAnyM.
#
# WHY THIS EXISTS. Every step below was performed by hand first, on 2026-09-24
# (M726, docs/plans/2026-09-freemint-aranym.md section 9); this is the transcript
# of a row that ran, not a guess at one. The Guix and illumos rigs set that rule.
#
# WHAT IT UNIQUELY EXERCISES, none of which another row can:
#   * MiNTLib, an older glibc derivative that hides POSIX.1-2001 interfaces at
#     strict POSIX (M723) -- the first libc here outside the glibc/musl/BSD/SysV set;
#   * the FreeMiNT kernel, which is none of Linux, a BSD, SysV or Windows;
#   * a FIXED stack: MiNT never grows it, which is how M726 and M727 were found;
#   * big-endian m68k with an FPU on a real kernel, not qemu-user's translation.
#
# WHAT IT NEEDS, all measured on the workstation:
#   * aranym (the operator's `sudo apt install aranym`); aranym-mmu, as the
#     FreeMiNT archive's own launcher uses, because MEM_PROT needs the MMU;
#   * m68k-atari-mint-gcc (ppa:vriviere/ppa, cross-mint-essential) for step 2;
#   * the FreeMiNT ARAnyM snapshot, pinned by commit hash AND sha256 -- there is
#     no release to pin to, and "latest" moves under you;
#   * EmuTOS 1.4's generic 512k image. ARAnyM 1.1.0 refuses the archive's own
#     1024k EmuTOS ("This 1024k ROM isn't supported by your ARAnyM version") and
#     then does NOT exit; EmuTOS 1.4's ARAnyM build is 1024k too.
#
# WHAT IT REFUSES TO COPY FROM THE ARCHIVE'S CONFIGURATION. It maps drive D: to
# the host's whole home directory, and puts the network on 192.168.0.x, which is
# this LAN's own subnet. The rig writes its own: no D:, a case-sensitive E: for
# results (the trailing colon), no network, no mouse grab, no audio.
#
# HOW A RUN ENDS. Nothing in the image halts the machine, so the rig builds a
# 132 KB helper that calls MiNTLib's Shutdown(0): the kernel syncs and unmounts,
# and ARAnyM exits by itself. Progress reaches the host through /dev/nfstderr;
# the console does not (RedirConsole carries nothing of MiNT's). The sentinel
# carries this run's id, because a waiter that trusts whatever sentinel it finds
# reads an earlier run's result as this one's.
#
# Steps (--step N):
#   1  boot, write the sentinel, halt                       (9 s measured)
#   2  cross-build run_tests and jichi from the tree, run the suite,
#      `jichi --version` and `jichi describe` in the guest  (31 s measured)
#   4  cross-build jichi WITH a TLS-free libcurl for MiNT and run the DRIVEN task
#      (scripts/_rig_live.sh: a text turn, then a tool turn whose answer is a
#      phrase minted this second) from inside the guest, over a point-to-point
#      tap link to the host's model server                 (17 s measured, M737)
#
# STEP 4'S NEEDS, and why each is a refusal rather than a warning:
#   * an ARAnyM WITH ethernet. Ubuntu 26.04's aranym has none: its configure's
#     TUN/TAP probe calls memset without <string.h>, gcc 14 rejects the implicit
#     declaration, and ethernet is compiled out in silence -- no error, just no
#     eth0 in the guest (measured 2026-09-24; the package ships no aratapif for
#     the same reason). ARAnyM 1.1.0 from its own source, configured with
#     `--enable-fullmmu --enable-ethernet ac_cv_tun_tap_support=yes`, has it.
#     Point TIER_V_ARANYM at that binary; the rig reads its strings for TunTap.
#   * a tap device this user owns, carrying the host's end of the link. The
#     operator creates it once, with sudo, and it lasts until reboot:
#       ip tuntap add dev tap0 mode tap user $USER
#       ip addr add 10.254.254.1/24 dev tap0 ; ip link set tap0 up
#     ARAnyM's `bridge` mode then only OPENS it, so nothing here is setuid --
#     `ptp` mode would need aratapif setuid root, and bridge does not.
#   * a firewall that lets the guest reach the host's end, on that port only
#     (with ufw: `ufw allow in on tap0 to 10.254.254.1 port 1234 proto tcp`).
#   * --live-port and --live-model: without them the step is announced as not
#     attempted (jc_rig_live_skip) and refused, since it is nothing but the task.
# The model server is used where it listens: if it already answers on every
# address, the guest reaches it at the tap's host end; if it is loopback-bound,
# the rig forwards that one port from the tap address, for the run's lifetime.
#
# Exit codes (the tier-v contract):
#   0  the step ran and every check passed
#   1  the step ran and something failed -- a RESULT, read results-freemint.txt
#   2  usage / missing tool on the host / a pinned download that does not verify
#   3  the guest never wrote THIS run's sentinel -- NOT a result, the rig failed
#
# Usage:
#   scripts/tier-v-freemint.sh --step 1
#   scripts/tier-v-freemint.sh --step 2 [--dirty]     # --dirty: the WORKING tree
#   scripts/tier-v-freemint.sh --dry-run --step 2
#   TIER_V_ARANYM=/path/to/aranym scripts/tier-v-freemint.sh --step 4 \
#       --live-port 1234 --live-model prism-ml/bonsai-27b
set -u
. "$(dirname "$0")/_rig_ship.sh"
. "$(dirname "$0")/_rig_live.sh"

REPO=$(cd "$(dirname "$0")/.." && pwd)
TIER_V_DIR=${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}
DL="$TIER_V_DIR/freemint-dl"

FM_HASH=8d1bbb93
FM_ZIP="freemint-1-19-$FM_HASH-aranym.zip"
FM_URL="https://atari.joska.no/snapshots/freemint/bootable/$FM_ZIP"
FM_SHA=783498b6dbc5f80fe023bf627ef30102196bb18582650f04556a3fe0ba242981
FM_DIR=aranym-1-19-8d1
ET_ZIP=emutos-512k-1.4.zip
ET_URL="https://downloads.sourceforge.net/project/emutos/emutos/1.4/$ET_ZIP"
ET_SHA=1ef7bd25f61bcfc66d19debc2f0ebb0d5e5ba811ccd7f0fddfcb105bb72d1663
ET_IMG=etos512us.img
ET_IMG_SHA=167f5f148419a684a3646519ef12cd7be00e1a35e10040358184f4d471e27da9
EMU=aranym-mmu

STEP=''
DIRTY=0
DRY=0
DEADLINE=''
LIVE_PORT=''
LIVE_MODEL=''
TAP=${TIER_V_TAP:-tap0}
TAP_HOST=${TIER_V_TAP_HOST:-10.254.254.1}
TAP_GUEST=${TIER_V_TAP_GUEST:-10.254.254.2}
while [ $# -gt 0 ]; do
    case "$1" in
        --live-port)  shift; LIVE_PORT="${1:-}" ;;
        --live-model) shift; LIVE_MODEL="${1:-}" ;;
        --step)     shift; STEP="${1:-}" ;;
        --dirty)    DIRTY=1 ;;
        --dry-run)  DRY=1 ;;
        --deadline) shift; DEADLINE="${1:-}" ;;
        -h|--help)  sed -n '2,60p' "$0"; exit 0 ;;
        *) echo "tier-v-freemint: unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
case "$STEP" in
    1) : "${DEADLINE:=120}" ;;   # measured 9-12 s; ten times that
    2) : "${DEADLINE:=300}" ;;   # measured 23 s in the emulator; over ten times that
    4) : "${DEADLINE:=900}" ;;   # measured 17 s; two model calls, so a slow model's margin
    *) echo "tier-v-freemint: --step 1, 2 or 4 is required" >&2; exit 2 ;;
esac
note() { printf '%s\n' "$*"; }
ok() { printf 'ok - %s\n' "$*"; }
bad() { printf 'not ok - %s\n' "$*"; }
[ "$STEP" = 4 ] && EMU=${TIER_V_ARANYM:-$EMU}
if [ "$STEP" = 4 ] && { [ -z "$LIVE_PORT" ] || [ -z "$LIVE_MODEL" ]; }; then
    jc_rig_live_skip
    echo "tier-v-freemint: step 4 IS the live task -- it needs --live-port and --live-model" >&2
    exit 2
fi

say() { printf 'tier-v-freemint: %s\n' "$*"; }

if [ "$DRY" = 1 ]; then
    say "step $STEP ($(jc_rig_ship_label "$DIRTY")), deadline ${DEADLINE}s, under $TIER_V_DIR"
    say "would fetch and verify $FM_ZIP ($FM_SHA) and $ET_ZIP ($ET_SHA)"
    say "would write config.rig (no D:, E: case-sensitive, $([ "$STEP" = 4 ] && echo "[ETH0] bridge on $TAP" || echo 'no network')), hook mint.cnf, build the halt helper"
    [ "$STEP" = 2 ] && say "would cross-build run_tests and jichi with m68k-atari-mint-gcc -m68020-60 HAVE_CURL= WERROR=1"
    [ "$STEP" = 4 ] && say "would build a TLS-free libcurl for MiNT (scripts/minimal-curl.sh --tls none --target m68k-atari-mint), cross-build jichi against it, bring up [ETH0] in bridge mode on $TAP ($TAP_HOST <-> $TAP_GUEST), and run the driven task against $LIVE_MODEL on port $LIVE_PORT with $EMU"
    say "would run: SDL_VIDEODRIVER=dummy timeout $DEADLINE $EMU -c config.rig -N"
    exit 0
fi

for t in "$EMU" curl unzip sha256sum timeout awk sed; do
    command -v "$t" > /dev/null 2>&1 || { echo "tier-v-freemint: missing on the host: $t" >&2; exit 2; }
done
command -v m68k-atari-mint-gcc > /dev/null 2>&1 ||
    { echo "tier-v-freemint: missing m68k-atari-mint-gcc (the halt helper needs it in every step)" >&2; exit 2; }
if [ "$STEP" = 4 ]; then
    command -v strings > /dev/null 2>&1 && command -v ip > /dev/null 2>&1 ||
        { echo "tier-v-freemint: step 4 needs strings and ip on the host" >&2; exit 2; }
    strings "$(command -v "$EMU")" 2>/dev/null | grep -q 'TunTap' || {
        echo "tier-v-freemint: $EMU has no ethernet (no TunTap code): the guest would get no eth0." >&2
        echo "  Build ARAnyM 1.1.0 with --enable-fullmmu --enable-ethernet ac_cv_tun_tap_support=yes" >&2
        echo "  and set TIER_V_ARANYM to it (the header says why Ubuntu's build lacks it)." >&2
        exit 2; }
    ip -brief addr show "$TAP" 2>/dev/null | grep -q "$TAP_HOST/" || {
        echo "tier-v-freemint: no $TAP carrying $TAP_HOST -- the operator creates it once, with sudo:" >&2
        echo "  ip tuntap add dev $TAP mode tap user $(id -un); ip addr add $TAP_HOST/24 dev $TAP; ip link set $TAP up" >&2
        exit 2; }
    MINTCURL=${MINCURL_DIR:-$HOME/.cache/jichi-mincurl}/prefix-none-m68k-atari-mint
    if [ ! -f "$MINTCURL/lib/libcurl.a" ]; then
        say "building a TLS-free libcurl for MiNT (scripts/minimal-curl.sh --tls none --target m68k-atari-mint)"
        sh "$REPO/scripts/minimal-curl.sh" --tls none --target m68k-atari-mint > "$TIER_V_DIR/mintcurl-build.log" 2>&1 ||
            { echo "tier-v-freemint: the MiNT libcurl did not build (see $TIER_V_DIR/mintcurl-build.log)" >&2; exit 2; }
    fi
fi

# fetch URL FILE SHA -- download once into $DL, and refuse anything that does not verify
fetch() {
    if [ ! -f "$DL/$2" ]; then
        mkdir -p "$DL" && curl -fsSL --max-time 300 -o "$DL/$2.part" "$1" && mv "$DL/$2.part" "$DL/$2" ||
            { echo "tier-v-freemint: download failed: $1" >&2; exit 2; }
    fi
    got=$(sha256sum "$DL/$2" | cut -d' ' -f1)
    [ "$got" = "$3" ] || { echo "tier-v-freemint: $2 is $got, pinned $3 -- refusing it" >&2; exit 2; }
}
fetch "$FM_URL" "$FM_ZIP" "$FM_SHA"
fetch "$ET_URL" "$ET_ZIP" "$ET_SHA"

RID="fm-$(date +%s)-$$"
RUN="$TIER_V_DIR/freemint-$(date +%Y%m%d-%H%M%S)-step$STEP"
mkdir -p "$RUN/share" "$RUN/unzip" || exit 2
RES="$RUN/results-freemint.txt"
log() { printf '%s\n' "$*" >> "$RES"; }
say "run $RID in $RUN"
unzip -q -o "$DL/$FM_ZIP" -d "$RUN/unzip" && unzip -q -o "$DL/$ET_ZIP" -d "$RUN/unzip" || exit 2
G="$RUN/guest"
mv "$RUN/unzip/$FM_DIR" "$G" || exit 2
IMG=$(find "$RUN/unzip" -name "$ET_IMG" | head -n 1)
[ -n "$IMG" ] && [ "$(sha256sum "$IMG" | cut -d' ' -f1)" = "$ET_IMG_SHA" ] ||
    { echo "tier-v-freemint: $ET_IMG missing or not the pinned image" >&2; exit 2; }
cp "$IMG" "$G/emutos/$ET_IMG"

# The rig's own configuration: the archive's, with every setting above replaced.
NET=none
[ "$STEP" = 4 ] && NET=bridge
awk -v share="$RUN/share" -v img="emutos/$ET_IMG" -v net="$NET" -v th="$TAP_HOST" \
    -v tg="$TAP_GUEST" -v tap="$TAP" '
    /^\[/ { sec = $0 }
    sec == "[GLOBAL]"  && /^EmuTOS *=/       { print "EmuTOS = " img; next }
    sec == "[HOSTFS]"  && /^D *=/            { print "D = "; next }
    sec == "[HOSTFS]"  && /^E *=/            { print "E = " share ":"; next }
    sec == "[STARTUP]" && /^GrabMouse *=/    { print "GrabMouse = No"; next }
    sec == "[AUDIO]"   && /^Enabled *=/      { print "Enabled = No"; next }
    sec == "[ETH0]"    && /^Type *=/         { print "Type = " net; next }
    sec == "[ETH0]"    && /^Tunnel *=/       { print "Tunnel = " tap; next }
    sec == "[ETH0]"    && /^HostIP *=/       { print "HostIP = " th; next }
    sec == "[ETH0]"    && /^AtariIP *=/      { print "AtariIP = " tg; next }
    sec == "[ETH0]"    && /^Netmask *=/      { print "Netmask = 255.255.255.0"; next }
    { print }' "$G/config" > "$G/config.rig" || exit 2
for want in "EmuTOS = emutos/$ET_IMG" "D = " "E = $RUN/share:" "Type = $NET" "GrabMouse = No"; do
    grep -qxF "$want" "$G/config.rig" || { echo "tier-v-freemint: config.rig lacks '$want'" >&2; exit 2; }
done

# The boot hook: no network script, no desktop -- the rig's script instead.
CNF="$G/drive_c/mint/1-19-8d1/mint.cnf"
# Step 4 keeps the archive's eth0-config.sh: it reads the addresses above back
# through nfeth-config and brings eth0 up with the default route to the host.
NETLINE='s#^exec u:/bin/bash u:/bin/eth0-config.sh#\# rig: no network#'
[ "$STEP" = 4 ] && NETLINE='s#^\(exec u:/bin/bash u:/bin/eth0-config.sh\)$#\1#'
sed -e "$NETLINE" \
    -e 's#^GEM=\${SYSDIR}xaaes/xaloader.prg#exec u:/bin/bash u:/c/rig/boot.sh#' "$CNF" > "$CNF.rig" &&
    mv "$CNF.rig" "$CNF" || exit 2
grep -q '^exec u:/bin/bash u:/c/rig/boot.sh$' "$CNF" || { echo "tier-v-freemint: mint.cnf hook not written" >&2; exit 2; }
# bash ships and sh does not; jichi and its tests run `sh -c`.
[ -e "$G/drive_c/mint/1-19-8d1/sys-root/bin/sh" ] || ln -s bash "$G/drive_c/mint/1-19-8d1/sys-root/bin/sh"

mkdir -p "$G/drive_c/rig"
cat > "$RUN/mint_halt.c" <<'C'
/* mint_halt.c -- ask the FreeMiNT kernel to halt, so ARAnyM exits (tier-v-freemint.sh). */
#include <mint/mintbind.h>
#include <stdio.h>
int main(void)
{
    printf("mint_halt: Shutdown(0)\n");
    fflush(stdout);
    (void)Shutdown(0L);
    return 1;
}
C
m68k-atari-mint-gcc -std=c89 -O2 "$RUN/mint_halt.c" -o "$G/drive_c/rig/mint_halt.ttp" > "$RUN/halt-build.log" 2>&1 ||
    { echo "tier-v-freemint: the halt helper did not build (see $RUN/halt-build.log)" >&2; exit 2; }

log "run:      $RID"
log "step:     $STEP"
if [ "$STEP" = 2 ] || [ "$STEP" = 4 ]; then
    jc_rig_ship_stamp "$REPO" "$DIRTY" >> "$RES"
else
    log "tree: none shipped (step 1 boots the image and halts)"
fi
log "guest:    FreeMiNT $FM_ZIP (sha256 $FM_SHA), EmuTOS 1.4 $ET_IMG, $($EMU --version 2>&1 | head -n 1)"

if [ "$STEP" = 2 ]; then
    say "cross-building run_tests and jichi ($(jc_rig_ship_label "$DIRTY"))"
    mkdir -p "$RUN/src" && jc_rig_ship_tar "$REPO" "$DIRTY" | tar -C "$RUN/src" -xf - || exit 2
    (cd "$RUN/src" && make CC='m68k-atari-mint-gcc -m68020-60' HAVE_CURL= WERROR=1 run_tests jichi \
        > "$RUN/cross-build.log" 2>&1) ||
        { log "cross-build: FAILED (cross-build.log)"; say "cross-build failed"; exit 1; }
    cp "$RUN/src/run_tests" "$RUN/src/jichi" "$RUN/share/" || exit 2
    log "cross-build: ok, run_tests $(wc -c < "$RUN/share/run_tests") B, jichi $(wc -c < "$RUN/share/jichi") B"
fi
if [ "$STEP" = 4 ]; then
    say "cross-building jichi with libcurl for MiNT ($(jc_rig_ship_label "$DIRTY"))"
    mkdir -p "$RUN/src" && jc_rig_ship_tar "$REPO" "$DIRTY" | tar -C "$RUN/src" -xf - || exit 2
    (cd "$RUN/src" && make CC='m68k-atari-mint-gcc -m68020-60' CURL_CFLAGS="-I$MINTCURL/include" \
        CURL_PC_LIBS="-L$MINTCURL/lib -lcurl" WERROR=1 jichi > "$RUN/cross-build.log" 2>&1) ||
        { log "cross-build: FAILED (cross-build.log)"; say "cross-build failed"; exit 1; }
    grep -q -- '-DJC_HAVE_CURL' "$RUN/cross-build.log" ||
        { log "cross-build: jichi built WITHOUT libcurl -- the live task cannot run"; say "no libcurl in the build"; exit 1; }
    cp "$RUN/src/jichi" "$RUN/share/" || exit 2
    log "cross-build: ok, jichi with libcurl $("$MINTCURL/bin/curl-config" --version), $(wc -c < "$RUN/share/jichi") B"
    # The task, from the one definition: the config names the host's end of the
    # link, and the fixture's phrase is minted now.
    jc_rig_live_config "$LIVE_MODEL" "http://$TAP_HOST:$LIVE_PORT/v1" > "$RUN/share/live.json"
    PHRASE=$(jc_rig_live_phrase TIER-M)
    mkdir -p "$RUN/share/ws" && jc_rig_live_fixture "$PHRASE" > "$RUN/share/ws/note.txt"
    P_WIRE=$(jc_rig_live_prompt_wire)
    P_TOOL=$(jc_rig_live_prompt_tool)
    log "live:     $LIVE_MODEL at http://$TAP_HOST:$LIVE_PORT/v1 over $TAP; phrase $PHRASE"
fi

# The guest's script. TMPDIR=/tmp because mint.cnf's u:/tmp defeats the guest's own
# `mkdir -p` ("cannot create directory 'u:'"), measured in M726.
{
    printf '#!/bin/bash\n'
    printf 'export TMPDIR=/tmp\n'
    printf 'echo "RIG START run=%s" > /dev/nfstderr\n' "$RID"
    if [ "$STEP" = 2 ]; then
        printf 'cd /e && ./run_tests > /e/run_tests.out 2>&1; echo "rc=$?" > /e/run_tests.rc\n'
        printf './jichi --version > /e/jichi-version.txt 2>&1; echo "rc=$?" >> /e/jichi-version.txt\n'
        printf './jichi describe > /e/jichi-describe.txt 2>&1; echo "rc=$?" > /e/jichi-describe.rc\n'
    fi
    if [ "$STEP" = 4 ]; then
        printf 'ifconfig eth0 > /e/eth0.txt 2>&1\n'
        printf 'cd /e && ./jichi --config /e/live.json --prompt-b64 %s --output json > /e/live.txt 2>&1; echo "rc=$?" > /e/live.rc\n' "$P_WIRE"
        printf 'cd /e/ws && /e/jichi --config /e/live.json --auto -q --prompt-b64 %s > /e/live-tool.txt 2>&1; echo "rc=$?" > /e/live-tool.rc\n' "$P_TOOL"
    fi
    printf 'echo "RIG DONE run=%s" > /e/sentinel.txt\n' "$RID"
    printf 'echo "RIG DONE run=%s" > /dev/nfstderr\n' "$RID"
    printf '/c/rig/mint_halt.ttp > /e/halt.txt 2>&1\n'
} > "$G/drive_c/rig/boot.sh"

FWD=''
if [ "$STEP" = 4 ]; then
    if ss -ltn 2>/dev/null | grep -q -e "0.0.0.0:$LIVE_PORT " -e "$TAP_HOST:$LIVE_PORT "; then
        log "model:    the server already listens where the guest can reach it; no forwarder"
    else
        command -v python3 > /dev/null 2>&1 || { echo "tier-v-freemint: a loopback-bound server needs python3 for the forwarder" >&2; exit 2; }
        python3 - "$TAP_HOST" "$LIVE_PORT" > "$RUN/forwarder.log" 2>&1 <<'PY' &
import socket, sys, threading
host, port = sys.argv[1], int(sys.argv[2])
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((host, port))                 # the tap address ONLY, never 0.0.0.0
srv.listen(8)
def pump(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d:
                break
            b.sendall(d)
    except OSError:
        pass
    for x in (a, b):
        try:
            x.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
while True:
    c, _ = srv.accept()
    u = socket.create_connection(("127.0.0.1", port))
    threading.Thread(target=pump, args=(c, u), daemon=True).start()
    threading.Thread(target=pump, args=(u, c), daemon=True).start()
PY
        FWD=$!
        log "model:    loopback-bound; forwarding $TAP_HOST:$LIVE_PORT for this run (pid $FWD)"
    fi
fi
say "booting FreeMiNT under $EMU (deadline ${DEADLINE}s)"
t0=$(date +%s)
(cd "$G" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout "$DEADLINE" "$EMU" -c config.rig -N \
    < /dev/null > "$RUN/aranym.out" 2> "$RUN/aranym.err")
erc=$?
secs=$(( $(date +%s) - t0 ))
[ -n "$FWD" ] && kill "$FWD" 2>/dev/null
log "emulator: rc=$erc after ${secs}s"

# The guest may end lines with CR; a whole-line match must not miss a real sentinel.
if ! tr -d '\r' < "$RUN/share/sentinel.txt" 2>/dev/null | grep -qxF "RIG DONE run=$RID"; then
    log "sentinel: MISSING for $RID -- the rig failed; see aranym.err"
    say "no sentinel for $RID after ${secs}s (emulator rc=$erc): not a result -- see $RUN/aranym.err"
    tr -d '\000' < "$RUN/aranym.err" | grep -E 'ERROR|not supported' | head -n 3 >&2
    exit 3
fi
log "sentinel: ok ($RID)"

if [ "$STEP" = 1 ]; then
    say "step 1 ok: booted, wrote the sentinel and halted in ${secs}s"
    exit 0
fi
if [ "$STEP" = 4 ]; then
    # jc_rig_live's two assertions, read off the share: the wire turn exits 0
    # with an answer, and the tool turn reports the phrase only the file holds.
    live_ok=0 tool_ok=0
    if tr -d '\r' < "$RUN/share/live.rc" 2>/dev/null | grep -qx 'rc=0' &&
       grep -q '"text"' "$RUN/share/live.txt" 2>/dev/null; then
        ok "live turn answered from FreeMiNT over $TAP ($LIVE_MODEL)" >> "$RES"; live_ok=1
    else
        bad "live turn did not answer -- see live.txt" >> "$RES"
    fi
    if grep -q "$PHRASE" "$RUN/share/live-tool.txt" 2>/dev/null; then
        ok "agentic turn: the model called a tool and reported $PHRASE" >> "$RES"; tool_ok=1
    else
        bad "agentic turn did NOT return $PHRASE -- see live-tool.txt" >> "$RES"
    fi
    log "eth0:     $(tr -d '\r\000' < "$RUN/share/eth0.txt" 2>/dev/null | grep -o 'inet [0-9.]*' | head -n 1)"
    say "step 4: wire $live_ok, tool $tool_ok ($PHRASE) in ${secs}s -- $RES"
    [ "$live_ok" = 1 ] && [ "$tool_ok" = 1 ] && exit 0
    exit 1
fi

summary=$(tr -d '\r\000' < "$RUN/share/run_tests.out" | grep -E '^[0-9]+ checks, [0-9]+ failures' | tail -n 1)
log "run_tests: ${summary:-no summary line} ($(cat "$RUN/share/run_tests.rc" 2>/dev/null))"
tr -d '\r\000' < "$RUN/share/run_tests.out" | grep -E '^  FAIL ' >> "$RES"
log "jichi --version: $(tr -d '\r' < "$RUN/share/jichi-version.txt" | tr '\n' ' ')"
log "jichi describe: $(tr -d '\r' < "$RUN/share/jichi-describe.rc" 2>/dev/null || echo 'no rc'), $(wc -c < "$RUN/share/jichi-describe.txt" 2>/dev/null || echo 0) B"
tr -d '\000' < "$RUN/aranym.err" | grep 'BUS ERROR' | head -n 1 >> "$RES"
say "step 2: ${summary:-no summary} -- $RES"
case "$summary" in
    *" 0 failures") exit 0 ;;
    *) exit 1 ;;
esac
