#!/bin/sh
# tier-b-device.sh - run the hardware-testing plan's per-run runbook on a
# physical device over SSH, and write one comparable results row.
#
# WHY THIS EXISTS. docs/plans/2026-07-hardware-testing.md:1104 says the runbook
# is "identical everywhere, so rows compare", and then every board row so far
# (M272 Pi Zero 2 W, M276 the same board in armhf, M282 the UNO Q) was driven by
# hand. That identity was enforced by discipline, not by code. With four more
# device rows to add at once -- a Pi 400, a phone, a tablet, and the Zero 2
# again -- discipline is the wrong mechanism: it is exactly the "one fact, two
# reporters" shape M296 forbids, and the failure mode is silent, because two
# rows that were produced slightly differently still look like a table.
#
# So: the runbook, as one script. Steps 0-5 in order, the same on every target.
#
# THE MULTIPLIER, AND WHY IT TAKES A REFERENCE. JC_SMOKE_TIMEOUT_MULT is
# "device build time / the reference host's build time, rounded up". Every
# published multiplier (28, 26, 19, 2) was divided by *threadwork*, whose serial
# `make WERROR=1` was ~4 s -- a number that appears nowhere in the plan, which
# is why those multipliers cannot be reproduced from the document alone. This
# script therefore REFUSES to guess: --ref-secs is required (or JC_REF_SECS),
# and the row records device_secs, ref_secs and the ratio, so a change of bench
# does not silently rebase every future row.
#
# HONEST SCOPE, stated up front:
#   - Verdicts are read from POSITIVE MARKERS in the output, never from an exit
#     code (docs/BUILD.md, M368: a `grep -c '^not ok'` pipe read green over a
#     red driver for thirteen milestones).
#   - The tree is shipped with `git archive`, so a row is stamped with a commit
#     and never carries local uncommitted state. The device needs `tar`, not git.
#   - Step 3's footprint has a FALLBACK LADDER, and the row records WHICH
#     instrument produced the number. `/usr/bin/time -v` is exact. The fallback
#     polls /proc/<pid>/status VmHWM, which can MISS THE PEAK of a command that
#     exits in milliseconds (`--version`); when that rung is used the row says
#     so rather than publishing a low number as if it were measured. This is the
#     UNO Q situation (M282: no /usr/bin/time on the image).
#   - Step 5 (a live model turn) is optional and skipped cleanly. Steps 0-4 need
#     no model, no key and no network beyond the ssh hop.
#   - This is NOT part of `make ci` or `make check-target`. It needs a second
#     machine.
#
# Usage:
#   scripts/tier-b-device.sh pi400.local --ref-secs 6.19
#   scripts/tier-b-device.sh alex@192.0.2.10 --label "Pi 400 aarch64" --ref-secs 6.19
#   scripts/tier-b-device.sh phone --ref-secs 6.19 --live http://192.0.2.1:1234/v1
#   scripts/tier-b-device.sh pi400 --ref-secs 6.19 --dry-run
#
# Options:
#   --ref-secs N     reference host serial `make WERROR=1` seconds (REQUIRED)
#   --cc NAME        compiler for the device, exported as CC to every remote
#                    command. Needed where `cc` does not exist: Guix System
#                    ships gcc but neither `cc` nor `c99` (POSIX names c99, not
#                    cc). Without it the Makefile's `CC ?= cc` default makes
#                    every capability PROBE fail, so `make info` reports "no
#                    vsnprintf, no curl, no malloc_trim" -- a far more
#                    misleading row than "no compiler" (M458).
#   --label NAME     row label; defaults to the ssh target
#   --out DIR        results dir (default ./.tier-b-results)
#   --remote DIR     work dir on the device (default ~/jichi-tier-b)
#   --rev REV        git revision to ship (default HEAD)
#   --live URL       also run steps 5 and 5b against this OpenAI-compatible base
#   --live-model ID  the model id that endpoint serves (default "local"); an
#                    OpenAI-compatible server rejects an id it does not have
#   --keep           leave the remote work dir in place
#   --dry-run        print what would run, touch nothing
#
# Env: JC_REF_SECS (same as --ref-secs), JC_TB_SSH (ssh command, default "ssh").
set -eu

