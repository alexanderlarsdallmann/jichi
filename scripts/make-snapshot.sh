#!/bin/sh
# make-snapshot.sh -- build the publishable tree for the public release.
#
# WHAT THIS IS. docs/plans/2026-08-public-snapshot.md designed the public
# repository at M391 and its section 6 said, in as many words, "do not wait for
# the licence to prepare everything else -- the result is a checklist with known
# timings instead of a scramble". Nobody ran it. This is that rehearsal, made
# repeatable: one command that produces exactly the tree that would be published,
# so `make check-target` can be run INSIDE it and tests/smoke/snapshot_lint.sh can
# assert against the real artifact rather than a proxy for it.
#
# THE SELECTION RULE, AND THE LIST THIS DELIBERATELY DOES NOT HAVE.
# The tree is `git archive HEAD` and nothing else: the git INDEX is the manifest.
# The plan's section 4 instead described a table of paths to delete after copying,
# and the argument against that is the plan's own history -- by the time it was
# read again, section 2.4 was wrong about which .jichi/ assets were tracked and
# section 4 was wrong that no generated artifact ships (three compiled binaries
# did). A parallel list of what-does-not-ship is a second source of truth about
# the tree, and second sources of truth here go stale silently.
#
# So the rule is: IF IT MUST NOT SHIP, IT MUST NOT BE TRACKED. `git rm --cached`
# it, which has the further virtue of making `git status` tell the truth to
# everyone, not just to this script. What that buys, structurally rather than by
# vigilance: `local/` (which holds a live API key), the built binaries, and the
# root-owned .v6-console-results/ CANNOT travel, because they were never in the
# index. That is also why this uses `git archive` and never `cp -a`/`rsync` of the
# working tree -- a copy would carry all three.
#
# Usage:
#   scripts/make-snapshot.sh                    # -> $HOME/.cache/jichi-snapshot/tree
#   scripts/make-snapshot.sh --dest DIR
#   scripts/make-snapshot.sh --rev REV          # snapshot something other than HEAD
#   scripts/make-snapshot.sh --dirty            # snapshot the working tree
#   scripts/make-snapshot.sh --commit           # also git init + one commit
#
# --commit is the only step that creates a repository, and it is deliberately not
# the default: producing the tree is a rehearsal anyone may run, and the first
# commit is a release act that needs the LICENCE file to exist first.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST="${JICHI_SNAPSHOT_DIR:-$HOME/.cache/jichi-snapshot}/tree"
REV=HEAD
ALLOW_DIRTY=0
DO_COMMIT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --dest)   DEST="$2"; shift ;;
        --rev)    REV="$2"; shift ;;
        --dirty)  ALLOW_DIRTY=1 ;;
        --commit) DO_COMMIT=1 ;;
        --help|-h) sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "make-snapshot: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done

if ! git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    echo "make-snapshot: $ROOT is not a git repository" >&2
    echo "  the index is this script's manifest, so there is nothing to select from" >&2
    exit 2
fi

