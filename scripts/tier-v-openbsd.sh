#!/bin/sh
# tier-v-openbsd.sh -- the OpenBSD row, from nothing to a result (M461).
#
# WHY THIS EXISTS SEPARATELY FROM tier-v-bsd.sh. That rig boots a FreeBSD
# cloud image: the vendor ships a qcow2 with cloud-init, so provisioning is a
# seed ISO and the VM comes up with sshd and your key already in place.
# OpenBSD ships no cloud image and has no cloud-init. What it has instead is
# autoinstall(8), which is better in one way and worse in another: it can do a
# genuinely unattended install from the 11 MB network-install ISO, but it has
# to be TOLD to, over the serial console, by something typing at it.
#
# So this rig does three things tier-v-bsd.sh never has to:
#
#   1. drives the boot loader to move the console to com0 (the loader speaks to
#      serial by default; the KERNEL does not, so without `set tty com0` the
#      installer's questions go to a VGA nobody is looking at);
#   2. answers the (I)nstall/(A)utoinstall prompt;
#   3. serves the response file over HTTP from the host, because QEMU's
#      built-in DHCP gives the guest no next-server to find one at.
#
# WHAT OPENBSD IS WORTH MEASURING FOR (its axis, not FreeBSD's):
#   * a SECOND non-Linux libc -- which is what proved FreeBSD's fixes were
#     portable rather than fitted to one platform: the build was clean first
#     try, with no further changes at all;
#   * /bin/sh is **ksh**, not an ash derivative, so ~200 POSIX-sh smoke drivers
#     run under a shell implementation nothing else in the matrix exercises;
#   * a grep, sed and head from a different lineage than GNU's, which is how
#     five lint defects and one product defect (`search_code` returning
#     "(no matches)" for everything) were found.
#
# HOST REQUIREMENTS: qemu-system-x86_64 with KVM, curl, and python3 -- the last
# ONLY as a five-second HTTP server for the response file. Nothing python
# touches the guest or the result; the in-guest gate is the usual python-free
# `gmake test` + `gmake smoke`.
#
# Usage (--ref-secs is REQUIRED: it is THIS bench's serial `make WERROR=1`
# seconds, the denominator of JC_SMOKE_TIMEOUT_MULT. Measure it with
# `make clean && time make WERROR=1`, median of three; never copy a published
# row's multiplier -- see docs/SESSION_RUNBOOK.md §5):
#   scripts/tier-v-openbsd.sh --ref-secs 4.38 # full row
#   scripts/tier-v-openbsd.sh --dry-run       # print the plan, touch nothing
#   scripts/tier-v-openbsd.sh --keep          # leave the VM up afterwards
#   scripts/tier-v-openbsd.sh --release 7.8   # a different release
#   scripts/tier-v-openbsd.sh --reuse         # skip the install, boot the disk
#   scripts/tier-v-openbsd.sh --dirty         # ship the WORKING tree, not HEAD:
#                                             # the only way to verify a portability
#                                             # fix before committing it
#
# Exit codes, matching the other tier-V rigs:
#   0 the row ran (read the RESULT lines for its verdict)
#   1 the row ran and something under test failed
#   2 the rig could not start (no qemu, download failed, ...)
#   3 never reached userspace -- a RIG failure, not a result about jichi
set -u

# The one implementation of JC_SMOKE_TIMEOUT_MULT, and why it is not inline here.
. "$(dirname "$0")/_rig_mult.sh"

REL="${TIER_V_OPENBSD_RELEASE:-7.9}"
DIR="${TIER_V_OPENBSD_DIR:-$HOME/.cache/jichi-tier-v-openbsd}"
PORT="${TIER_V_OPENBSD_PORT:-2222}"
HTTP_PORT="${TIER_V_OPENBSD_HTTP_PORT:-8088}"
REF_SECS="${JC_REF_SECS:-}"
MEM=2048
SMP=2
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
        *) echo "tier-v-openbsd: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done

# Refuse before doing ten minutes of unattended install work, not after: a row
# whose denominator is unknown cannot be compared with any other row, which is the
# entire point of measuring it. --dry-run is exempt so the steps can be read.
[ "$DRY" -eq 1 ] || jc_rig_ref_or_die "tier-v-openbsd" "$REF_SECS" || exit 2