# The one implementation of JC_SMOKE_TIMEOUT_MULT (M464). This script's logic is
# the original; it lives there now so the other rigs cannot diverge from it again.
. "$(dirname "$0")/_rig_mult.sh"
. "$(dirname "$0")/_rig_live.sh"

REF_SECS="${JC_REF_SECS:-}"
CC_NAME=""
LABEL=""
OUT="./.tier-b-results"
REMOTE_DIR="jichi-tier-b"
REV="HEAD"
LIVE=""
LIVE_PORT=""
# The model id the endpoint serves. An OpenAI-compatible server rejects an id it
# does not have, and the id is not derivable from the URL, so it is an option.
LIVE_MODEL="local"
KEEP=0
DRY=0
TARGET=""
SSH="${JC_TB_SSH:-ssh}"

while [ $# -gt 0 ]; do
    case "$1" in
        --ref-secs) REF_SECS="$2"; shift ;;
        --cc)       CC_NAME="$2";  shift ;;
        --label)    LABEL="$2";    shift ;;
        --out)      OUT="$2";      shift ;;
        --remote)   REMOTE_DIR="$2"; shift ;;
        --rev)      REV="$2";      shift ;;
        --live)     LIVE="$2";     shift ;;
        --live-port) LIVE_PORT="$2"; shift ;;
        --live-model) LIVE_MODEL="$2"; shift ;;
        --keep)     KEEP=1 ;;
        --dry-run)  DRY=1 ;;
        -h|--help)  awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        -*)         echo "tier-b-device: unknown option: $1" >&2; exit 2 ;;
        *)          [ -n "$TARGET" ] && { echo "tier-b-device: one target only" >&2; exit 2; }
                    TARGET="$1" ;;
    esac
    shift
done

[ -n "$TARGET" ]   || { echo "tier-b-device: need an ssh target (try --help)" >&2; exit 2; }
[ -n "$REF_SECS" ] || { echo "tier-b-device: --ref-secs is required -- a multiplier without its denominator is not reproducible (see the header)" >&2; exit 2; }
[ -n "$LABEL" ]    || LABEL="$TARGET"

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ -n "$CC_NAME" ]; then MAKEV="CC=$CC_NAME "; else MAKEV=""; fi
COMMIT=$(git -C "$REPO_ROOT" rev-parse --short "$REV" 2>/dev/null || echo unknown)

N_OK=0; N_FAIL=0; N_SKIP=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK + 1));     echo "ok - $*";     echo "ok   - $*" >> "$OUT/results.txt"; }
bad()  { N_FAIL=$((N_FAIL + 1)); echo "not ok - $*"; echo "FAIL - $*" >> "$OUT/results.txt"; }
skip() { N_SKIP=$((N_SKIP + 1)); echo "skip - $*";   echo "skip - $*" >> "$OUT/results.txt"; }
note() { echo "$*" >> "$OUT/results.txt"; }

# Run a command on the device, from inside the shipped tree, with an isolated
# HOME so the operator's own config cannot move a number (the M430 rule).
dev() { $SSH "$TARGET" "cd \$HOME/$REMOTE_DIR && HOME=\$HOME/$REMOTE_DIR/.home ${MAKEV}sh -lc '$1'"; }

