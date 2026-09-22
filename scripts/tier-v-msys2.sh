#!/bin/sh
# tier-v-msys2.sh -- the Windows + MSYS2 (MSYS) row: the SECOND emulation of
# POSIX over Win32, which is the entire point of measuring it beside Cygwin.
#
# WHY BOTH, WHEN THEY SHARE ANCESTRY. MSYS2's runtime is a fork of Cygwin's, so
# an agreement between the two rows is evidence about EMULATING POSIX; a
# divergence isolates Cygwin-version behaviour from emulation behaviour. One row
# alone cannot tell those apart, and every conclusion drawn from it would be
# ambiguous between them. (The same logic, one layer down, is why the UCRT64 and
# MINGW64 survey measures
# UCRT64 and MINGW64 rather than picking one: same POSIX absence, different C
# runtime.)
#
# WHAT THIS ROW UNIQUELY EXERCISES:
#   * A FILESYSTEM WHERE `chmod` RETURNS SUCCESS AND DOES NOTHING. MSYS2 mounts
#     `noacl` by default, and this is the only platform in the matrix where
#     jichi's file-privacy guarantees genuinely do not hold. Measured M697: the
#     daemon REFUSES TO START rather than expose a socket that runs shell
#     commands, and the key file and audit log land at -rw-r--r--. Those are
#     fences firing correctly, not defects -- and `doctor` says so, because it
#     probes the real state root.
#   * AND THE DOCUMENTED FIX PROTECTS THE TESTS, NOT THE USER. Adding
#     `C:/msys64/tmp /tmp ntfs binary,acl` makes the TIER pass, because its
#     isolated HOME lives under /tmp -- while a real user's ~/.jichi.env under
#     /home stays world-readable. This rig probes BOTH and says so when they
#     diverge, because a green gate over an exposed key file is the worst
#     possible combination and nothing else in the tree would catch it.
#   * `ln -s` THAT CANNOT MAKE A DANGLING LINK, failing with ENOENT under every
#     symlink setting. At M697 that turned `pathfence_dangling` into a driver
#     reporting a hole in the PATH FENCE for a fixture the platform cannot build.
#     A harness defect dressed as a security finding is the worst direction for
#     a failure, and the one a reader believes.
#
# Exit codes (the tier-v contract):
#   0  the row ran and every check passed
#   1  the row ran and something failed -- a RESULT, read results-msys2.txt
#   2  usage, or a tool missing on the host
#   3  this is not an MSYS shell -- NOT a result, the rig never reached the row
#
# Usage (--ref-secs is REQUIRED: THIS bench's serial `make WERROR=1` seconds):
#   scripts/tier-v-msys2.sh --ref-secs 9.65
#   scripts/tier-v-msys2.sh --dry-run
#   scripts/tier-v-msys2.sh --ref-secs 9.65 \
#       --live https://api.hrz.uni-giessen.de/v1 --live-model jlu/qwen3.8-27b \
#       --live-key-env JICHI_API_KEY --free-prefix jlu/
set -u

. "$(dirname "$0")/_rig_mult.sh"
. "$(dirname "$0")/_rig_ship.sh"
. "$(dirname "$0")/_rig_live.sh"
. "$(dirname "$0")/_rig_win.sh"

ROW="msys2"
REF_SECS="${JC_REF_SECS:-}"
DRY=0
LIVE_URL=""
LIVE_PORT=""
LIVE_MODEL="local"
LIVE_KEY_ENV=""
FREE_PREFIX=""
TREE="${JC_WIN_TREE:-$HOME/jichi}"
DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}/msys2"

while [ $# -gt 0 ]; do
    case "$1" in
        --ref-secs)     REF_SECS="$2"; shift ;;
        --live)         LIVE_URL="$2"; shift ;;
        --live-port)    LIVE_PORT="$2"; shift ;;
        --live-model)   LIVE_MODEL="$2"; shift ;;
        --live-key-env) LIVE_KEY_ENV="$2"; shift ;;
        --free-prefix)  FREE_PREFIX="$2"; shift ;;
        --tree)         TREE="$2"; shift ;;
        --dry-run)      DRY=1 ;;
        -h|--help)      awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        *) echo "tier-v-msys2: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