REPO=$(cd "$(dirname "$0")/.." && pwd)
RELNUM=$(printf '%s' "$REL" | tr -d '.')
ISO="$DIR/cd$RELNUM.iso"
URL="https://cdn.openbsd.org/pub/OpenBSD/$REL/amd64/cd$RELNUM.iso"
DISK="$DIR/openbsd-$RELNUM.qcow2"
KEY="$DIR/tierv-openbsd"
# Results go in $DIR, never the repo: a rig that can dirty the tree it tests
# has already been three separate mistakes in this campaign.
RESULTS="$DIR/results-openbsd.txt"
SSH_OPTS="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8"

N_OK=0; N_FAIL=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));   echo "ok - $*";     echo "ok   - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL - $*" >> "$RESULTS"; }
note() { echo "$*" >> "$RESULTS"; }
# shellcheck disable=SC2086
g()  { ssh $SSH_OPTS -i "$KEY" -p "$PORT" tierv@127.0.0.1 "$@"; }
# shellcheck disable=SC2086
gr() { ssh $SSH_OPTS -i "$KEY" -p "$PORT" root@127.0.0.1 "$@"; }

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-openbsd: DRY RUN"
    echo "  release : OpenBSD $REL amd64"
    echo "  iso     : $URL"
    echo "  cache   : $DIR"
    echo "  vm      : -m $MEM -smp $SMP, ssh on 127.0.0.1:$PORT"
    echo "  http    : 127.0.0.1:$HTTP_PORT (response file only, host-side)"
    echo "  results : $RESULTS"
    echo "  steps   : fetch iso -> autoinstall over serial -> boot -> pkg_add"
    echo "            -> ship -> gmake WERROR=1 -> test -> smoke"
    echo "  NOTE    : OpenBSD's make is BSD make; the row uses gmake deliberately."
    echo "  NOTE    : /bin/sh is ksh here -- that is the axis this row adds."
    exit 0
fi

for t in qemu-system-x86_64 curl python3 qemu-img; do
    command -v "$t" >/dev/null 2>&1 || {
        echo "tier-v-openbsd: $t not found (host requirement)" >&2; exit 2; }
done
[ -w /dev/kvm ] || echo "tier-v-openbsd: warning: /dev/kvm not writable; this will be slow" >&2

mkdir -p "$DIR"
: > "$RESULTS"
note "# tier-v-openbsd row: OpenBSD $REL amd64"
note "# date        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# host        : $(uname -srm)"
note "# revision    : $(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
note ""

# ------------------------------------------------------------ 1. the media
if [ "$REUSE" -eq 0 ]; then
    say "stage cd$RELNUM.iso (~11 MB: the network installer, sets come from the CDN)"
    if [ ! -f "$ISO" ]; then
        curl -fL --no-progress-meter --retry 3 -o "$ISO.part" "$URL" || {
            echo "tier-v-openbsd: download failed: $URL" >&2; exit 2; }
        mv "$ISO.part" "$ISO"
    fi
    ok "iso staged: $(du -h "$ISO" | cut -f1)"
fi

[ -f "$KEY" ] || ssh-keygen -q -t ed25519 -N "" -C tier-v-openbsd -f "$KEY"

# ------------------------------------------------- 2. the autoinstall answers
# Every line here is an answer to a question autoinstall(8) documents. The root
# password is literally disabled (13 asterisks, autoinstall's own idiom) rather
# than set to something throwaway: the key is the only way in, so there is no
# password to leak in a repo or a process listing.
mkdir -p "$DIR/www"
{
    echo "System hostname = obsdrow"
    echo "Password for root = *************"
    echo "Public ssh key for root account = $(cat "$KEY.pub")"
    echo "Allow root ssh login = prohibit-password"
    # The installed system keeps the serial console, so --keep leaves a VM you
    # can still drive headlessly if ssh ever fails to come up.
    echo "Change the default console to com0 = yes"
    echo "Which speed should com0 use = 115200"
    echo "Setup a user = no"
    echo "Do you expect to run the X Window System = no"
    echo "What timezone are you in = UTC"
    echo "Which disk is the root disk = sd0"
    echo "Use (W)hole disk = whole"
    echo "Use (A)uto layout = auto"
    echo "Location of sets = http"
    echo "HTTP Server = cdn.openbsd.org"
    # -x* -game*: X and the games set are ~300 MB this row never touches. comp*
    # is NOT dropped -- it carries the compiler and headers, i.e. the point.
    echo "Set name(s) = -x* -game*"
    echo "Continue without verification = yes"
} > "$DIR/www/install.conf"
ok "response file written (root password disabled; key-only)"

