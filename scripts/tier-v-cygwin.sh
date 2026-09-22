#!/bin/sh
# tier-v-cygwin.sh -- the Windows + Cygwin row: POSIX emulated over Win32.
#
# WHY THIS EXISTS ONLY NOW. `docs/DEFERRED.md` recommendation 4 -- "give Cygwin
# and MSYS2 a rig" -- is the oldest surviving live item on that page, and it
# stayed open on purpose. The Guix rule, carried there since May: an untested rig
# for a platform nobody can run here would be a never-executed artifact. This row
# ran by hand at M696 and was driven at M697, on the one machine in the project
# that carries WSL2, Cygwin and MSYS2 side by side. This is the transcript of
# that, not a guess at it.
#
# WHAT IT UNIQUELY EXERCISES, which no Linux or BSD row can:
#   * POSIX emulated over Win32 by cygwin1.dll -- and with tier-v-msys2.sh, the
#     SAME question asked of a second, independently maintained DLL, which is
#     what separates Cygwin-version behaviour from emulation behaviour;
#   * a case-insensitive filesystem, where two fixture files differing only in
#     case collapse into one (measured, M696: `bool_dialect` lost a file);
#   * `tcsetattr(TCSAFLUSH)` that does NOT discard pending input, so jichi
#     announced a type-ahead discard that had not happened and the stray line
#     became the user's first prompt -- a SAFETY property, found here and
#     nowhere else (M696);
#   * a fork penalty spanning 1.5x to ~300x WITHIN one tier, which is why this
#     rig states no single multiplier without also stating its denominator.
#
# THE DEADLINE IS NOT A CAP. `JC_SMOKE_TIMEOUT_MULT` is a multiplier on deadlines
# compiled into the tier, and a driver killed at one yields a BOUND, not a
# duration. Worse, a killed driver FABRICATES FINDINGS: the shell continues past
# a command substitution the kill emptied and prints `not ok` lines that accuse
# the product. Three such phantoms were chased at M696 before the mechanism was
# understood. This rig reports killed and failed as different things, always.
#
# Exit codes (the tier-v contract):
#   0  the row ran and every check passed
#   1  the row ran and something failed -- a RESULT, read results-cygwin.txt
#   2  usage, or a tool missing on the host
#   3  this is not a Cygwin shell -- NOT a result, the rig never reached the row
#
# Usage (--ref-secs is REQUIRED: THIS bench's serial `make WERROR=1` seconds):
#   scripts/tier-v-cygwin.sh --ref-secs 9.65
#   scripts/tier-v-cygwin.sh --dry-run
#   scripts/tier-v-cygwin.sh --ref-secs 9.65 \
#       --live https://api.hrz.uni-giessen.de/v1 --live-model jlu/qwen3.8-27b \
#       --live-key-env JICHI_API_KEY --free-prefix jlu/
set -u

. "$(dirname "$0")/_rig_mult.sh"
. "$(dirname "$0")/_rig_ship.sh"
. "$(dirname "$0")/_rig_live.sh"
. "$(dirname "$0")/_rig_win.sh"

ROW="cygwin"
REF_SECS="${JC_REF_SECS:-}"
DRY=0
LIVE_URL=""
LIVE_PORT=""
LIVE_MODEL="local"
LIVE_KEY_ENV=""
FREE_PREFIX=""
TREE="${JC_WIN_TREE:-$HOME/jichi}"
DIR="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}/cygwin"

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
        *) echo "tier-v-cygwin: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

[ -n "$LIVE_PORT" ] && [ -z "$LIVE_URL" ] && LIVE_URL="http://127.0.0.1:$LIVE_PORT/v1"

if [ "$DRY" -eq 1 ]; then
    echo "tier-v-cygwin: dry run -- nothing is built, measured or driven."
    echo "  tree      $TREE"
    echo "  results   $DIR/results-cygwin.txt"
    echo "  ref-secs  ${REF_SECS:-<required>}"
    echo "  live      ${LIVE_URL:-<none: the row would be Verified, not Driven>}"
    echo "  model     $LIVE_MODEL"
    echo "  key env   ${LIVE_KEY_ENV:-<none: keyless, as a loopback server is>}"
    echo "  free ns   ${FREE_PREFIX:-<unchecked>}"
    exit 0
