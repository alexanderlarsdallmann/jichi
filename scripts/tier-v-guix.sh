#!/bin/sh
# tier-v-guix.sh -- the Guix System row: the PUBLISHED Guix System VM image, headless.
#
# WHY THIS EXISTS. M468 booted the published `guix-system-vm-image` and ran jichi's gates
# in it, but a person had to type at the desktop: `sendkey` from the QEMU monitor dropped
# keys in GRUB's editor at every pacing tried, and the image has no sshd. On 2026-09-24
# (M738) the same image was driven with no one at the keyboard, and this is the
# transcript of that run: with `-nographic` GRUB reads its input from the SERIAL line,
# and an EDIT of the entry there -- arrow keys, then fourteen characters -- adds
# `console=ttyS0` to the kernel line, where a typed command line lost a token in 2026-08
# (docs/analysis/2026-08-17-driving-the-published-guix-image.md). Guix's base services
# then start a login on whatever console the kernel names.
# `guest` logs in with no password, as the image intends.
#
# WHAT IT UNIQUELY EXERCISES: a non-FHS distribution (packages in /gnu/store, no /usr
# worth the name), a toolchain from the store -- gcc and libcurl as Guix builds them --
# and Guix's own kernel, none of which another row has. Guix ships neither `cc` nor
# `c99`, so every make here names CC=gcc (M458).
#
# WHAT IT DOES. An overlay on the image with NO size argument (M468: `qemu-img info`
# rounds, and an overlay given the rounded size clipped the last partition), a 9p share
# holding `git archive` of the tree and the driven task's files, then in the guest:
# mount the share, wait for the network, `guix shell` a toolchain, build with WERROR=1,
# run the unit suite, and -- with --live-port -- the two turns of scripts/_rig_live.sh,
# against the host's model server through QEMU's host alias 10.0.2.2. QEMU's user-mode
# network reaches the host's loopback, so the server can stay bound to 127.0.0.1.
#
# Usage:
#   scripts/tier-v-guix.sh --image PATH/guix-system-vm-image-1.5.0.x86_64-linux.qcow2 \
#       [--rev REV] [--live-port 1234 --live-model prism-ml/bonsai-27b]
#   scripts/tier-v-guix.sh --dry-run [...]   # the plan, and what a real run would refuse
#
# Wants: qemu-system-x86_64 with KVM, qemu-img, python3 with pexpect (the serial
# console is a conversation, and pexpect is the tool for one), and the image, which the
# operator downloads (https://guix.gnu.org/en/download/) -- this script fetches nothing.
# Results: $TIER_V_DIR/guix-<stamp>/results-guix.txt, never the tree.
#
# Exit: 0 every check passed; 1 a check failed (a RESULT); 2 usage or a missing tool;
#       3 the guest never gave a shell (not a result: the rig failed).
set -u
. "$(dirname "$0")/_rig_live.sh"

REPO=$(cd "$(dirname "$0")/.." && pwd)
TIER_V_DIR=${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}
IMAGE='' REV=HEAD LIVE_PORT='' LIVE_MODEL='' DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --image)      IMAGE=${2:?}; shift 2 ;;
        --rev)        REV=${2:?}; shift 2 ;;
        --live-port)  LIVE_PORT=${2:?}; shift 2 ;;
        --live-model) LIVE_MODEL=${2:?}; shift 2 ;;
        --dry-run)    DRY=1; shift ;;
        -h|--help)    awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        *) echo "tier-v-guix: unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
done
note() { printf '%s\n' "$*"; }
ok() { printf 'ok - %s\n' "$*"; }
bad() { printf 'not ok - %s\n' "$*"; }
# Every refusal is collected, so a dry run can list all of them and touch nothing;
# a real run stops on the list.
WHY=''
refuse() { WHY="$WHY
  - $*"; }
[ -n "$IMAGE" ] && [ -f "$IMAGE" ] || refuse "--image names no file: '$IMAGE'"
case "$LIVE_PORT:$LIVE_MODEL" in
    :?*|?*:) refuse "--live-port and --live-model go together (one alone would skip the turns in silence)" ;;
esac
for t in qemu-system-x86_64 qemu-img python3 git; do
    command -v "$t" > /dev/null 2>&1 || refuse "missing on the host: $t"