# ------------------------------------------------------------ 3. the install
if [ "$REUSE" -eq 0 ]; then
    say "install (unattended; fetches ~600 MB of sets, allow ~10 min)"
    rm -f "$DISK" "$DIR/in.fifo"
    qemu-img create -q -f qcow2 "$DISK" 12G
    mkfifo "$DIR/in.fifo"

    python3 -m http.server "$HTTP_PORT" --bind 127.0.0.1 \
            --directory "$DIR/www" > "$DIR/httpd.log" 2>&1 &
    HTTPD=$!
    cleanup_install() {
        kill "$HTTPD" 2>/dev/null
        [ -n "${QP:-}" ] && kill "$QP" 2>/dev/null
        rm -f "$DIR/in.fifo"
    }
    trap cleanup_install EXIT INT TERM

    : > "$DIR/install-console.log"
    qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
        -drive file="$DISK",if=virtio,format=qcow2 \
        -cdrom "$ISO" -boot d \
        -netdev user,id=n0 -device virtio-net,netdev=n0 \
        -nographic < "$DIR/in.fifo" >> "$DIR/install-console.log" 2>&1 &
    QP=$!
    exec 3> "$DIR/in.fifo"      # hold the write end open for the whole install

    # A real wait-for, not a sleep. The first version of this used blind delays
    # and could not say WHY it failed when a step moved; this prints the
    # pattern it gave up on, which is the one line that explains the run.
    waitfor() {
        _p=$1; _lim=$2; _i=0
        while [ "$_i" -lt "$_lim" ]; do
            kill -0 "$QP" 2>/dev/null || {
                echo "tier-v-openbsd: qemu exited while waiting for: $_p" >&2
                return 2; }
            if tr -d '\r' < "$DIR/install-console.log" | grep -qF "$_p"; then
                return 0
            fi
            sleep 2; _i=$((_i+2))
        done
        echo "tier-v-openbsd: TIMEOUT (${_lim}s) waiting for: $_p" >&2
        tail -20 "$DIR/install-console.log" >&2
        return 1
    }

    waitfor 'boot>' 120 || exit 3
    # The LOADER already talks to serial; the kernel does not until told.
    sleep 1; printf 'set tty com0\n' >&3
    sleep 1; printf 'boot\n' >&3
    ok "console moved to com0"

    waitfor '(S)hell?' 300 || exit 3
    sleep 2; printf 'A\n' >&3

    # DHCP gives no next-server here, so autoinstall falls through to asking.
    # That fallback is documented in autoinstall(8) and is what makes this rig
    # possible without a DHCP server on the host.
    if waitfor 'esponse file' 240; then
        sleep 2; printf 'http://10.0.2.2:%s/install.conf\n' "$HTTP_PORT" >&3
        ok "response file URL accepted (guest reaches the host at 10.0.2.2)"
    else
        echo "tier-v-openbsd: installer never asked for a response file" >&2
        exit 3
    fi

    if waitfor 'CONGRATULATIONS' 2400; then
        ok "unattended install completed"
    else
        bad "install did not complete"
        exit 3
    fi
    sleep 10
    kill "$QP" 2>/dev/null; wait "$QP" 2>/dev/null
    kill "$HTTPD" 2>/dev/null
    trap - EXIT INT TERM
    rm -f "$DIR/in.fifo"
fi