# The destination must not live inside the repository. pin-driver.sh refuses the
# same way and for a related reason: a tree produced inside the tree it copies is
# reachable by the next `git add -A`, and a snapshot that can be committed back
# into the development repo is a mistake waiting for a hurried afternoon.
DEST_PARENT=$(cd "$(dirname "$DEST")" 2>/dev/null && pwd) || DEST_PARENT=""
if [ -n "$DEST_PARENT" ]; then
    case "$DEST_PARENT/" in
        "$ROOT"/*|"$ROOT"/) echo "make-snapshot: refusing a destination inside the repository ($DEST)" >&2
               echo "  the snapshot would become committable back into the development tree" >&2
               exit 2 ;;
    esac
fi

if [ -n "$(git -C "$ROOT" status --porcelain 2>/dev/null)" ]; then
    if [ "$ALLOW_DIRTY" -eq 0 ]; then
        echo "make-snapshot: the working tree is dirty" >&2
        echo "  a snapshot names the revision it came from, and a dirty tree makes that a lie." >&2
        echo "  Commit first, or pass --dirty to rehearse against uncommitted work." >&2
        exit 2
    fi
    # --dirty means snapshot the tree AS IT READS NOW, not merely tolerate that it
    # differs from HEAD. The first draft did the latter, and the lint that consumes
    # this then measured HEAD while reporting on the working tree -- five findings
    # that had just been fixed came back identical, which is the same wrong-subject
    # mistake as running a command in the wrong directory.
    #
    # The obvious `git stash create` is WRONG here and was tried: its worktree
    # commit is built against HEAD, so a file removed with `git rm --cached` but
    # still present on disk -- precisely the untrack-a-build-artifact move this
    # release work is made of -- comes back. Measured: all three untracked binaries
    # reappeared in the snapshot.
    #
    # So build a tree from the INDEX, which is the manifest, with worktree
    # modifications to tracked files folded in via `add -u` against a scratch index
    # copy. Nothing touches the real index, and `git archive` stays the single
    # extraction path.
    _idx=$(git -C "$ROOT" rev-parse --git-path index)
    _tmpidx="${TMPDIR:-/tmp}/jichi-snap-idx.$$"
    cp "$_idx" "$_tmpidx" || exit 1
    GIT_INDEX_FILE="$_tmpidx" git -C "$ROOT" add -u 2>/dev/null
    _tree=$(GIT_INDEX_FILE="$_tmpidx" git -C "$ROOT" write-tree 2>/dev/null)
    rm -f "$_tmpidx"
    if [ -z "$_tree" ]; then
        echo "make-snapshot: could not write a tree for the dirty worktree" >&2
        exit 1
    fi
    REV="$_tree"
fi

SHA=$(git -C "$ROOT" rev-parse --short "$REV" 2>/dev/null) || {
    echo "make-snapshot: no such revision '$REV'" >&2; exit 2; }

if [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
    echo "make-snapshot: $DEST exists and is not empty -- remove it first" >&2
    exit 2
fi

mkdir -p "$DEST" || exit 1
git -C "$ROOT" archive --format=tar "$REV" | (cd "$DEST" && tar -xf -) || {
    echo "make-snapshot: git archive failed" >&2; exit 1; }

NFILES=$(find "$DEST" -type f | wc -l | tr -d ' ')
if [ "$NFILES" -lt 100 ]; then
    echo "make-snapshot: only $NFILES files extracted -- refusing to call that a snapshot" >&2
    exit 1
fi

echo "make-snapshot: $NFILES files from $REV ($SHA) -> $DEST"

if [ "$DO_COMMIT" -eq 1 ]; then
    if [ ! -f "$DEST/LICENSE" ]; then
        echo "make-snapshot: refusing --commit with no LICENSE in the tree" >&2
        echo "  the first public commit's contents are what people acquire rights to" >&2
        exit 2
    fi
    # M619: the first PUBLIC commit's author is a deliberate input, not whatever
    # identity the machine happens to have. Read it from the development
    # repository's own config (local, then global); refuse when absent. Found
    # the day the LICENSE landed: snapshot_lint check 2 flipped to its decided
    # state and the embedded `git commit` failed under the smoke tier's scratch
    # HOME with "unable to auto-detect email address" -- an ambient dependency
    # that had been unreachable while the refusal path always fired first.
    # M621: fall back to GIT_AUTHOR_NAME/GIT_AUTHOR_EMAIL before refusing. The
    # gate runs this script on machines that have no git identity at all -- the
    # FIRST HOSTED CI RUN failed exactly here (2026-08-27, GitHub Actions run
    # 33093305185: snapshot_lint check 2 "failed for the wrong reason"), because
    # the M619 refusal had been validated only on machines that already had a
    # configured identity. The lint's rehearsal commit is throwaway, so it may
    # supply a labelled rehearsal identity via the environment; a real release
    # still reads the repository config first, and the refusal below still
    # fires when neither source names an author -- the fallback is two explicit
    # variables, never git's ambient user@host auto-detection.
    SNAME=$(git -C "$ROOT" config user.name 2>/dev/null)
    SMAIL=$(git -C "$ROOT" config user.email 2>/dev/null)
    [ -n "$SNAME" ] || SNAME=${GIT_AUTHOR_NAME:-}
    [ -n "$SMAIL" ] || SMAIL=${GIT_AUTHOR_EMAIL:-}
    if [ -z "$SNAME" ] || [ -z "$SMAIL" ]; then
        echo "make-snapshot: refusing --commit with no author identity" >&2
        echo "  the first public commit is attributed on purpose: set user.name and" >&2
        echo "  user.email in the development repository (git config user.name ...)" >&2
        exit 2
    fi
    ( cd "$DEST" && git init -q && git add -A && \
      GIT_AUTHOR_NAME="$SNAME" GIT_AUTHOR_EMAIL="$SMAIL" \
      GIT_COMMITTER_NAME="$SNAME" GIT_COMMITTER_EMAIL="$SMAIL" \
      git commit -q -F - <<EOF
jichi $(sed -n 's/.*JC_VERSION "\([^"]*\)".*/\1/p' "$DEST/include/jc_version.h" 2>/dev/null)

A complete AI coding agent in C89 for Linux/POSIX.

This repository begins at a curated first commit. The development history
exists -- it is a private repository of many hundreds of milestones -- and was
deliberately not published: a development history contains intermediate states
nobody chose to publish, and a fresh first commit makes the published surface
exactly the tree you can inspect.

The narrative is not lost, it simply lives in documents rather than in commits.
See docs/ROADMAP.md for the reasoning per milestone, CHANGELOG.md for what
changed per version, docs/DECISIONS.md for what was chosen and what was
rejected, docs/DEFERRED.md for what was consciously left, and
docs/ANECDOTES.md for the failures that taught the most. Those ship in full,
on purpose.
EOF
    ) || { echo "make-snapshot: git init/commit failed" >&2; exit 1; }
    echo "make-snapshot: committed in $DEST"
fi