[ -n "$LIVE_PORT" ] && [ -z "$LIVE_URL" ] && LIVE_URL="http://127.0.0.1:$LIVE_PORT/v1"

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-msys2: dry run -- nothing is built, measured or driven."
    echo "  tree      $TREE"
    echo "  results   $DIR/results-msys2.txt"
    echo "  ref-secs  ${REF_SECS:-<required>}"
    echo "  live      ${LIVE_URL:-<none: the row would be Verified, not Driven>}"
    echo "  model     $LIVE_MODEL"
    echo "  key env   ${LIVE_KEY_ENV:-<none: keyless, as a loopback server is>}"
    echo "  free ns   ${FREE_PREFIX:-<unchecked>}"
    exit 0
fi

jc_rig_ref_or_die tier-v-msys2 "$REF_SECS" || exit 2

# MSYS2 ships four shells and only ONE of them is this row. ucrt64 and mingw64
# are a different C runtime with no POSIX emulation at all (a later survey), and
# a row measured from the wrong launcher looks entirely real and is not.
case "$(uname -s 2>/dev/null)" in
    MSYS_NT-*) ;;
    MINGW64_NT-*|UCRT64_NT-*|CLANG64_NT-*)
        echo "tier-v-msys2: this is the $(uname -s) shell, not MSYS." >&2
        echo "  That is a different row -- a native CRT with no POSIX emulation." >&2
        echo "  Launch msys2.exe, not mingw64.exe/ucrt64.exe." >&2
        exit 3 ;;
    *) echo "tier-v-msys2: this is not an MSYS2 shell (uname -s = $(uname -s))." >&2
       echo "  Run it from msys2.exe, with Cygwin closed. Their DLL" >&2
       echo "  shared-memory regions collide and fork begins failing in both." >&2
       exit 3 ;;
esac

mkdir -p "$DIR"
# Every log this rig writes goes here, never into the tree it is measuring.
JCW_OUT="$DIR"
LIVE_KEY_ENV="${LIVE_KEY_ENV:-JICHI_API_KEY}"
RESULTS="$DIR/results-msys2.txt"
: > "$RESULTS"
N_OK=0; N_FAIL=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));     echo "ok - $*";     echo "ok   - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL - $*" >> "$RESULTS"; }
note() { echo "$*"; echo "$*" >> "$RESULTS"; }

note "== tier-v-msys2, $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
note "uname: $(uname -a 2>/dev/null)"
note "$(jc_rig_ship_stamp "$TREE" 1)"
note "reference bench: ${REF_SECS}s serial make WERROR=1"
note ""

# ------------------------------------------------------------ prerequisites
jcw_key_stash "$LIVE_KEY_ENV"

say "prerequisites"
jcw_guard_other_layer "msys2" "C:\\cygwin64" || exit 1
jcw_prereqs "msys2" || exit 1
[ -d "$TREE" ] || { bad "no tree at $TREE -- clone one with core.autocrlf=false"; exit 1; }
ok "msys2: tree present at $TREE"
note ""

# ------------------------------- the configuration this row is being measured in
# STATED, NEVER ASSUMED. A row measured only in the configured state is true for
# nobody: what a learner installs is the stock state, and what a maintainer has
# is whatever they fixed last August. Both are legitimate rows; a row that does
# not say which it is, is not.
say "configuration"
note "    MSYS=${MSYS:-<unset>}   (unset means \`ln -s\` copies directories)"
if [ -f /etc/fstab ]; then
    # CAPTURED FIRST, because `grep | sed | tee || note` can never reach the
    # fallback: the exit status of a pipeline is its LAST command's, and `tee`
    # succeeds whether or not grep matched anything. So the stock configuration --
    # no acl line at all, which is the single most important fact this rig can
    # report about an MSYS2 host -- would have printed nothing and read as though
    # the check had not run. Same shape as the guard in _rig_win.sh: absence and
    # success must not share a representation.
    _acl=$(grep -n 'acl' /etc/fstab 2>/dev/null)
    if [ -n "$_acl" ]; then
        note "    /etc/fstab acl lines:"
        printf '%s\n' "$_acl" | sed 's/^/      /' | tee -a "$RESULTS"
    else
        note "    /etc/fstab carries NO acl line -- this is the STOCK configuration."
        note "    chmod will be a no-op, jichi's file-privacy guarantees will not"
        note "    hold, and the failures that follow are its fences firing correctly"
        note "    rather than defects. That is the row a new user actually gets."
    fi
fi

jcw_report_modes "msys2 (user home)" "$HOME"
_home_ok=$JCW_MODES_OK
jcw_report_modes "msys2 (tier tmp)" "${TMPDIR:-/tmp}"
_tmp_ok=$JCW_MODES_OK