# -------------------------------------------------------------- 4. boot it
say "boot the installed disk (kvm, ${MEM}M, ${SMP} cpu)"
if [ -f "$DIR/obsd.pid" ] && kill -0 "$(cat "$DIR/obsd.pid" 2>/dev/null)" 2>/dev/null; then
    echo "tier-v-openbsd: a VM from a previous --keep run is still up." >&2
    echo "  stop it first:  kill \$(cat $DIR/obsd.pid)" >&2
    exit 2
fi
# -display none, not -nographic: qemu refuses -nographic with -daemonize, and
# the serial is better off in a file we can quote from on failure anyway.
if ! qemu-system-x86_64 -enable-kvm -m "$MEM" -smp "$SMP" \
    -drive file="$DISK",if=virtio,format=qcow2 \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:"$PORT"-:22 \
    -device virtio-net,netdev=n0 -display none \
    -serial file:"$DIR/obsd-console.log" \
    -daemonize -pidfile "$DIR/obsd.pid" 2>"$DIR/qemu-err.log"; then
    echo "tier-v-openbsd: qemu failed to start:" >&2
    sed 's/^/  /' "$DIR/qemu-err.log" >&2
    exit 2
fi
VMPID=$(cat "$DIR/obsd.pid" 2>/dev/null)
# CLEAN SHUTDOWN FIRST (M482). This used to `kill` qemu outright, which is
# pulling the power cord: the guest's filesystem is left dirty, and one
# interrupted run corrupted this row's disk badly enough that the next boot
# stopped at "UNEXPECTED INCONSISTENCY; RUN fsck_ffs MANUALLY" in single user,
# costing a full reinstall. `halt -p` gives the guest a chance to unmount; the
# kill stays as the fallback for a guest that never came up or has wedged, which
# is why the wait is bounded rather than open-ended.
stop_vm() {
    [ "$KEEP" -eq 1 ] && {
        echo "tier-v-openbsd: VM left up (pid $VMPID, ssh -p $PORT -i $KEY tierv@127.0.0.1)"
        return; }
    [ -n "${VMPID:-}" ] || return
    gr 'halt -p' >/dev/null 2>&1
    _sd=0
    while [ "$_sd" -lt 20 ]; do
        kill -0 "$VMPID" 2>/dev/null || return
        sleep 1; _sd=$((_sd+1))
    done
    kill "$VMPID" 2>/dev/null
}
trap stop_vm EXIT INT TERM

_up=0
for _i in $(seq 1 60); do
    if gr true 2>/dev/null; then _up=1; break; fi
    sleep 5
done
[ "$_up" -eq 1 ] || { bad "never reached userspace -- no ssh after ~5 min"; exit 3; }
ok "ssh reachable: OpenBSD guest is up"

# ------------------------------------------------------- 5. provision + user
say "pkg_add the toolchain"
# NOT PKG_PATH= -- setting it empty overrides /etc/installurl and pkg_add then
# reports "Can't find gmake" as if the package did not exist. Measured.
# poppler-utils supplies pdftotext (M482a). NOT a jichi dependency -- the PDF path
# shells out and reports an actionable error when the extractor is absent -- so this
# is about COVERING that code path on a non-Linux userland.
#
# MEASURED 2026-08-19: it does not currently install on this row, and the reason is
# upstream rather than ours. Its dependency closure pulls cairo -> glib2 -> python3,
# and the 7.9 package set is skewed: poppler-utils-26.04.0 requires python-3.13.14
# while the mirror publishes python-3.13.13, so pkg_add reports nine dependencies
# "not found anywhere" and gives up with "Couldn't install cairo-1.18.4
# poppler-26.04.0 poppler-utils-26.04.0". The attempt is kept because it costs
# nothing and will start working when the set catches up; the row REPORTS the
# outcome (see the pdftotext check below) instead of assuming either way, so
# `pdf`/`docs_pdf` still decline here and the results file says why.
#
# NetBSD installs it cleanly, so the shell-out path IS covered on a non-Linux
# userland -- which was the point of adding it.
if gr 'pkg_add -I gmake git curl poppler-utils >/dev/null 2>&1; command -v gmake >/dev/null' 2>/dev/null; then
    ok "toolchain present (gmake, not make: OpenBSD's make is BSD make)"