# devl -- dev() carrying a REVERSE forward, so the board reaches a model server
# bound to the HOST's loopback for exactly the lifetime of this one command.
#
# WHY (M677). The live step took a `--live <url>` the board had to reach on its
# own, which means the model server must listen on the LAN. Measured on this
# bench: LM Studio binds 127.0.0.1:1234 and nothing else, so that URL was
# unreachable from either Pi and the only way to drive a board was to reconfigure
# the model server to accept connections from the network. The three VM rigs had
# already refused that trade and solved it with `ssh -R`; the boards can use the
# identical trick, and then --live-port names a port on the BOARD's loopback that
# exists only while the turn runs. Proven before it was written in: both Pis
# fetched /v1/models through the forward against an unchanged, loopback-bound
# server.
# devl -- dev() carrying a REVERSE forward, used only when the board cannot
# already reach the endpoint.
#
# WHY THE PROBE BELOW EXISTS, and it is the more useful half of this (M677).
# The first version always forwarded, and `ssh -R` whose forward cannot bind
# prints "Warning: remote port forwarding failed for listen port N" and RUNS
# THE COMMAND ANYWAY, exit 0. Measured on the Pi 400: the rig's forward failed,
# the turn answered regardless, and the row reported two green live turns with
# no indication of which path carried them.
#
# The cause was NOT a stale socket. Both boards on this bench carry a
# deliberate persistent tunnel -- `ssh -N -R 1234:127.0.0.1:1234` with
# ServerAliveInterval and ExitOnForwardFailure already set -- so the port was
# already correctly forwarded and the rig was trying to bind it a second time.
# Adding ExitOnForwardFailure here alone would therefore have BROKEN a working
# bench: the rig would abort precisely where the operator had already done the
# work.
#
# So the rig asks the board first. If it can already reach the endpoint, that
# path is used and NAMED in the results; if it cannot, the rig makes its own
# forward and a bind failure is then a real failure rather than a warning that
# scrolls past. Either way the row says which transport carried it, which is
# the thing whose absence made the log unreadable.
devl() {
    $SSH -o ExitOnForwardFailure=yes -R "$LIVE_PORT:127.0.0.1:$LIVE_PORT" "$TARGET" \
        "cd \$HOME/$REMOTE_DIR && HOME=\$HOME/$REMOTE_DIR/.home ${MAKEV}sh -lc '$1'"
}

# Can the board already reach the endpoint on its own? Answered by asking IT,
# not by looking at this host's process list.
dev_can_reach() {
    $SSH "$TARGET" "curl -s -o /dev/null --max-time 5 http://127.0.0.1:$LIVE_PORT/v1/models" \
        >/dev/null 2>&1
}

if [ "$DRY" -eq 1 ]; then
    echo "tier-b-device: DRY RUN"
    echo "  target      : $TARGET"
    echo "  label       : $LABEL"
    echo "  revision    : $REV ($COMMIT)"
    echo "  ref secs    : $REF_SECS   (denominator for JC_SMOKE_TIMEOUT_MULT)"
    echo "  remote dir  : ~/$REMOTE_DIR"
    echo "  results     : $OUT/results.txt"
    if [ -n "$LIVE_PORT" ]; then
        echo "  live        : $LIVE_MODEL over a reverse forward on port $LIVE_PORT"
    elif [ -n "$LIVE" ]; then
        echo "  live        : $LIVE_MODEL at $LIVE (the board must reach this itself)"
    else
        echo "  live        : <skipped -- pass --live-port or --live>"
    fi
    echo
    echo "would run, in order:"
    echo "  0. identity     uname -a; /etc/os-release; gcc --version; ldd --version; nproc; free -m"
    echo "  1. build        make clean && make info; time make WERROR=1; make SIZE=1; size jichi"
    echo "  2. gate         JC_SMOKE_TIMEOUT_MULT=<computed> make check-target"
    echo "  3. footprint    /usr/bin/time -v  ->  else poll /proc/<pid> VmHWM (labelled)"
    echo "  4. offline      doctor; context; map | head -20; describe"
    echo "  5. live turn   the shared driven task (scripts/_rig_live.sh)   (--live/--live-port)"
    exit 0
fi

mkdir -p "$OUT"
# Clear the PER-STEP artifacts too, not just results.txt. Each is written when
# its step runs, so a run that dies partway used to leave last run's file
# sitting there looking current, and a reader -- or a monitor -- could not tell
# which run an artifact belonged to. That is the same "a stale answer is
# indistinguishable from this answer" trap this rig exists to avoid; it caught
# me reading a previous row's gate.txt during M459. Removed BY NAME rather than
# wiping $OUT, since --out may point somewhere with other content in it.
for _f in build footprint gate identity info live map size; do
    rm -f "$OUT/$_f.txt"
done
: > "$OUT/results.txt"
note "# tier-b-device row: $LABEL"
note "# date        : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
note "# driver host : $(uname -srm) / $(hostname)"
note "# revision    : $REV ($COMMIT)"
note "# ref_secs    : $REF_SECS (serial make WERROR=1 on the driver host)"
note ""

