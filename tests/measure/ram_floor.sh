#!/bin/sh
# tests/measure/ram_floor.sh -- the lowest cgroup MemoryMax at which a real
# headless turn still COMPLETES (M403; the M265 method, scripted).
#
# NOT a gate and not in `make smoke`: it needs cgroup-v2 memory delegation, it
# deliberately provokes OOM kills, and its answer is a measurement of THIS
# machine's kernel + libc + libcurl, not a property of jichi. It lives here with
# soak.py and repeat_failures.py for the same reason.
#
# WHAT IT MEASURES, PRECISELY -- and this is the whole point of the script,
# because the number is easy to overstate:
#
#   A cgroup ceiling on a 4.9 GB box is NOT a 64 MB machine. The host page cache
#   still holds libcurl and the TLS chain, so file-backed pages evicted under the
#   ceiling are re-faulted from RAM rather than from a slow eMMC; the kernel's own
#   footprint is outside the ceiling entirely; and there is no memory pressure
#   from anything else on the system. A real 64 MB board pays all three.
#
#   So a floor measured here is a LOWER BOUND on what a real machine of that size
#   needs -- useful, honest, and not a substitute for the board. docs/PLATFORMS.md
#   keeps that distinction; docs/LOW_MEMORY.md states it beside every figure.
#
# The model is a local mockmodel started OUTSIDE the ceiling, so the figure is
# jichi's turn and not the harness's. Completion is verified by the ANSWER TEXT,
# never by an exit code -- a truncated run can exit 0 (docs/TEST_INTEGRITY.md).
#
# FOUR WORKLOADS, because they are four different numbers and the plan that
# asked for this said so: docs/plans/2026-07-hardware-testing.md (V1) asks to
# "Record three floors, they are different numbers: offline subcommand
# (doctor), one headless turn against a mock, and the full check-target".
# Until 2026-08-13 this script measured only the turn, which is why
# docs/LOW_MEMORY.md still read "the whole smoke tier ... passes at 32 MB; not
# bisected lower" -- nobody had an instrument for that row.
#
#   turn   (default) a full headless turn: model call, tool call, answer
#   doctor the offline subcommand -- no turn, but libcurl and TLS are initialized
#   units  ./run_tests, the whole unit suite
#   smoke  tests/smoke/run.sh, the whole POSIX-sh tier
#
# WHAT EACH FLOOR INCLUDES, since a floor is only meaningful with its scope:
# `turn` and `doctor` measure ONE jichi process (the mock model runs outside the
# ceiling). `units` measures one process too. `smoke` measures the whole tier --
# run.sh forks jichi, mockmodel and PTY drivers *inside* the ceiling, so its
# floor is a pipeline's, not a process's, and it is legitimately the highest.
# For units/smoke the binaries are built BEFORE the ceiling is applied, so no
# compiler is inside it: this measures running, never compiling.
#
# THE `units` FLOOR IS A FILESYSTEM MEASUREMENT AS MUCH AS A MEMORY ONE, and
# whoever quotes it must say which. Measured 2026-08-13 on threadwork: 72 MB,
# against the 14 MB docs/LOW_MEMORY.md published one day earlier. That is not a
# regression. tests/test_bounds.c:117 writes a session fixture of
# JC_READ_FILE_MAX + 4096 -- 64 MiB + 4 KB -- to prove jc_read_file refuses an
# over-cap file, and it hardcodes /tmp. Where /tmp is **tmpfs** those pages are
# charged to the cgroup, so the ceiling must exceed the fixture. Proven with no
# jichi in the picture: a bare `dd` of 64 MiB survives a 72 MB ceiling on tmpfs,
# is OOM-killed at 64 MB, and survives 64 MB when written to a disk-backed path
# (disk page cache is reclaimable; tmpfs is not).
#
# So: on a box whose /tmp is disk-backed this reports jichi's own working set
# (~14 MB), and on one whose /tmp is tmpfs it reports max(that, the fixture).
# `findmnt -no FSTYPE /tmp` before believing either. It also means a small board
# with a tmpfs /tmp needs >=72 MB to run `make check-target` at all, whatever
# jichi itself costs -- relevant to every tier below that.
#
# Usage: tests/measure/ram_floor.sh [--workload turn|doctor|units|smoke]
#                                   [ceiling_MB ...]
#        (default workload `turn`, and a per-workload descending sweep)
set -u

