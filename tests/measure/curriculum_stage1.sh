#!/bin/sh
# curriculum_stage1.sh -- one series of curriculum attempts, as measured on 2026-10-04
# (docs/analysis/2026-10-04-curriculum-stage1.md; plan 2026-10-03, step 6.1). A measurement,
# never a CI gate.
#
# Usage: sh tests/measure/curriculum_stage1.sh OUT LABEL CONFIG SPEC...
#   OUT     a directory OUTSIDE any repository; one subdirectory per series, one per attempt
#   LABEL   the series name (e.g. u2-qwen3.5-9b)
#   CONFIG  a jichi config file, copied in as the attempt's ~/.jichi
#   SPEC    assignment names without .md (e.g. 01-find-the-setting)
# Environment: JICHI (default: jichi on PATH), JC_REV (default: HEAD) -- the jichi tree each
# attempt works in is `git archive JC_REV` of the checkout this script lives in.
#
# Each attempt gets a fresh copy of the tree as its own git repository, a fresh HOME holding only
# CONFIG, and a private TMPDIR and Zig cache, so nothing carries from one attempt to the next.
# Caps OFF (measure first, then cap): no --deadline, no --budget-tokens, no --max-tool-calls, no
# timeout wrapper -- put "timeouts": {"stall": 0, "request": 0} and a "maxToolIters" far out of
# reach in CONFIG. Fences ON: attempt's own worktree, auto mode and path fence.
# Per attempt, under OUT/LABEL/SPEC/: result.json (attempt --output json), telemetry.jsonl, stderr.txt,
# before.txt/after.txt (time and load), rc, outer_wall_s, changed.txt, diff.patch, untracked.tar, runs/.
out=$1; label=$2; cfg=$3; shift 3
[ -n "$out" ] && [ -n "$label" ] && [ -f "$cfg" ] && [ $# -gt 0 ] || {
    echo "usage: sh tests/measure/curriculum_stage1.sh OUT LABEL CONFIG SPEC..." >&2; exit 2; }
J=${JICHI:-jichi}
src=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cur-stage1.XXXXXX") || exit 1
mkdir -p "$work/tmpl" && (cd "$src" && git archive "${JC_REV:-HEAD}") | tar -x -C "$work/tmpl" || exit 1
for t in "$@"; do
    d=$out/$label/$t; w=$work/$label/$t
    rm -rf "$d" "$w"; mkdir -p "$d" "$w/home" "$w/tmp" "$w/zig"
    cp -a "$work/tmpl" "$w/ws"
    ( cd "$w/ws" && git init -q && git -c user.name=measure -c user.email=measure@localhost add -A \
      && git -c user.name=measure -c user.email=measure@localhost commit -q -m "measured tree" ) \
      || { echo "setup failed $t"; continue; }
    cp "$cfg" "$w/home/.jichi"
    { date '+%F %T'; uptime; } > "$d/before.txt"
    s=$(date +%s.%N)
    ( cd "$w/ws" && HOME="$w/home" TMPDIR="$w/tmp" ZIG_GLOBAL_CACHE_DIR="$w/zig" \
        GIT_AUTHOR_NAME=measure GIT_AUTHOR_EMAIL=measure@localhost \
        GIT_COMMITTER_NAME=measure GIT_COMMITTER_EMAIL=measure@localhost \
        "$J" --log "$d/telemetry.jsonl" \
        attempt "docs/assignments/$t.md" --output json --keep-worktree \
        < /dev/null > "$d/result.json" 2> "$d/stderr.txt" )
    rc=$?
    e=$(date +%s.%N)
    { date '+%F %T'; uptime; } > "$d/after.txt"
    echo "$rc" > "$d/rc"
    awk -v s="$s" -v e="$e" 'BEGIN{printf "%.1f\n", e-s}' > "$d/outer_wall_s"
    wt=$(ls -d "$w/home/.jichi.d/worktrees"/att-*/wt-0 2>/dev/null | head -n 1)
    if [ -n "$wt" ]; then
        ( cd "$wt" && git status --porcelain > "$d/changed.txt" 2>&1; git diff > "$d/diff.patch" 2>&1
          git ls-files --others --exclude-standard > "$d/untracked.txt" 2>&1 )
    fi
    cp -a "$w/home/.jichi.d/runs" "$d/runs" 2>/dev/null
    echo "$label $t rc=$rc $(cat "$d/outer_wall_s")s"
done