say "tier-b-device -- $LABEL ($TARGET), rev $COMMIT"

# ---------------------------------------------------------------- reachability
if $SSH -o BatchMode=yes -o ConnectTimeout=10 "$TARGET" true 2>/dev/null; then
    ok "ssh reachable: $TARGET"
else
    bad "ssh not reachable: $TARGET (no row can be produced)"
    say "totals: $N_OK ok, $N_FAIL failed, $N_SKIP skipped -- $OUT/results.txt"
    exit 1
fi

# ------------------------------------------------------------------- ship tree
say "shipping tree ($COMMIT) to ~/$REMOTE_DIR"
$SSH "$TARGET" "rm -rf \$HOME/$REMOTE_DIR && mkdir -p \$HOME/$REMOTE_DIR/.home"
if git -C "$REPO_ROOT" archive --format=tar "$REV" \
     | $SSH "$TARGET" "tar xf - -C \$HOME/$REMOTE_DIR"; then
    ok "tree shipped at $COMMIT"
else
    bad "could not ship the tree"
    exit 1
fi

# `git archive` deliberately carries no .git -- that is what makes the shipped
# tree clean and commit-stamped. But tests/test_git.c returns early when the cwd
# is not a git repo ("skipping git integration"), so a device row silently ran
# FOUR CHECKS FEWER than a host run: 11,589 against 11,593, measured on the Pi
# 400 and reproduced exactly by unpacking the same archive into a non-repo
# directory on the host. An unexplained delta in a comparison table is the thing
# this whole rig exists to prevent -- and worse, it meant jichi's git tools had
# never once been exercised on ARM.
#
# So: make the shipped tree a real repository at a known state. Cheap,
# deterministic, and it restores the row's comparability to a host run.
if $SSH "$TARGET" "cd \$HOME/$REMOTE_DIR && git init -q . >/dev/null 2>&1 && \
        git -c user.email=bench@invalid -c user.name=bench add -A >/dev/null 2>&1 && \
        git -c user.email=bench@invalid -c user.name=bench commit -qm 'shipped $COMMIT' >/dev/null 2>&1"; then
    ok "shipped tree made a git repo (so the git-tool checks run, as on a host)"
else
    skip "could not git-init on the device -- git-integration checks will skip (row will read 4 checks low)"
fi

# ------------------------------------------------------------- step 0 identity
say "step 0 -- identity"
note "## step 0 -- identity (verbatim)"
# Every probe is individually tolerant: identity is a RECORD, not a gate, and a
# platform that lacks one of these is exactly the platform worth recording. On
# Termux there is no /etc/os-release and no `free`, which used to fail the whole
# step and lose the rest of the block with it (M459).
# `[ -r ... ] &&` before the dot, NOT `|| true` after it: `.` is a POSIX SPECIAL
# BUILTIN, so a failed source EXITS a non-interactive shell and `|| true` cannot
# catch it. On Termux there is no /etc/os-release, and the step died there
# silently after uname, losing every probe below it (M459).
if dev 'uname -a || true; [ -r /etc/os-release ] && . /etc/os-release && echo "os: $PRETTY_NAME"; \
        { cc --version 2>/dev/null || gcc --version 2>/dev/null; } | head -1 || true; \
        ldd --version 2>&1 | head -1 || true; \
        echo "nproc: $(nproc 2>/dev/null || echo ?)"; free -m 2>/dev/null | head -2 || true; \
        echo IDENTITY_OK' \
        > "$OUT/identity.txt" 2>&1 && grep -q '^IDENTITY_OK$' "$OUT/identity.txt"; then
    sed 's/^/    /' "$OUT/identity.txt" >> "$OUT/results.txt"
    ok "identity captured"
else
    bad "identity step failed"
fi
note ""

# ------------------------------------------------------- step 1 configure+build
say "step 1 -- build (this wall clock IS the CPU signal)"
note "## step 1 -- build"
dev 'make clean >/dev/null 2>&1; make info' > "$OUT/info.txt" 2>&1 \
    && { sed 's/^/    /' "$OUT/info.txt" >> "$OUT/results.txt"; ok "make info recorded"; } \
    || bad "make info failed"

