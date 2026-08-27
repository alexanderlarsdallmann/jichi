#!/bin/sh
# jichi-dev.sh -- launch a self-hosting session with the right config and fences.
#
#   sh examples/self-hosting/jichi-dev.sh [MODE] [ARGS...]
#
# Run it from a jichi checkout. MODE is a word, not a flag -- deliberately, so
# nothing here can be mistaken for an option jichi itself takes (the tier checks
# that every option this project documents in a `--`-prefixed form is a real
# one: tests/smoke/docs_flags.sh).
#
#   review      DEFAULT. The review-only slice against a LOCAL model server
#               (config.jichi-dev-local.json): no key, nothing can be written,
#               safe against any model.
#   gateway     the same slice on the institutional gateway (needs JICHI_API_KEY)
#   write       the WRITE-enabled slice: fenced to tests/, docs/, CHANGELOG.md,
#               with verify + rollback. REFUSES to run on master/main, and adds
#               the README's budgets unless you pass your own.
#   clean       remove the pack's assets from .jichi/ and exit
#   ARGS...     everything else is passed to jichi (e.g. -p "/review-diff")
#
# WHY A LAUNCHER EXISTS. The pack's README asks for four things a reader can
# forget one at a time: the assets copied into .jichi/, the right config, a
# branch that is not master, and budgets on an unattended run. Three of those are
# checkable, so they are checked here rather than remembered. `local/` is
# git-ignored in this repository, so this lives in examples/ and can be copied
# there (`cp examples/self-hosting/jichi-dev.sh local/`) if you prefer it out of
# the way; it resolves the checkout from its own path either way.
#
# Strict POSIX sh: this is a script other people run, on machines this project
# does not choose (the tier holds it to that -- tests/smoke/posix_utils_lint.sh).
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root"

cfg="$here/config.jichi-dev-local.json"
mode=review
clean=0
case "${1:-}" in
    review)  shift ;;
    gateway) cfg="$here/config.jichi-dev.json";       mode=gateway; shift ;;
    write)   cfg="$here/config.jichi-dev-write.json"; mode=write;   shift ;;
    clean)   clean=1; shift ;;
esac
[ "${1:-}" = "--" ] && shift

# --- clean mode: take the copies out, leave what the repository ships -------
if [ "$clean" -eq 1 ]; then
    for f in "$here"/agents/*.md; do
        [ -f "$f" ] && rm -f ".jichi/agents/$(basename "$f")"
    done
    for f in "$here"/commands/*.md; do
        [ -f "$f" ] && rm -f ".jichi/commands/$(basename "$f")"
    done
    echo "jichi-dev: pack assets removed from .jichi/"
    exit 0
fi

if [ ! -x "$root/jichi" ]; then
    echo "jichi-dev: no ./jichi in $root -- run 'make' first" >&2
    exit 2
fi

# --- the fences the README asks for, enforced --------------------------------
if [ "$mode" = gateway ]; then
    if [ -z "${JICHI_API_KEY:-}" ]; then
        echo "jichi-dev: the gateway mode needs JICHI_API_KEY in the environment" >&2
        echo "           (never in the config; see the pack README)" >&2
        exit 2
    fi
fi

set -- ${1+"$@"}
if [ "$mode" = write ]; then
    branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")
    case "$branch" in
        master|main)
            echo "jichi-dev: refusing the write mode on '$branch'." >&2
            echo "           The write slice edits tests/, docs/ and CHANGELOG.md." >&2
            echo "           Run it on a feature branch: git switch -c dev-loop" >&2
            exit 2 ;;
        "") echo "jichi-dev: not a git checkout -- the write mode wants a branch to hold the run" >&2
            exit 2 ;;
    esac
    # The README's budgets, added only if you passed none of your own. An
    # unattended loop with no bound is not a bounded run (docs/AUTONOMY.md).
    case " $* " in
        *" --budget-tokens "*|*" --deadline "*|*" --max-tool-calls "*) ;;
        *) set -- --budget-tokens 200k --deadline 20m --max-tool-calls 60 ${1+"$@"}
           echo "jichi-dev: adding the README's budgets (200k tokens, 20m, 60 tool calls)" ;;
    esac
    echo "jichi-dev: WRITE slice on branch '$branch' -- fenced to tests/, docs/, CHANGELOG.md"
fi

# --- the assets, idempotently ------------------------------------------------
# .jichi/ is git-ignored wholesale, so these copies never reach git; the two
# documentation reviewers the repository tracks there are left alone, and
# `jichi agents` will list them alongside the pack's five.
mkdir -p .jichi/agents .jichi/commands
cp "$here"/agents/*.md   .jichi/agents/
cp "$here"/commands/*.md .jichi/commands/

echo "jichi-dev: $mode slice, config $(basename "$cfg")"
exec "$root/jichi" --config "$cfg" ${1+"$@"}