done
python3 -c 'import pexpect' 2> /dev/null || refuse "python3 needs pexpect for the serial console"
[ -e /dev/kvm ] || refuse "no /dev/kvm -- under TCG a boot takes minutes, and nothing here says so"
git -C "$REPO" rev-parse --verify -q "$REV^{commit}" > /dev/null || refuse "no commit $REV"
if [ "$DRY" = 1 ]; then
    echo "tier-v-guix: DRY RUN -- nothing is booted, built or driven"
    echo "  image   : ${IMAGE:-(none given)}"
    echo "  rev     : $REV ($(git -C "$REPO" rev-parse --short "$REV" 2> /dev/null || echo unknown))"
    echo "  results : $TIER_V_DIR/guix-<stamp>/results-guix.txt"
    echo "  guest   : GRUB over serial + console=ttyS0, login guest, 9p share, guix shell;"
    echo "            WERROR=1 build with CC=gcc, then the unit suite"
    if [ -n "$LIVE_PORT" ] && [ -n "$LIVE_MODEL" ]; then
        echo "  live    : both turns of scripts/_rig_live.sh, $LIVE_MODEL at 10.0.2.2:$LIVE_PORT"
    else
        echo "  live    : not attempted (no --live-port/--live-model)"
    fi
    [ -z "$WHY" ] || printf '  a real run would refuse:%s\n' "$WHY"
    exit 0
fi
[ -z "$WHY" ] || { printf 'tier-v-guix: refusing:%s\n' "$WHY" >&2; exit 2; }

RUN="$TIER_V_DIR/guix-$(date +%Y%m%d-%H%M%S)"
SH="$RUN/share"
RES="$RUN/results-guix.txt"
mkdir -p "$SH/ws" || exit 2
qemu-img create -q -f qcow2 -F qcow2 -b "$IMAGE" "$RUN/overlay.qcow2" || exit 2
git -C "$REPO" archive --format=tar --prefix=jichi/ "$REV" > "$SH/jichi.tar" || exit 2
{
    echo "# tier-v-guix: $REV ($(git -C "$REPO" rev-parse --short "$REV")), image $(basename "$IMAGE")"
    echo "# host: $(uname -sr), $(qemu-system-x86_64 --version | head -n 1)"
} > "$RES"
LIVE=0
if [ -n "$LIVE_PORT" ] && [ -n "$LIVE_MODEL" ]; then
    LIVE=1
    jc_rig_live_config "$LIVE_MODEL" "http://10.0.2.2:$LIVE_PORT/v1" > "$SH/live.json"
    PHRASE=$(jc_rig_live_phrase TIER-G)
    jc_rig_live_fixture "$PHRASE" > "$SH/ws/note.txt"
    printf 'PW=%s\nPT=%s\n' "$(jc_rig_live_prompt_wire)" "$(jc_rig_live_prompt_tool)" > "$SH/prompts.env"
else
    jc_rig_live_skip >> "$RES"
    : > "$SH/prompts.env"
fi

# The conversation. Each step waits for the guest's own prompt; a timeout is the rig's
# failure (exit 3), never a result.
python3 - "$RUN" "$LIVE" > "$RUN/console.out" 2>&1 <<'PY'
import pexpect, sys, time
run, live = sys.argv[1], sys.argv[2] == "1"
sh = run + "/share"
log = open(run + "/serial.log", "wb")
p = pexpect.spawn(
    "qemu-system-x86_64 -enable-kvm -m 4096 -smp 4 -nographic "
    "-drive file=%s/overlay.qcow2,if=virtio -nic user,model=virtio-net-pci "
    "-virtfs local,path=%s,mount_tag=host,security_model=none,id=host" % (run, sh),
    timeout=300, logfile=log)
PROMPT = rb"guest@gnu [^\r\n]*\$ "
def say(m):
    print(time.strftime("%H:%M:%S"), m, flush=True)
def run_cmd(c, timeout=120):
    p.sendline(c)
    p.expect(PROMPT, timeout=timeout)
