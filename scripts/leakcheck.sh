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
# Usage:  scripts/leakcheck.sh [path/to/jichi] [path/to/jichi-harness]
# The jichi binary must be a SAN=1 build; this refuses a non-sanitized one
# rather than reporting a clean run it never actually checked. The harness
# argument is optional (it defaults to jichi-harness beside the jichi binary):
# when present it is checked the same way, over EVERY tests/smoke/harness_*.sh
# driver (M1058, SV:1158.2/SV:1200.3), because the jichi SUBS list above
# assumes jichi's config, which the harness does not read.
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
else
    echo "leakcheck: $fail of $ran subcommand(s) leak" >&2
fi

# If the jichi loop found leaks, report them now -- the harness below is an
# ADDITIONAL check, and a script that found a leak in the first loop must not
# keep going as if the run were clean.
[ "$fail" -eq 0 ] || exit 1

# --- the harness: jichi's third program, its own binary -----------------------
# leakcheck.sh [jichi] [jichi-harness] -- a second binary, because the SAN
# stage builds BOTH (`make SAN=1 CC=clang jichi jichi-harness`).
# M1058 (SV:1158.2/SV:1200.3): the section no longer runs just `--version` --
# it runs EVERY tests/smoke/harness_*.sh driver against the SAN binaries, the
# way the smoke tier does but here under the checkers: JC_SMOKE_HBIN points
# each driver at the sanitizer harness and JC_SMOKE_BIN at the sanitizer jichi
# (the drivers run a real jichi child, e.g. harness_run's happy run). The
# drivers are ENUMERATED by glob, never a hardcoded list (a list misses the
# next driver by construction). A stage that runs only `--version` had seen
# none of the spawn/argv paths: `run`'s team_head wrote argv[14] on a
# char *[14] (UBSan) and the mirror push's check 4 read e->d_name after
# closedir (ASan) -- both on `--version`-inert code. The same refusal of a
# non-sanitized binary and the same floor carry over unchanged -- a clean run
# over a binary that was never checked, or over one that ran nothing, is the
# green this script exists to not print.

HBIN="${2:-$ROOT/jichi-harness}"
if [ ! -x "$HBIN" ]; then
    # The jichi loop already reported its own verdict; a missing harness is a
    # refusal (the build stage did not make it), not a leak.
    echo "leakcheck: no harness binary at $HBIN -- build it with: make SAN=1 CC=clang jichi-harness" >&2
    exit 2
fi
case "$HBIN" in
    /*) ;;
    *)  HBIN="$(cd "$(dirname "$HBIN")" && pwd)/$(basename "$HBIN")" ;;
esac
if ! strings "$HBIN" 2>/dev/null | grep -qE "LeakSanitizer|__asan_"; then
    echo "leakcheck: $HBIN is not a sanitizer build -- a clean result would mean" >&2
    echo "  'nothing was checked'. Build with: make clean && make SAN=1 CC=clang jichi-harness" >&2
    exit 2
fi

# The smoke helpers the drivers spawn (mockmodel for harness_run's real jichi
# child, ptydrive/jsonq/sockq for the rest of the tier): the ci line above
# builds only the two SAN binaries, so build them when absent. The tools are
# fixtures, not the programs under the checkers -- a plain build is the
# smoke tier's own state for them (run.sh requires their PRESENCE, never
# their sanitization).
for t in mockmodel ptydrive jsonq sockq; do
    if [ ! -x "$ROOT/tests/tools/$t" ]; then
        make -C "$ROOT" smoke-tools || exit 1
        break
    fi
done

hran=0
hfail=0
hcount=0
# Every harness driver, enumerated (a driver added to tests/smoke/harness_*.sh
# is picked up by the next run; no list to update). Each one runs its full TAP
# plan against the SAN binaries; a driver whose TAP went nowhere did not run,
# and a sanitizer report (a LEAK, an ASan ERROR, or a UBSan `runtime error`
# -- the two classes M1058's one-off proofs caught) fails the stage.
for drv in "$ROOT"/tests/smoke/harness_*.sh; do
    [ -e "$drv" ] || continue   # the glob matched nothing: no drivers at all
    hcount=$((hcount + 1))
    hname=$(basename "$drv" .sh)
    herr="$WORK/hdrv.$hname"
    ( cd "$WORK/ws" && ASAN_OPTIONS=detect_leaks=1 \
        JC_SMOKE_HBIN="$HBIN" JC_SMOKE_BIN="$BIN" \
        sh "$drv" > "$herr.out" 2> "$herr" ) || true
    # A driver that printed no TAP plan did not run (a missing helper, a
    # shell error): the same "0 output is not 0 findings" rule as the jichi
    # loop. t_skip prints `1..0`, so a legitimate skip still counts as a run.
    if ! grep -q '^1\.\.[0-9]' "$herr.out" 2>/dev/null; then
        hfail=$((hfail + 1))
        echo "leakcheck: harness driver \`$hname\` printed no TAP plan -- it did not run" >&2
        sed 's/^/    /' "$herr" 2>/dev/null | head -14
        continue
    fi
    hran=$((hran + 1))
    # Both streams: the harness children's sanitizer reports land in the
    # driver's stderr captures, and the drivers quote them into their t_fail
    # messages on stdout (a driver that aborts under ASan/UBSan names it
    # there). A signature in EITHER is a red stage.
    if grep -qE "ERROR: LeakSanitizer|ERROR: AddressSanitizer|runtime error" \
        "$herr" "$herr.out" 2>/dev/null; then
        hfail=$((hfail + 1))
        echo "leakcheck: SANITIZER REPORT in harness driver \`$hname\`"
        grep -A6 -E "ERROR: LeakSanitizer|ERROR: AddressSanitizer|runtime error" \
            "$herr" "$herr.out" 2>/dev/null | head -14 | sed 's/^/    /'
    fi
done

# The harness floor: at least one driver must have RUN -- the same "0 is
# indistinguishable from 0" reason as the jichi one (a renamed glob pattern
# matches nothing and the loop above would print a cheerful OK).
if [ "$hran" -lt 1 ]; then
    echo "leakcheck: no harness driver ran; the enumeration is broken, not the tree clean" >&2
    exit 1
fi
if [ "$hfail" -eq 0 ]; then
    echo "leakcheck: harness OK ($hran of $hcount driver(s), no sanitizer reports)"
else
    echo "leakcheck: harness: $hfail of $hran run driver(s) (of $hcount) red" >&2
    exit 1
fi
exit 0