# THE FINDING THIS RIG EXISTS TO CATCH, and the only check here that no other
# platform needs. If /tmp honours modes and $HOME does not, the smoke tier will
# pass -- its isolated HOME lives under /tmp -- while the key file a real user
# writes stays world-readable. A green gate over an exposed secret is worse than
# a red one, because nobody goes looking.
if [ "$_tmp_ok" -eq 1 ] && [ "$_home_ok" -eq 0 ]; then
    bad "msys2: MODE SUPPORT DIVERGES -- /tmp honours chmod and \$HOME does not."
    note "    The smoke tier will PASS, because its isolated HOME is under /tmp,"
    note "    while a real user's ~/.jichi.env stays world-readable. This is the"
    note "    documented /tmp acl fix protecting the TESTS rather than the USER."
    note "    Measured M697 -- the line that covers a user is:"
    note "        C:/msys64/home /home ntfs binary,acl 0 0"
    note "    Mounting / instead does NOT work: the installation root is special."
elif [ "$_home_ok" -eq 1 ]; then
    ok "msys2: \$HOME honours chmod, so jichi's privacy guarantees hold here"
else
    note "    Both locations ignore modes: this is the STOCK configuration, and"
    note "    the privacy failures the tier reports are jichi's fences firing"
    note "    correctly rather than defects. Nine such at M697, all one cause."
fi

# The dangling-symlink capability, probed rather than remembered.
_dl=$(mktemp -d)
ln -s /jichi/no/such/target "$_dl/probe" 2>/dev/null
if [ -L "$_dl/probe" ]; then
    ok "msys2: dangling symlinks can be created (pathfence_dangling will run)"
else
    note "    msys2: \`ln -s\` to a non-existent target FAILS here, so"
    note "    pathfence_dangling skips. It does not report a fence hole any more:"
    note "    until M697 it tested the fence against a path that was not the link"
    note "    it thought it had made, and blamed jichi for its own fixture."
fi
rm -rf "$_dl"
note ""

# ------------------------------------------------------------------- build
say "build"
jcw_build_median "msys2" "$TREE" || exit 1
MULT=$(jc_rig_mult "$JCW_BUILD_MEDIAN" "$REF_SECS") || {
    bad "msys2: could not compute a multiplier from ${JCW_BUILD_MEDIAN}s / ${REF_SECS}s"
    note "    Refused rather than defaulted to 1: a wrong denominator does not"
    note "    degrade a row, it invalidates it."
    exit 1
}
ok "msys2: JC_SMOKE_TIMEOUT_MULT=$MULT (ceil ${JCW_BUILD_MEDIAN}s / ${REF_SECS}s)"
note "    State the denominator with the multiplier, always."
note ""

# ------------------------------------------------------------------- suites
say "unit suite"
if ( cd "$TREE" && make WERROR=1 test ) > "$DIR/unit.log" 2>&1; then
    ok "msys2: unit suite green -- $(grep -oE '[0-9]+ checks?, [0-9]+ failures?' "$DIR/unit.log" | tail -1)"
else
    bad "msys2: unit suite failed -- see $DIR/unit.log"
    grep 'FAIL' "$DIR/unit.log" | head -10 | sed 's/^/    /'
fi
note ""

say "smoke tier"
jcw_tier "msys2" "$TREE" "$MULT"
note ""

say "offline surfaces"
jcw_surfaces "msys2" "$TREE"
note ""

# -------------------------------------------------------------- live turns
say "live turns"
if [ -z "$LIVE_URL" ]; then
    jc_rig_live_skip
else
    note "    transport: $LIVE_URL reached DIRECTLY -- no tunnel, no guest."
    note "    This row is not a VM: the platform under test is this machine, so"
    note "    there is nothing to forward from. Every other Driven row used the"
    note "    loopback + ssh -R arrangement; this one did not, and says so."
    if jcw_free_or_die "$LIVE_URL" "$LIVE_MODEL" "$FREE_PREFIX" "${LIVE_KEY_ENV:-JICHI_API_KEY}"; then
        jcw_live "$ROW" "$TREE" "$LIVE_URL" "$LIVE_MODEL" \
                 "${LIVE_KEY_ENV:-JICHI_API_KEY}" "$DIR" || true
    fi
fi

note ""
note "== totals: $N_OK ok, $N_FAIL failed"
say "totals: $N_OK ok, $N_FAIL failed -- $RESULTS"
[ "$N_FAIL" -eq 0 ]