else
    bad "pkg_add failed"; gr 'cat /etc/installurl' >> "$RESULTS" 2>&1; exit 1
fi

# Whether the extractor actually landed is reported rather than assumed: the
# pkg_add above ignores its own exit status (deliberately -- `already installed` is
# not an error here), so this is the only thing that distinguishes "installed" from
# "the package name changed in a later release".
if gr 'command -v pdftotext >/dev/null' 2>/dev/null; then
    ok "pdftotext present (the pdf/docs_pdf drivers can run)"
else
    note "    pdftotext ABSENT: pdf and docs_pdf will decline (not a jichi \
dependency -- the PDF path shells out and errors actionably without it)"
fi

# The row runs UNPRIVILEGED on purpose: as root, doctor refuses the hardened
# posture and the row would measure the posture rather than the platform.
gr 'useradd -m -s /bin/ksh tierv 2>/dev/null;
    mkdir -p ~tierv/.ssh && cp /root/.ssh/authorized_keys ~tierv/.ssh/ &&
    chown -R tierv:tierv ~tierv/.ssh && chmod 700 ~tierv/.ssh' 2>/dev/null
g true 2>/dev/null && ok "unprivileged row user ready" || { bad "no tierv user"; exit 1; }

# ---------------------------------------------------------------- 6. identity
note ""
note "## identity"
g 'uname -a; cc --version 2>&1 | head -1; echo "sh -> $(ls -l /bin/sh)"' >> "$RESULTS" 2>&1
g 'sysctl -n hw.ncpu hw.physmem 2>/dev/null | tr "\n" " "; echo' >> "$RESULTS" 2>&1
if g 'test -e /proc/self/status && echo HAVE_PROC || echo NO_PROC' 2>/dev/null | grep -q NO_PROC; then
    ok "procfs is ABSENT (the degradation path four source files must survive)"
fi

# ------------------------------------------------------------------ 7. ship
# HEAD by default, so a published row names a commit and is reproducible.
#
# --dirty exists because without it the loop "find a portability defect on the target,
# fix it, verify the fix on the target" is impossible: `git archive HEAD` cannot see an
# uncommitted change, so the only way to test a fix would be to commit it untested --
# which is exactly how M466's \b fix would have shipped. The mode is LOUD in the
# results file, because a row measured against a dirty tree must never be mistaken for
# one measured against a commit.
if [ "${DIRTY:-0}" = 1 ]; then
    say "ship the WORKING TREE (--dirty: uncommitted changes included)"
else
    say "ship the tree at HEAD"