try:
    p.expect(rb"GNU GRUB", timeout=180)
    time.sleep(1)
    p.send("e")
    time.sleep(2)
    # The entry: setparams, search, a `linux` line wrapping over four screen rows,
    # initrd. Three Downs (the arrow's escape sequence -- the 2026-08 attempts used
    # Ctrl-N and landed on `search`) end inside the linux line; Ctrl-E goes to its end.
    for _ in range(3):
        p.send("\x1b[B")
        time.sleep(0.3)
    p.send("\x05")               # Ctrl-E: end of the (logical) line
    time.sleep(0.3)
    p.send(" console=ttyS0")
    time.sleep(0.5)
    p.send("\x18")               # Ctrl-X: boot the edited entry
    say("grub: console=ttyS0 added")
    p.expect(rb"login: ", timeout=300)
    p.sendline("guest")
    p.expect(PROMPT, timeout=60)
    say("logged in")
    run_cmd("sudo mkdir -p /mnt/host && sudo mount -t 9p -o trans=virtio,version=9p2000.L host /mnt/host && echo MOUNTED", 60)
    # The network is NetworkManager's, and it comes up after the login does: wait for
    # the resolver it writes, or `guix shell` finds no substitute server and starts to
    # bootstrap from source (measured: the first attempt did exactly that).
    run_cmd("for i in $(seq 120); do grep -q '^nameserver' /etc/resolv.conf 2>/dev/null && break; sleep 1; done; "
            "grep -c '^nameserver' /etc/resolv.conf > /mnt/host/net.txt", 180)
    run_cmd("rm -rf /tmp/jichi && tar -C /tmp -xf /mnt/host/jichi.tar && echo UNPACKED", 120)
    say("tree unpacked; guix shell, build, unit suite%s" % (", live turns" if live else ""))
    inner = ("uname -srm > /mnt/host/uname.txt; guix describe > /mnt/host/guix.txt 2>&1; "
             "gcc --version | head -1 > /mnt/host/gcc.txt; curl-config --version > /mnt/host/curl.txt; "
             "make CC=gcc WERROR=1 > /mnt/host/build.log 2>&1; echo $? > /mnt/host/build.rc; "
             "make CC=gcc WERROR=1 test > /mnt/host/test.log 2>&1; echo $? > /mnt/host/test.rc")
    if live:
        inner += ("; . /mnt/host/prompts.env; "
                  "./jichi --config /mnt/host/live.json --prompt-b64 $PW --output json > /mnt/host/live.txt 2>&1; "
                  "echo $? > /mnt/host/live.rc; cd /mnt/host/ws && /tmp/jichi/jichi --config /mnt/host/live.json "
                  "--auto -q --prompt-b64 $PT > /mnt/host/live-tool.txt 2>&1; echo $? > /mnt/host/live-tool.rc")
    run_cmd("cd /tmp/jichi && guix shell --no-grafts gcc-toolchain make curl pkg-config coreutils grep sed gawk "
            "findutils diffutils tar gzip bash -- sh -c '%s' > /mnt/host/shell.log 2>&1; echo $? > /mnt/host/shell.rc" % inner, 3600)
    say("guix shell done")
    p.sendline("sudo umount /mnt/host; sudo shutdown now")
    time.sleep(8)
    sys.exit(0)
except pexpect.exceptions.ExceptionPexpect as e:
    say("console step failed: %s" % type(e).__name__)
    sys.exit(3)
finally:
    p.terminate(force=True)
    log.close()
PY
crc=$?
cat "$RUN/console.out" >> "$RES"
[ "$crc" = 0 ] || { echo "tier-v-guix: the guest never finished its script (console rc $crc) -- see $RUN/serial.log" >&2; exit 3; }

rc=0
{
    echo "uname: $(cat "$SH/uname.txt" 2>/dev/null)"
    echo "guix:  $(head -n 1 "$SH/guix.txt" 2>/dev/null | tr -s ' ')"
    echo "gcc:   $(cat "$SH/gcc.txt" 2>/dev/null)  curl: $(cat "$SH/curl.txt" 2>/dev/null)"
} >> "$RES"
if [ "$(cat "$SH/build.rc" 2>/dev/null)" = 0 ]; then ok "WERROR=1 build with the store's gcc" >> "$RES"
else bad "the build failed -- see $SH/build.log" >> "$RES"; rc=1; fi
sum=$(grep -E '^[0-9]+ checks, [0-9]+ failures' "$SH/test.log" 2>/dev/null | tail -n 1)
if [ "$(cat "$SH/test.rc" 2>/dev/null)" = 0 ]; then ok "unit suite: $sum" >> "$RES"
else bad "unit suite: ${sum:-no summary} -- see $SH/test.log" >> "$RES"; rc=1; fi
if [ "$LIVE" = 1 ]; then
    if [ "$(cat "$SH/live.rc" 2>/dev/null)" = 0 ] && grep -q '"text"' "$SH/live.txt" 2>/dev/null; then
        ok "live turn answered from Guix System over QEMU's host alias ($LIVE_MODEL)" >> "$RES"
    else
        bad "live turn did not answer -- see $SH/live.txt" >> "$RES"; rc=1
    fi
    if grep -q "$PHRASE" "$SH/live-tool.txt" 2>/dev/null; then
        ok "agentic turn: the model called a tool and reported $PHRASE" >> "$RES"
    else
        bad "agentic turn did NOT return $PHRASE -- see $SH/live-tool.txt" >> "$RES"; rc=1
    fi
fi
cat "$RES"
exit "$rc"
