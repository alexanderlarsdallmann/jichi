#!/bin/sh
# mutant-sweep.sh -- prove a smoke driver can notice a binary that does nothing.
#
# WHAT IT DOES. Replaces $BIN with a shell script that prints nothing and exits 0 --
# the purest "hollow green" a binary can offer -- and runs each driver against it.
# Every driver that exercises the binary MUST go red. One that stays green is
# measuring something other than jichi: its own fixtures, the shell, or nothing.
#
# WHY IT EXISTS. Vacuous checks are this project's most persistent defect class:
# a check that passes while covering less than its header claims. Six instances in
# one session (docs/analysis/2026-08-22-learning-from-errors.md), and the only
# instruments that ever caught them were a floored extraction and the red-before-
# green ritual -- both of which depend on the author remembering. A syntactic lint
# was tried first and ABANDONED after measurement: three successive formulations
# ("assert-zero with no floor", "assert-zero over a swallowed stderr", "no numeric
# floor anywhere") each produced false positives on the first files inspected,
# because the vulnerable unit is a CHECK and grep can only see a FILE. That result
# is the reason this script exists in its place: it needs no per-check authoring at
# all, and it asks the only question that matters -- if the product vanished, would
# this driver notice?
#
# WHAT IT FOUND. Sweeping all 211 drivers: 202 red, 9 green, of which 8 are the
# legitimate exclusions below and ONE was a real defect -- parallel_abort.sh
# asserted only that a process "exited promptly" after SIGINT, which a binary that
# does nothing satisfies perfectly, while its header claimed it verified that two
# forked children were reaped. Fixed at M540 by giving it a denominator.
#
# SCOPE. By default it sweeps only the drivers git reports as added or modified,
# because that is when a vacuous check is INTRODUCED and the cost is seconds.
# MUTANT_ALL=1 sweeps everything (~9 minutes, and worth running before a release).
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

# Drivers that legitimately stay green against a hollow binary, each with the
# reason. NOT a convenience list: every entry is a driver whose checks are
# structural (they read source or docs) or which skips without a special build.
# A driver added here that does exercise the binary is a coverage hole hidden by
# an exclusion, so tests/smoke/smoke_lint.sh checks this table for stale entries.
#
#   faults, faults_net, faults_net_midstream -- SKIP without a FAULT=1 binary
#   smoke_lint, project_records_lint, doc_commands_lint -- source/doc lints that
#       mention "$BIN" in passing but assert over files, not over behaviour
EXCLUDE='faults faults_net faults_net_midstream smoke_lint project_records_lint
doc_commands_lint'

tmp=${TMPDIR:-/tmp}/jc_mutant.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT INT TERM
mutant="$tmp/jichi"
printf '#!/bin/sh\nexit 0\n' > "$mutant"
chmod +x "$mutant"

# The driver list: changed-only by default, everything under MUTANT_ALL=1.
#
# M579: THE SELECTOR IGNORES COMMENTS. It used to `grep -l '"$BIN"'` over whole
# files, which matched a driver whose ONLY mention of the binary was a sentence
# ABOUT it -- project_records_lint's header says "...driver to invoke \"$BIN\")".
# It was swept, could not possibly notice a mute binary because it never runs
# one, and was reported as a failure. A selector that reads prose selects the
# wrong set, which is the same defect this sweep exists to catch, one level up.
uses_bin() {   # does this driver INVOKE the binary, not merely mention it?
    sed -e 's|#.*$||' "$1" | grep -q '"\$BIN"'
}
if [ -n "${MUTANT_ALL:-}" ]; then
    list=""
    for f in tests/smoke/*.sh; do
        uses_bin "$f" || continue
        list="$list $f"
    done
else
    changed=$(git status --porcelain tests/smoke 2>/dev/null \
              | awk '{print $NF}' | grep '\.sh$' || true)
    # HEAD's own diff too, so a driver committed a moment ago is still covered.
    head_diff=$(git diff-tree --no-commit-id --name-only -r HEAD 2>/dev/null \
                | grep '^tests/smoke/.*\.sh$' || true)
    list=""
    for f in $changed $head_diff; do
        [ -f "$f" ] || continue
        uses_bin "$f" || continue
        case " $list " in *" $f "*) continue ;; esac
        list="$list $f"
    done
fi

nrun=0; nskip=0; bad=""
for f in $list; do
    b=$(basename "$f" .sh)
    # _smoke.sh is the harness and run.sh is the tier RUNNER, not drivers --
    # excluded structurally, exactly as tests/smoke/smoke_lint.sh excludes them,
    # rather than by name in the table below. run.sh in particular runs the whole
    # tier, so a hollow binary makes its own children fail while it reports their
    # failures and exits 0, which is correct behaviour for a runner and would have
    # been a permanent false positive here.
    case "$b" in _smoke|run) continue ;; esac
    case " $EXCLUDE " in *" $b "*) nskip=$((nskip + 1)); continue ;; esac
    out=$(JC_SMOKE_BIN="$mutant" timeout "${MUTANT_TIMEOUT:-120}" sh "$f" 2>&1 || true)
    nrun=$((nrun + 1))
    if printf '%s' "$out" | grep -q '^not ok'; then
        printf 'ok - %s notices a hollow binary\n' "$b"
    else
        printf 'not ok - %s STAYS GREEN against a binary that does nothing\n' "$b"
        printf '#   %s\n' "$(printf '%s' "$out" | tr '\n' ' ' | cut -c1-200)"
        bad="$bad $b"
    fi
done

if [ "$nrun" = "0" ]; then
    printf 'mutant: no changed drivers to sweep (%d excluded)\n' "$nskip"
    exit 0
fi
if [ -n "$bad" ]; then
    printf 'mutant: FAILED --%s stayed green (%d swept, %d excluded)\n' \
        "$bad" "$nrun" "$nskip" >&2
    printf 'mutant: a driver that passes without the product is measuring its own\n' >&2
    printf 'mutant: fixtures. Give it a denominator -- see scripts/mutant-sweep.sh.\n' >&2
    exit 1
fi
printf 'mutant: OK (%d drivers swept, %d excluded)\n' "$nrun" "$nskip"