WORKLOAD=turn
CEIL_ARGS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --workload) shift
            case "${1:-}" in
                turn|doctor|units|smoke) WORKLOAD=$1 ;;
                *) echo "ram_floor: --workload must be turn|doctor|units|smoke" >&2
                   exit 2 ;;
            esac ;;
        -h|--help) awk 'NR>1 && !/^#/{exit} NR>1' "$0"; exit 0 ;;
        -*) echo "ram_floor: unknown option: $1" >&2; exit 2 ;;
        *)  CEIL_ARGS="$CEIL_ARGS $1" ;;
    esac
    shift
done

root=$(cd "$(dirname "$0")/../.." && pwd)
BIN="$root/jichi"
TOOLS="$root/tests/tools"
MARK=RAM_FLOOR_ANSWER_OK

# Per-workload default sweeps: starting a turn's sweep at 128 wastes four rungs,
# and starting smoke's at 8 finds nothing. Each starts comfortably above the
# figure docs/LOW_MEMORY.md currently publishes for it.
if [ -n "${CEIL_ARGS# }" ]; then
    CEILINGS=$CEIL_ARGS
else
    case "$WORKLOAD" in
        turn)   CEILINGS="128 96 64 48 32 24 16 12 8 6 4 3 2" ;;
        doctor) CEILINGS="64 48 32 24 16 12 8 6 5 4 3 2" ;;
        units)  CEILINGS="64 48 32 24 20 16 14 12 10 8" ;;
        smoke)  CEILINGS="256 192 128 96 64 48 32 24 16" ;;
    esac
fi

[ -x "$BIN" ] || { echo "no $BIN -- run make first" >&2; exit 2; }
[ -x "$TOOLS/mockmodel" ] || { echo "no mockmodel -- run make smoke-tools" >&2; exit 2; }
if [ "$WORKLOAD" = units ] && [ ! -x "$root/run_tests" ]; then
    # `make` alone does NOT build the test binary -- it builds the product. A
    # measurement that silently re-ran a stale run_tests would be worse than
    # this refusal (docs/TEST_INTEGRITY.md; the same trap cost M286 five
    # consecutive false verifications).
    echo "no $root/run_tests -- run 'make run_tests' (NOT plain 'make')" >&2
    exit 2
fi
command -v systemd-run >/dev/null 2>&1 || { echo "systemd-run absent" >&2; exit 2; }
case "$(cat "/sys/fs/cgroup/user.slice/user-$(id -u).slice/cgroup.controllers" 2>/dev/null)" in
    *memory*) ;;
    *) echo "no delegated cgroup-v2 memory controller for this user" >&2; exit 2 ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/ramfloor.XXXXXX") || exit 2
trap 'kill "$MM_PID" 2>/dev/null; rm -rf "$tmp"' EXIT INT TERM
HOME=$tmp/home; export HOME; mkdir -p "$HOME"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  text $MARK
EOF

mkdir -p "$tmp/cap"
"$TOOLS/mockmodel" --script "$tmp/replies.mm" --capture "$tmp/cap" \
    --port-file "$tmp/cap/.port" --deadline 300 >/dev/null &
MM_PID=$!
i=0
while [ ! -s "$tmp/cap/.port" ]; do
    kill -0 "$MM_PID" 2>/dev/null || { echo "mockmodel died" >&2; exit 2; }
    i=$((i + 1)); [ "$i" -gt 10 ] && { echo "mockmodel silent" >&2; exit 2; }
    sleep 1
done
PORT=$(cat "$tmp/cap/.port")

cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"markdown":false,
"lowResource":true,"maxRetries":0}
EOF

# One workload = one command plus the POSITIVE MARKER that proves it finished.
# The marker is the whole discipline of this script: a truncated or OOM-killed
# run can still exit 0, so a verdict is never read from an exit code
# (docs/TEST_INTEGRITY.md). Each marker below is a string the workload prints
# only on success, taken from its real output rather than guessed.
case "$WORKLOAD" in
    turn)
        DESC="one headless turn (model call, tool call, answer)"
        MARKER=$MARK ;;
    doctor)
        DESC="the offline doctor subcommand (libcurl + TLS initialized)"
        # doctor's FINAL summary line ("N ok, M warnings, K problems"), verified
        # to be the only line containing the word. The obvious marker --
        # "libcurl available" -- is printed on doctor's SECOND line, so a run
        # OOM-killed two thirds of the way through still prints it and would
        # have scored COMPLETE. That is the failure docs/TEST_INTEGRITY.md names
        # and that M403 hit twice in one hour writing this very harness: the
        # assertion matched, but not the thing it named.
        MARKER=" problems" ;;
    units)
        # READ THE FILESYSTEM WARNING BELOW BEFORE QUOTING THIS NUMBER.
        DESC="the unit suite (./run_tests) -- floor depends on /tmp's filesystem"
        MARKER="checks, 0 failures" ;;
    smoke)
        DESC="the whole smoke tier (forks jichi + mock + PTY INSIDE the ceiling)"
        MARKER="smoke: OK (" ;;
esac

run_workload() {  # run_workload <mb>; stdout = the workload's output
    case "$WORKLOAD" in
        turn)
            systemd-run --user --quiet --scope --collect \
                -p MemoryMax="$1M" -p MemorySwapMax=0 \
                "$BIN" --config "$tmp/config.json" --no-session -q --lite \
                -p "ram floor probe" < /dev/null 2>"$tmp/err" ;;
        doctor)
            systemd-run --user --quiet --scope --collect \
                -p MemoryMax="$1M" -p MemorySwapMax=0 \
                "$BIN" --config "$tmp/config.json" doctor \
                < /dev/null 2>"$tmp/err" ;;
        units)
            systemd-run --user --quiet --scope --collect \
                -p MemoryMax="$1M" -p MemorySwapMax=0 \
                "$root/run_tests" < /dev/null 2>"$tmp/err" ;;
        smoke)
            # run.sh, not `make smoke`: the binaries are already built, and
            # putting make inside the ceiling would measure make too.
            ( cd "$root" && systemd-run --user --quiet --scope --collect \
                -p MemoryMax="$1M" -p MemorySwapMax=0 \
                sh tests/smoke/run.sh < /dev/null 2>"$tmp/err" ) ;;
    esac
}

echo "workload: $WORKLOAD -- $DESC"
echo "verified by: \"$MARKER\" in the output (never an exit code)"
echo
printf 'ceiling  verdict   detail\n'
floor=""
for mb in $CEILINGS; do
    rm -f "$tmp/cap/.port.used"
    out=$(run_workload "$mb")
    rc=$?
    case "$out" in
        *"$MARKER"*) printf '%5s MB  COMPLETE  marker verified\n' "$mb"; floor=$mb ;;
        *) why=$(head -c 90 "$tmp/err" | tr '\n' ' ')
           printf '%5s MB  FAILED    rc=%s %s\n' "$mb" "$rc" "${why:-no marker}"
           break ;;
    esac
done

echo
if [ -n "$floor" ]; then
    echo "$WORKLOAD: lowest ceiling with a VERIFIED marker: ${floor} MB"
    echo "read as a LOWER BOUND for a real machine of that size (see the header)"
    [ "$floor" = "${CEILINGS%% *}" ] && echo \
        "NOTE: that is the TOP of the sweep -- the floor may be lower; widen it downward"
else
    echo "$WORKLOAD: no ceiling in the sweep completed -- widen the sweep upward"
fi