# Serial build, timed on the device, to match how ref_secs was measured.
# The subtraction is done with awk, NOT bc: bc is absent on a stock Raspberry Pi
# OS image, and its absence used to be silent and dangerous. `secs=?` flowed into
# the multiplier awk below, where `d <= 0` compared the STRING "?" against "0" --
# "?" sorts higher, so the guard passed, "?"/ref evaluated to 0, and the clamp
# turned that into JC_SMOKE_TIMEOUT_MULT=1. On the SLOWEST board in the fleet.
# A missing tool produced the tightest possible deadlines and a plausible-looking
# number instead of an error. awk is POSIX and present everywhere the gate runs.
#
# LC_ALL=C, and it is load-bearing (M657c). awk's printf "%.2f" formats through
# LC_NUMERIC, so on a device with a German locale the elapsed seconds came back
# as "30,45" -- not numeric to the shell guard below, so `jc_rig_mult` refused
# it and the whole Pi 400 row was abandoned after a clean build. The refusal was
# right; the number should never have had a comma in it. A figure produced for a
# machine is formatted in the C locale, wherever the machine happens to live.
# Every device row before this one happened to be an English-locale image, which
# is why a rig that cannot measure a German board shipped and passed.
#
# build.err goes to /var/tmp, spelled literally, for the same reason the results
# file lives outside the tree -- and the FIRST attempt wrote "$HOME/..." which
# did not survive the trip through the rig's quoting and ssh: what arrived was
# relative, so the capture landed at `<shipped tree>/.home/jichi-tb-build.err`
# and snapshot_lint check 14 reported it as an untracked file. Twice, in two
# different steps of this same edit. The check caught both; I caught neither.
# The reason: the rig makes the shipped tree a git repository so the git-tool checks
# run, and `snapshot_lint` check 14 then correctly reports the rig's own stderr
# capture as an untracked file and fails the gate. "A rig should not be able to
# dirty the tree it tests" -- this was the fourth instance in this project, and
# the first one caught by a check rather than by a person.
dev 'S=$(date +%s.%N); LC_ALL=C make WERROR=1 >/dev/null 2>/var/tmp/jichi-tb-build.err; RC=$?; E=$(date +%s.%N); \
     echo "rc=$RC"; echo "secs=$(LC_ALL=C awk -v a="$E" -v b="$S" "BEGIN{printf \"%.2f\", a-b}" 2>/dev/null)"; \
     tail -5 /var/tmp/jichi-tb-build.err' > "$OUT/build.txt" 2>&1 || true

BUILD_RC=$(sed -n 's/^rc=//p' "$OUT/build.txt" | head -1)
DEV_SECS=$(sed -n 's/^secs=//p' "$OUT/build.txt" | head -1)

if [ "${BUILD_RC:-1}" = "0" ]; then
    ok "WERROR=1 build clean on the device (${DEV_SECS}s)"
else
    bad "WERROR=1 build FAILED on the device -- this is a portability finding"
    sed 's/^/    /' "$OUT/build.txt" >> "$OUT/results.txt"
fi

# Multiplier: ceil(device / reference). Computed, never guessed.
#
# The arithmetic and its numeric validation moved to scripts/_rig_mult.sh at M464,
# unchanged, because two other rigs needed it and had each got it wrong in a
# different way (one baked a bench's 6.19 s in as a literal 6; one computed no
# multiplier at all and ran with the default of 1). The reason the validation is
# load-bearing is recorded there: awk compares a non-numeric -v operand as a
# STRING, so a `secs=?` from a failed timing passed a `d <= 0` guard, divided to
# 0, and was clamped to 1 -- the tightest deadline, on the slowest board.
MULT=$(jc_rig_mult "${DEV_SECS:-}" "$REF_SECS") || MULT=""

if [ -n "$MULT" ] && [ "$MULT" -ge 1 ] 2>/dev/null; then
    note "    build: device ${DEV_SECS}s / ref ${REF_SECS}s => JC_SMOKE_TIMEOUT_MULT=$MULT"
    ok "multiplier computed: $MULT (${DEV_SECS}s / ${REF_SECS}s)"
