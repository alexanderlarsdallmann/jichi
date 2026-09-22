#!/bin/sh
# leakcheck.sh - run jichi's SUBCOMMANDS under LeakSanitizer (M693).
#
# WHY THIS EXISTS. `make ci`'s sanitizer stage is `make SAN=1 CC=clang test` --
# the unit suite. The unit suite never enters `main()`'s subcommand dispatch, so
# nothing in the gate had ever run `jichi doctor` under a leak checker. What was
# there when somebody finally did:
#
#   Direct leak of 512 byte(s)  jc_list_dir   <- run_doctor
#   Direct leak of 384 byte(s)  jc_calib_load <- main
#
# The bytes do not matter -- the process exits immediately. What matters is that
# a leak CHECKER could not be used on these paths at all: a real leak introduced
# tomorrow would arrive as two more lines in a report that already had some. A
# tool that always complains is a tool nobody runs, which is the same argument
# M689 made about a warning that fires on `ls`.
#
# THE EXIT CODE OF THE SUBCOMMAND IS DELIBERATELY IGNORED. `doctor` returns 1
# when any check FAILs, and the fixture config below points at a port nothing
# listens on precisely so the network is never touched -- so a non-zero exit is
# the EXPECTED state here. What is asserted is the absence of a LeakSanitizer
# report. Conflating the two would make this script fail for the wrong reason
# on the very configuration that keeps it hermetic.
#
# Usage:  scripts/leakcheck.sh [path/to/jichi]
# The binary must be a SAN=1 build; this refuses a non-sanitized one rather than
# reporting a clean run it never actually checked.
set -e
BIN="${1:-./jichi}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -x "$BIN" ] || BIN="$ROOT/jichi"
# ABSOLUTE, BEFORE THE LOOP. The loop runs each subcommand from a temp
# workspace (`cd "$WORK/ws"`), and a relative `./jichi` stops resolving the
# moment it does -- the command then fails, `|| true` swallows it, and the run
# is counted as clean. This script reported "OK (7 subcommands, no
# LeakSanitizer reports)" against a binary with a deliberately reintroduced
# leak, which is the exact failure it exists to prevent, inside the fix for it.
# Caught by the tooth rather than by review.
case "$BIN" in
    /*) ;;
    *)  BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" ;;
esac

if [ ! -x "$BIN" ]; then
    echo "leakcheck: no binary at $BIN -- build one with: make SAN=1 CC=clang jichi" >&2
    exit 2
fi

# REFUSE A NON-SANITIZED BINARY. Without this the loop below runs, finds no
# LeakSanitizer output because there is no LeakSanitizer, and reports success --
# a green that means "nothing was checked". That is the failure this whole
# script is about, and it would be embarrassing to ship it inside the fix.
if ! strings "$BIN" 2>/dev/null | grep -qE "LeakSanitizer|__asan_"; then
    echo "leakcheck: $BIN is not a sanitizer build -- a clean result would mean" >&2
    echo "  'nothing was checked'. Build with: make clean && make SAN=1 CC=clang jichi" >&2
    exit 2
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
cat > "$WORK/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"]}],
"repoMap":false,"references":false,"toolProfile":"full","lowResource":false}
EOF
mkdir -p "$WORK/ws"

# Every subcommand that runs to completion without a server, a TTY or a prompt.
SUBS="--version doctor models describe context assignments skills"

fail=0
ran=0
for sub in $SUBS; do
    err="$WORK/err.$(printf '%s' "$sub" | tr -c 'a-zA-Z0-9' '_')"
    ( cd "$WORK/ws" && ASAN_OPTIONS=detect_leaks=1 \
        "$BIN" --config "$WORK/config.json" $sub < /dev/null > "$err.out" 2> "$err" ) || true
    # A subcommand that produced NOTHING on either stream did not run -- an
    # unresolvable binary, a usage error. Counting it as inspected is how the
    # relative-path defect above hid: `ran` reached 7 while nothing executed.
    if [ -s "$err.out" ] || [ -s "$err" ]; then
        ran=$((ran + 1))
    else
        echo "leakcheck: \`jichi $sub\` produced no output at all -- it did not run" >&2
    fi
    if grep -q "ERROR: LeakSanitizer" "$err" 2>/dev/null; then
        fail=$((fail + 1))
        echo "leakcheck: LEAK in \`jichi $sub\`"
        grep -A6 -E "Direct leak|Indirect leak" "$err" | head -14 | sed 's/^/    /'
    fi
done

# A FLOOR, for the same reason every lint in this tree has one: if the loop ran
# nothing -- a renamed binary, an empty SUBS -- "0 leaks" is indistinguishable
# from "0 subcommands", and the cheerful branch is the wrong one to take.
if [ "$ran" -lt 5 ]; then
    echo "leakcheck: only $ran subcommand(s) ran; expected at least 5 -- the" >&2
    echo "  extraction is broken, not the tree clean" >&2
    exit 1
fi

if [ "$fail" -eq 0 ]; then
    echo "leakcheck: OK ($ran subcommands, no LeakSanitizer reports)"
    exit 0
fi
echo "leakcheck: $fail of $ran subcommand(s) leak" >&2
exit 1
