#!/bin/sh
# tests/bench/mentor_ab/run-arms.sh -- the mentor under two jichi binaries, one
# draft per arm per workspace, for blind grading (M605; first run 2026-08-27,
# docs/analysis/2026-08-27-the-language-of-lessons.md section 13).
#
# THE QUESTION. M596 made a command's `agent:` persona reach its subtask; until
# then the scaffolded /learn mentor ran under the generic sub-agent prompt and
# never saw its own FORMAT IS STRICT block. Does a mentor that receives its
# instructions draft something a human would rather `learn apply`? That is a
# grader's question, so this harness produces the drafts and blind.sh seals the
# condition; the grader is a person who did not write the mentor (the same rule
# the craft A/B states: the author of a thing is the last whose unblinded
# judgement of it is worth having).
#
# A MEASUREMENT, NOT A GATE: needs a live model, never runs in `make ci`. Free
# models only (CLAUDE.md): the config you pass must name a jlu/* or local model.
# NO caps but connect -- a cap that fires manufactures an answer (ANECDOTES #64).
#
# Usage:
#   sh tests/bench/mentor_ab/run-arms.sh --old OLD_BIN --new NEW_BIN \
#       --config CONFIG_JSON --key-file FILE --label LABEL [--pair N] \
#       WORKSPACE:TELEMETRY_LOG [WORKSPACE:TELEMETRY_LOG ...]
#
#   OLD_BIN / NEW_BIN   two pinned jichi binaries (scripts/pin-driver.sh), so a
#                       rebuild of the tree cannot change an arm mid-run
#   CONFIG_JSON         a jichi config; its `apiKeyEnv` names the variable the key
#                       is exported under (both JICHI_API_KEY and JLU_API_KEY are set)
#   FILE                the key, one line; read into the environment, never printed
#   LABEL               results land in tests/bench/mentor_ab/results/LABEL/
#   --pair N            suffix for a second/third pair of the same workspaces
#   WORKSPACE:LOG       a project directory and the telemetry log (a path) to
#                       give both arms as that project's history
#
# WHAT IT ISOLATES, AND HOW. Each arm runs from a scratch HOME (config copy + the
# one telemetry log), so nothing touches the operator's ~/.jichi.d. The
# workspace's own draft is moved OUT of the workspace for the arm and restored
# after -- kept beside .jichi/ it was found and read by the mentor within three
# tool calls (measured on the first attempt). A workspace without mentor assets
# gets the scaffolded learn.md + mentor.md for the duration of both arms and has
# them removed afterwards; the same text for both arms. learn.md's embedded
# `learn analyze` runs $JICHI_BIN, pinned to the arm's own binary, so the
# operator's installed jichi is not a third arm. Every change under .jichi/ other
# than the draft is reported as `other_changes=N`, not trusted to be zero.
set -u
OLD=""; NEW=""; CFG=""; KEYFILE=""; LABEL=""; PAIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --old) OLD=$2; shift 2 ;;
        --new) NEW=$2; shift 2 ;;
        --config) CFG=$2; shift 2 ;;
        --key-file) KEYFILE=$2; shift 2 ;;
        --label) LABEL=$2; shift 2 ;;
        --pair) PAIR=$2; shift 2 ;;
        --*) echo "unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