else
    bad "multiplier NOT derivable: device build time was '${DEV_SECS:-<empty>}'. A guessed multiplier makes every timing in this row meaningless, so no row is produced. Check that the device timed its build (date +%s.%N and awk must both work there)."
    note "    build: device time '${DEV_SECS:-<empty>}' is not numeric -- row abandoned"
    say "totals: $N_OK ok, $N_FAIL failed, $N_SKIP skipped -- $OUT/results.txt"
    exit 1
fi

dev 'make clean >/dev/null 2>&1; make SIZE=1 >/dev/null 2>&1 && { size jichi; ls -l jichi; }' \
    > "$OUT/size.txt" 2>&1 \
    && { sed 's/^/    /' "$OUT/size.txt" >> "$OUT/results.txt"; ok "SIZE=1 binary measured"; } \
    || bad "SIZE=1 build failed"
note ""

# ---------------------------------------------------------------- step 2 gate
say "step 2 -- the portable gate (JC_SMOKE_TIMEOUT_MULT=$MULT)"
note "## step 2 -- make check-target (mult $MULT)"
# TMPDIR on DISK, not /tmp (M657c). On the Pi Zero 2 W /tmp is a 213 MB tmpfs --
# half of the board's 425 MB of RAM -- and the smoke tier's own fixtures fill it
# to 100%. The failure does not read like a full disk: drivers report
# `printf: I/O error` and "cannot open .../files", and six checks announce that
# their own matcher is "meaningless", which reads as a defect in jichi. It is an
# environment limit, and the rig is what should know about it. M457 routed every
# fixture path through TMPDIR for exactly this; the rig simply never set one.
# /var/tmp, spelled literally. The first version of this line wrote
# TMPDIR=\$HOME/... and the escaping did not survive the trip through the rig's
# own double quotes and ssh: what arrived was RELATIVE, so the tier built its
# fixtures at `<shipped tree>/.home/jichi-tb-tmp/` -- inside the repository --
# and `make-snapshot` refused with "refusing a destination inside the
# repository". A rig that dirties the tree it tests, again, in the very edit
# that was fixing a rig that dirties the tree it tests. Caught by snapshot_lint
# rather than by me. /var/tmp needs no expansion, is disk-backed on every target
# in this fleet (only /tmp is tmpfs), and is POSIX's place for temporary files
# that may be large.
dev "make clean >/dev/null 2>&1; \
     TMPDIR=/var/tmp LC_ALL=C JC_SMOKE_TIMEOUT_MULT=$MULT JC_SMOKE_KEEP_GOING=1 \
     make check-target" \
    > "$OUT/gate.txt" 2>&1 || true

# Positive markers only: the counts must be present and zero-failure.
UNITS=$(grep -oE '[0-9]+ checks, 0 failures' "$OUT/gate.txt" | tail -1 || true)
# JC_SMOKE_KEEP_GOING=1 above is what makes a count EXIST on a failing row, and
# it is the root cause of the gap this page and PLATFORM_RETEST.md both record.
# Without it the tier stops at the first failing driver and never prints its
# summary, so the rig had nothing to extract -- the Pi 400 was filed for months
# as "green without a driver count" and the Pi Zero produced neither shape of the
# marker today. The BSD rig has set it since M466 ("report EVERY failing driver
# in this one boot") and the device rig never did. Matching both shapes below is
# necessary and was not sufficient.
#
# BOTH SHAPES, because a row that FAILS is the one whose denominator a reader
# wants (2026-09-18). This matched only `smoke: OK (N drivers, M checks)`, so a
# partly-green device came back with **no driver count at all** and its coverage
# debt was not a large number, it was not a number -- which is exactly what
# PLATFORM_RETEST.md records for the Pi 400 and why that row sat uncomputable.
# The same gap was found and fixed in tier-v-bsd.sh the same day; this is the
# sibling that page said had not been checked.
SMOKE=$(grep -oE 'smoke: OK \([0-9]+ drivers, [0-9]+ checks\)|smoke: \([0-9]+ of [0-9]+ drivers passed, [0-9]+ checks\)' \
        "$OUT/gate.txt" | tail -1 || true)