fi

jc_rig_ref_or_die tier-v-cygwin "$REF_SECS" || exit 2

# ---------------------------------------------------------------- the host
# Refused rather than attempted. A Cygwin row measured from an MSYS2 or WSL2
# shell would be a row about a different platform wearing this one's name, and
# every number in it would be quoted later by somebody who was not here.
case "$(uname -s 2>/dev/null)" in
    CYGWIN_NT-*) ;;
    *) echo "tier-v-cygwin: this is not a Cygwin shell (uname -s = $(uname -s))." >&2
       echo "  Run it from Cygwin Terminal, with MSYS2 closed. Their DLL" >&2
       echo "  shared-memory regions collide and fork begins failing in both." >&2
       exit 3 ;;
esac

mkdir -p "$DIR"
# Every log this rig writes goes here, never into the tree it is measuring.
JCW_OUT="$DIR"
LIVE_KEY_ENV="${LIVE_KEY_ENV:-JICHI_API_KEY}"
RESULTS="$DIR/results-cygwin.txt"
: > "$RESULTS"
N_OK=0; N_FAIL=0
say()  { echo "== $*"; }
ok()   { N_OK=$((N_OK+1));     echo "ok - $*";     echo "ok   - $*" >> "$RESULTS"; }
bad()  { N_FAIL=$((N_FAIL+1)); echo "not ok - $*"; echo "FAIL - $*" >> "$RESULTS"; }
# `note` prints as well as records, unlike the VM rigs' results-only note: the
# operator of a local rig is sitting at the machine being measured, and the
# prerequisite warnings below are worth nothing an hour later in a file.
note() { echo "$*"; echo "$*" >> "$RESULTS"; }

note "== tier-v-cygwin, $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
note "uname: $(uname -a 2>/dev/null)"
note "$(jc_rig_ship_stamp "$TREE" 1)"
note "reference bench: ${REF_SECS}s serial make WERROR=1"
note ""

# ------------------------------------------------------------ prerequisites
jcw_key_stash "$LIVE_KEY_ENV"

say "prerequisites"
jcw_guard_other_layer "cygwin" "C:\\msys64" || exit 1
jcw_prereqs "cygwin" || exit 1
jcw_report_modes "cygwin" "$HOME"
[ -d "$TREE" ] || { bad "no tree at $TREE -- clone one with core.autocrlf=false"; exit 1; }
ok "cygwin: tree present at $TREE"
note ""

# ------------------------------------------------------------------- build
# Three clean builds and the MEDIAN, because a single build is a sample of one
# and this number becomes a published denominator.
say "build"
jcw_build_median "cygwin" "$TREE" || exit 1
MULT=$(jc_rig_mult "$JCW_BUILD_MEDIAN" "$REF_SECS") || {
    bad "cygwin: could not compute a multiplier from ${JCW_BUILD_MEDIAN}s / ${REF_SECS}s"
    note "    Refused rather than defaulted to 1: a wrong denominator does not"
    note "    degrade a row, it invalidates it."
    exit 1
}
ok "cygwin: JC_SMOKE_TIMEOUT_MULT=$MULT (ceil ${JCW_BUILD_MEDIAN}s / ${REF_SECS}s)"
note "    STATE THE DENOMINATOR WITH THE MULTIPLIER, always. The old published"
note "    figure of 10 came from 130s/13s with both operands compile-inclusive,"
note "    so it was not a runtime ratio at all. And on this row the per-driver"
note "    ratio spans 1.5x to ~300x, so no single number describes it."
note ""

# ------------------------------------------------------------------- suites
say "unit suite"
if ( cd "$TREE" && make WERROR=1 test ) > "$DIR/unit.log" 2>&1; then
    ok "cygwin: unit suite green -- $(grep -oE '[0-9]+ checks?, [0-9]+ failures?' "$DIR/unit.log" | tail -1)"
else
    bad "cygwin: unit suite failed -- see $DIR/unit.log"
    grep 'FAIL' "$DIR/unit.log" | head -10 | sed 's/^/    /'
fi
note ""

say "smoke tier"
jcw_tier "cygwin" "$TREE" "$MULT"
note ""

say "offline surfaces"
jcw_surfaces "cygwin" "$TREE"
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