if [ -z "$OLD" ] || [ -z "$NEW" ] || [ -z "$CFG" ] || [ -z "$KEYFILE" ] || \
   [ -z "$LABEL" ] || [ $# -eq 0 ]; then
    echo "usage: $0 --old BIN --new BIN --config JSON --key-file FILE --label L [--pair N] WS:LOG..." >&2
    exit 2
fi
for b in "$OLD" "$NEW"; do
    [ -x "$b" ] || { echo "not executable: $b" >&2; exit 2; }
done
[ -f "$CFG" ] || { echo "no config: $CFG" >&2; exit 2; }
[ -f "$KEYFILE" ] || { echo "no key file: $KEYFILE" >&2; exit 2; }

HERE=$(cd "$(dirname "$0")" && pwd)
R="$HERE/results/$LABEL"
OUT="$R/drafts"; mkdir -p "$OUT" "$R/journals" "$R/homes"

KEY=$(tr -d '\n' < "$KEYFILE")
export JICHI_API_KEY="$KEY" JLU_API_KEY="$KEY"

# A workspace without the mentor assets gets the scaffolded pair for BOTH arms.
ensure_mentor() { # ws -> "own" | "scaffolded"
    ws=$1
    if [ -f "$ws/.jichi/commands/learn.md" ] && [ -f "$ws/.jichi/agents/mentor.md" ]; then
        echo own; return
    fi
    tmpws=$(mktemp -d "$R/init.XXXXXX")
    (cd "$tmpws" && HOME="$tmpws" "$NEW" init < /dev/null > /dev/null 2>&1)
    mkdir -p "$ws/.jichi/commands" "$ws/.jichi/agents"
    [ -f "$ws/.jichi/commands/learn.md" ] || { cp "$tmpws/.jichi/commands/learn.md" "$ws/.jichi/commands/learn.md"; echo "$ws/.jichi/commands/learn.md" >> "$R/scaffolded-files.txt"; }
    [ -f "$ws/.jichi/agents/mentor.md" ] || { cp "$tmpws/.jichi/agents/mentor.md" "$ws/.jichi/agents/mentor.md"; echo "$ws/.jichi/agents/mentor.md" >> "$R/scaffolded-files.txt"; }
    rm -rf "$tmpws"
    echo scaffolded
}

run_arm() { # ws arm bin log
    ws=$1; arm=$2; bin=$3; tlog=$4
    base=$(basename "$ws")${PAIR:+$PAIR}
    home="$R/homes/$arm-$base"; rm -rf "$home"; mkdir -p "$home/.jichi.d/telemetry"
    cp "$CFG" "$home/.jichi"
    cp "$tlog" "$home/.jichi.d/telemetry/$(basename "$tlog")"
    draft="$ws/.jichi/lessons.draft.md"
    [ -f "$draft" ] && mv "$draft" "$home/draft.ab-aside"
    [ -f "$draft.answer" ] && mv "$draft.answer" "$home/draft.answer.ab-aside"
    (cd "$ws" && find .jichi -type f | sort | xargs md5sum) > "$home/before.md5" 2>/dev/null
    start=$(date +%s)
    (cd "$ws" && HOME="$home" JICHI_BIN="$bin" "$bin" --config "$home/.jichi" --no-session \
        --log-level off -p "/learn" < /dev/null > "$home/stdout.txt" 2> "$home/stderr.txt")
    rc=$?
    end=$(date +%s)
    (cd "$ws" && find .jichi -type f | sort | xargs md5sum) > "$home/after.md5" 2>/dev/null
    if [ -f "$draft" ]; then
        cp "$draft" "$OUT/$base-$arm.md"; rm -f "$draft"
    else
        printf '(no draft written; stdout follows)\n\n' > "$OUT/$base-$arm.md"
        cat "$home/stdout.txt" >> "$OUT/$base-$arm.md"
    fi
    [ -f "$draft.answer" ] && mv "$draft.answer" "$OUT/$base-$arm.answer.md"
    [ -f "$home/draft.ab-aside" ] && mv "$home/draft.ab-aside" "$draft"
    [ -f "$home/draft.answer.ab-aside" ] && mv "$home/draft.answer.ab-aside" "$draft.answer"
    cp "$home/stderr.txt" "$R/journals/$arm-$base.stderr.txt"
    cp "$home/stdout.txt" "$R/journals/$arm-$base.stdout.txt"
    diff "$home/before.md5" "$home/after.md5" | grep -v 'lessons.draft.md' > "$home/jichi-diff.txt"
    printf '%s %s rc=%s wall=%ss tool_calls=%s draft_bytes=%s other_changes=%s\n' "$base" "$arm" "$rc" \
        $((end - start)) "$(grep -c '^\[tool\]' "$home/stderr.txt")" \
        "$(wc -c < "$OUT/$base-$arm.md")" "$(grep -c '^[<>]' "$home/jichi-diff.txt")" \
        | tee -a "$R/arms.log"
}

: > "$R/scaffolded-files.txt"
for pair in "$@"; do
    ws=${pair%%:*}; tlog=${pair#*:}
    [ -d "$ws" ] || { echo "no workspace: $ws" >&2; continue; }
    [ -f "$tlog" ] || { echo "no telemetry log: $tlog" >&2; continue; }
    src=$(ensure_mentor "$ws")
    echo "$(basename "$ws")${PAIR:+$PAIR} mentor assets: $src" | tee -a "$R/arms.log"
    run_arm "$ws" old "$OLD" "$tlog"
    run_arm "$ws" new "$NEW" "$tlog"
done
while read -r f; do [ -n "$f" ] && rm -f "$f"; done < "$R/scaffolded-files.txt"
echo "arms done: $R/drafts (scaffolded files removed: $(grep -c . "$R/scaffolded-files.txt"))"
echo "next: sh $HERE/blind.sh --label $LABEL"