if [ -n "$UNITS" ]; then ok "unit suite: $UNITS"; note "    $UNITS"
else bad "unit suite: no '<N> checks, 0 failures' marker in the output"; fi

# A count that is not "OK" is still a RESULT, and is recorded as one: the row
# says how much of the tier ran, and the failing drivers are named below.
case "$SMOKE" in
    "smoke: OK"*) ok "smoke tier: $SMOKE"; note "    $SMOKE" ;;
    "smoke: ("*)  bad "smoke tier did not fully pass -- $SMOKE"; note "    $SMOKE"
                  grep 'FAILED (in suite)' "$OUT/gate.txt" 2>/dev/null \
                      | sed 's/^/      /' >> "$OUT/results.txt" || true ;;
    *)            bad "smoke tier: no driver-count marker of EITHER shape in the output" ;;
esac

if [ -z "$UNITS" ] || [ -z "$SMOKE" ]; then
    note "    (last 20 lines of the gate output)"
    tail -20 "$OUT/gate.txt" | sed 's/^/      /' >> "$OUT/results.txt"
fi
note ""

# ----------------------------------------------------------- step 3 footprint
say "step 3 -- footprint"
note "## step 3 -- footprint"
if dev 'command -v /usr/bin/time >/dev/null && /usr/bin/time -v true >/dev/null 2>&1' 2>/dev/null; then
    INSTR="/usr/bin/time -v (exact peak)"
    dev 'make >/dev/null 2>&1; \
         for c in "--version" "doctor"; do \
           printf "%s: " "$c"; \
           /usr/bin/time -v ./jichi $c 2>&1 >/dev/null | sed -n "s/.*Maximum resident set size (kbytes): //p"; \
         done' > "$OUT/footprint.txt" 2>&1 || true
else
    INSTR="polled /proc/<pid> VmHWM -- MAY UNDER-REPORT a fast command; see header"
    dev 'make >/dev/null 2>&1; \
         for c in "--version" "doctor"; do \
           ./jichi $c >/dev/null 2>&1 & p=$!; hi=0; \
           while [ -d /proc/$p ]; do \
             v=$(sed -n "s/^VmHWM:[ \t]*\([0-9]*\).*/\1/p" /proc/$p/status 2>/dev/null || echo 0); \
             [ -n "$v" ] && [ "$v" -gt "$hi" ] 2>/dev/null && hi=$v; \
           done; wait $p 2>/dev/null; printf "%s: %s\n" "$c" "$hi"; \
         done' > "$OUT/footprint.txt" 2>&1 || true
fi
note "    instrument: $INSTR"
if [ -s "$OUT/footprint.txt" ]; then
    sed 's/^/    /' "$OUT/footprint.txt" >> "$OUT/results.txt"
    ok "footprint recorded via $INSTR"
else
    bad "footprint could not be measured"
fi
note ""

# ---------------------------------------------------- step 4 offline surfaces
say "step 4 -- offline surfaces (no network, no key)"
note "## step 4 -- offline surfaces"
for sub in doctor context describe; do
    if dev "./jichi $sub >/dev/null 2>&1"; then ok "offline: $sub ran"
    else bad "offline: $sub failed"; fi
done
# A POSITIVE MARKER, like every other check here. This used to accept any
# non-empty capture -- and ssh's own "Warning: Permanently added ... to the list
# of known hosts" is non-empty, so the check passed on a device where `map` could
# not even DYNAMICALLY LINK (a Termux libcurl/openssl mismatch, M459). The rest of
# this script reads verdicts from markers precisely so a failure cannot masquerade
# as output; this one line did not, and it lied on the first device that broke.
if dev './jichi map >/dev/null 2>&1 && echo MAP_OK' > "$OUT/map.txt" 2>&1 \
   && grep -q '^MAP_OK$' "$OUT/map.txt"; then
    ok "offline: map ran"
else
    bad "offline: map failed"
fi
note ""