fi
g 'rm -rf ~/jichi && mkdir -p ~/jichi' 2>/dev/null
_head=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
if [ "${DIRTY:-0}" = 1 ]; then
    _dirtyn=$(git -C "$REPO" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
    printf 'tree: WORKING TREE (--dirty), %s path(s) differ from %s -- NOT a commit\n' \
           "$_dirtyn" "$_head" >> "$RESULTS"
    # Tracked files with working-tree content: what `gmake smoke` would actually run
    # here, which is the whole point of the mode.
    git -C "$REPO" ls-files -z \
      | tar -C "$REPO" --null -T - -cf - 2>/dev/null \
      | g 'cd ~/jichi && tar xf -' 2>/dev/null
else
    printf 'tree: HEAD %s (clean archive)\n' "$_head" >> "$RESULTS"
    git -C "$REPO" archive --format=tar HEAD | g 'cd ~/jichi && tar xf -' 2>/dev/null
fi
g 'test -f ~/jichi/Makefile' 2>/dev/null || { bad "tree did not ship"; exit 1; }
if [ "${DIRTY:-0}" = 1 ]; then
    ok "working tree shipped (${_dirtyn} path(s) uncommitted -- row NOT reproducible from a commit)"
else
    ok "tree shipped (HEAD $_head)"
fi

# ------------------------------------------------------------------ 8. the row
note ""
note "## make info"
g 'cd ~/jichi && gmake info 2>&1 | head -8' >> "$RESULTS" 2>&1

say "build (WERROR=1)"
_t0=$(date +%s)
if g 'cd ~/jichi && gmake WERROR=1 >/tmp/build.log 2>&1 && echo BUILD_OK' 2>/dev/null | grep -q BUILD_OK; then
    _t1=$(date +%s); _secs=$((_t1-_t0))
    ok "WERROR=1 build clean on OpenBSD (${_secs}s)"
    # M464: this used to read `_mult=$(( (_secs + 6) / 6 ))` under a comment
    # claiming "both halves are recorded, so the row survives a change of bench".
    # Only one half was: the 6.19 s denominator was a literal 6, so the comment
    # asserted the very property the code violated, and the row was silently wrong
    # on any other machine. Both terms are now recorded and the reference is a
    # required parameter -- docs/SESSION_RUNBOOK.md §5, copy the formula not the
    # number. jc_rig_mult refuses rather than guessing, so a bad timing kills the
    # row instead of producing the tightest possible deadlines.
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
    g 'grep -E "error|warning" /tmp/build.log 2>/dev/null | head -20' >> "$RESULTS" 2>&1
    exit 1
fi

say "unit suite"
if g 'cd ~/jichi && gmake test 2>&1 | tail -3' 2>/dev/null | tee -a "$RESULTS" \
   | grep -qE '[0-9]+ checks, 0 failures'; then
    ok "unit suite: 0 failures"
else
    bad "unit suite did not report '<N> checks, 0 failures'"
fi

say "smoke tier (ksh as /bin/sh -- the axis this row adds)"
# M466: this was `gmake smoke 2>&1 | tail -6`, the same evidence-discarding capture
# that kept FreeBSD's one failing check hidden for two sessions (M464 fixed it there,
# and only there). It survived the 2026-08-17 run by LUCK -- run.sh happens to print
# its standalone re-classification last, so the failing checks landed inside the six
# lines. A capture that works by luck is not a capture. Log in the guest, pull back
# the failing lines, and record the SKIPS too: on a platform this far from the
# development host, "201 drivers passed" and "201 drivers ran" are different claims,
# and only the skip list distinguishes them.
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
        # NOT anchored: run.sh indents a nested driver's checks as "    | not ok 3".
        echo "--- every failing check, and the driver banners ---"
        g 'grep -nE "not ok|FAILED \(in suite\)|ALSO fails|classify the failure" /tmp/smoke.log | head -40' 2>/dev/null
        echo "--- the standalone re-run, which is the classified diagnosis ---"
        g 'sed -n "/ALSO fails standalone/,\$p" /tmp/smoke.log | head -45' 2>/dev/null
    } | tee -a "$RESULTS"
fi
# Bring the WHOLE log home rather than grepping it in the guest. The first attempt
# did the analysis remotely and returned nothing, because run.sh indents a nested
# driver's TAP as "    | 1..0" and the pattern was anchored `^1\.\.0` -- the exact
# mistake the comment six lines above warns about, made while writing that comment.
# Copying the log costs one scp and moves every later question to the host, where the
# tools are GNU and a wrong pattern can be retried without booting a VM.
SMOKE_LOG="$DIR/smoke-openbsd.log"
if scp $SSH_OPTS -i "$KEY" -P "$PORT" \
        tierv@127.0.0.1:/tmp/smoke.log "$SMOKE_LOG" >/dev/null 2>&1; then
    note "    full smoke log: $SMOKE_LOG ($(wc -l < "$SMOKE_LOG" | tr -d ' ') lines)"
    # "201 drivers passed" and "201 drivers ran" are different claims, and only the
    # skip list separates them. Unanchored, because of the indent noted above.
    _skipped=$(grep -c '1\.\.0' "$SMOKE_LOG" 2>/dev/null || echo 0)
    {
        printf 'drivers that declined to run (TAP 1..0): %s\n' "$_skipped"
        grep -B3 '1\.\.0' "$SMOKE_LOG" 2>/dev/null \
          | grep -E 'smoke: [a-z_]+$|# skip' | head -40
    } | tee -a "$RESULTS"
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
echo "tier-v-openbsd: $N_OK ok, $N_FAIL failed -- $RESULTS"
[ "$N_FAIL" -eq 0 ] || exit 1
exit 0
