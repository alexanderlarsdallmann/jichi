#!/bin/sh
# pin-driver.sh -- install a jichi binary somewhere its own build cannot reach.
#
# THE PROBLEM THIS SOLVES. Driving jichi headlessly at another project, from
# jichi's own checkout, means `../jichi/jichi` -- which `make clean` deletes.
# In one session that produced a bare `RC=127` mid-task: the agent run had not
# failed, the binary had simply evaporated under it while `make ci` rebuilt the
# tree. The two activities cannot share a tree, and the fix is not scheduling
# discipline (which failed repeatedly) but a copy the build cannot touch.
#
# So: build once, copy the binary AND a config to a prefix outside the
# repository, and drive from there. `make clean`, `make ci`, a `git checkout`
# and a branch switch all become irrelevant to a run in flight.
#
# The pinned copy is a SNAPSHOT and says so: it records the commit it was built
# from, so a row or a dogfood run can name the revision that produced it rather
# than whatever HEAD happens to be by the time anyone reads the log.
#
# Usage:
#   scripts/pin-driver.sh                       # -> ~/.local/opt/jichi-driver
#   scripts/pin-driver.sh --prefix DIR
#   scripts/pin-driver.sh --config FILE         # also pin a config to use
#   scripts/pin-driver.sh --print               # print the pinned binary path
#
# Then:
#   ~/.local/opt/jichi-driver/jichi --config ~/.local/opt/jichi-driver/config.json ...
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX="${JICHI_DRIVER_PREFIX:-$HOME/.local/opt/jichi-driver}"
CONFIG=""
PRINT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift ;;
        --config) CONFIG="$2"; shift ;;
        --print)  PRINT=1 ;;
        --help|-h) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "pin-driver: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done

if [ "$PRINT" -eq 1 ]; then
    echo "$PREFIX/jichi"
    exit 0
fi

# The prefix must not live inside the repository, or the build reaches it and
# the whole point is lost.
case "$(cd "$(dirname "$PREFIX")" 2>/dev/null && pwd)/" in
    "$ROOT"/*) echo "pin-driver: refusing a prefix inside the repository ($PREFIX)" >&2
               echo "  the build would delete it, which is the problem this avoids" >&2
               exit 2 ;;
esac

echo "== building $ROOT"
( cd "$ROOT" && make WERROR=1 jichi ) >/dev/null 2>&1 || {
    echo "pin-driver: build failed -- not pinning a broken binary" >&2
    exit 1
}
[ -x "$ROOT/jichi" ] || { echo "pin-driver: no binary after build" >&2; exit 1; }

mkdir -p "$PREFIX"
cp "$ROOT/jichi" "$PREFIX/jichi.new" && mv "$PREFIX/jichi.new" "$PREFIX/jichi"

REV=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)
DIRTY=$(git -C "$ROOT" status --porcelain 2>/dev/null | wc -l)
{
    echo "pinned:   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "from:     $ROOT"
    echo "revision: $REV"
    echo "dirty:    $DIRTY file(s) uncommitted at pin time"
} > "$PREFIX/PINNED"

if [ -n "$CONFIG" ]; then
    [ -r "$CONFIG" ] || { echo "pin-driver: cannot read $CONFIG" >&2; exit 2; }
    cp "$CONFIG" "$PREFIX/config.json"
    echo "config:   $CONFIG" >> "$PREFIX/PINNED"
fi

echo "== pinned $REV -> $PREFIX/jichi"
[ -n "$CONFIG" ] && echo "   config -> $PREFIX/config.json"
"$PREFIX/jichi" --version 2>/dev/null | head -1
cat "$PREFIX/PINNED"