# --------------------------------------------------------- step 5 live turn
# The DRIVEN step. The TASK -- both prompts, the nonce, the config, the fixture
# -- is scripts/_rig_live.sh, shared with every other rig so the rows are
# comparable. Only the TRANSPORT is this rig's own: `dev`/`devl` drive a
# physical board over the LAN, cd into a run directory under a nested $HOME, so
# every path here is RELATIVE. That is why this calls the task functions rather
# than jc_rig_live, which assumes the tier-v guests' absolute layout.
say "step 5 -- live turn"
note "## step 5 -- live turn"
if [ -z "$LIVE" ] && [ -z "$LIVE_PORT" ]; then
    skip "live turn not attempted (no --live/--live-port); steps 0-4 need no model"
    note "    not attempted -- no endpoint given"
else
    # --live-port tunnels to the HOST's loopback (preferred; needs no model
    # server reconfiguration). --live takes a URL the board reaches itself.
    if [ -n "$LIVE_PORT" ]; then
        _lurl="http://127.0.0.1:$LIVE_PORT/v1"
        if dev_can_reach; then
            _run=dev
            ok "transport: the board already reaches port $LIVE_PORT (an existing forward)"
            note "    transport: existing forward on the board -- the rig made none"
        else
            _run=devl
            note "    transport: the rig's own reverse forward on port $LIVE_PORT"
        fi
    else
        _lurl="$LIVE"
        _run=dev
        note "    transport: the board reaches $LIVE itself (no forward)"
    fi

    # A RELATIVE path: `dev` already cds into the run directory, and $HOME
    # inside it is the nested .home, so `$HOME/live.json` resolved to a path
    # that did not exist and jichi reported `config file not found`.
    jc_rig_live_config "$LIVE_MODEL" "$_lurl" | $_run "cat > live.json" 2>/dev/null || true

    _p_live=$(jc_rig_live_prompt_wire)
    if $_run "./jichi --config live.json --prompt-b64 $_p_live --output json" \
        > "$OUT/live.txt" 2>&1 && grep -q '"text"' "$OUT/live.txt"; then
        ok "live turn answered against $_lurl"
        sed 's/^/    /' "$OUT/live.txt" | head -5 >> "$OUT/results.txt"
    else
        bad "live turn did not produce a JSON answer against $_lurl"
        tail -10 "$OUT/live.txt" | sed 's/^/    /' >> "$OUT/results.txt"
    fi

    # ------------------------------------------------ step 5b: the AGENT LOOP
    # The turn above proves the wire and nothing else: no tool is chosen, none
    # executed, no second turn consumes a result. A row green on the text turn
    # alone can be a row where jichi executes nothing.
    _phrase=$(jc_rig_live_phrase TIER-B)
    _p_tool=$(jc_rig_live_prompt_tool)
    say "step 5b -- agentic turn (a tool the model must actually run)"
    note "## step 5b -- agentic turn"
    $_run "mkdir -p ws" >/dev/null 2>&1 || true
    jc_rig_live_fixture "$_phrase" | $_run "cat > ws/note.txt" 2>/dev/null || true
    if $_run "cd ws && ../jichi --config ../live.json --auto -q --prompt-b64 $_p_tool \
< /dev/null" > "$OUT/live-tool.txt" 2>&1 \
       && grep -q "$_phrase" "$OUT/live-tool.txt"; then
        ok "agentic turn: the model called a tool and reported $_phrase"
        note "    phrase $_phrase returned -- the tool ran and its result was used"
    else
        bad "agentic turn did NOT return the phrase -- the model may have \
described the tool call instead of invoking it (doctor --live classifies this \
as \`text\`), or the loop does not execute tools on this platform"
        tail -12 "$OUT/live-tool.txt" 2>/dev/null | sed 's/^/    /' >> "$OUT/results.txt"
    fi
fi
note ""

# ------------------------------------------------------------------- teardown
if [ "$KEEP" -eq 1 ]; then
    say "keeping ~/$REMOTE_DIR on the device (--keep)"
else
    $SSH "$TARGET" "rm -rf \$HOME/$REMOTE_DIR" 2>/dev/null || true
fi

note "## totals: $N_OK ok, $N_FAIL failed, $N_SKIP skipped"
say "totals: $N_OK ok, $N_FAIL failed, $N_SKIP skipped -- $OUT/results.txt"
[ "$N_FAIL" -eq 0 ]
