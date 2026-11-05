#!/bin/sh
# smoke: the rules walk stops at a linked worktree's root, as it does at a
# checkout's (M884).
#
# WHY THIS EXISTS. jc_rules_load reads AGENTS.md (or else CLAUDE.md) in every
# directory from the repository root down to the working directory, and took the
# root to be the nearest directory holding a `.git` DIRECTORY. A linked
# worktree's `.git` is a FILE ("gitdir: ..."), and so is a submodule's, so from a
# worktree the walk climbed past the repository and read every rules file above
# it, ahead of the project's own. Found on 2026-10-03 in this project's own
# worktree, where `jichi sysmsg` delivered a template AGENTS.md from two
# directories up before CLAUDE.md. docs/RULES.md had always said "the nearest
# ancestor containing `.git`".
#
# The worktree is made by `git worktree add`, not by hand: the fixture is the
# thing users have (docs/TESTING_RUNBOOK.md §3c).
#
# CHECKED      -- `jichi rules`: (1) in a checkout, its own rules are read and the
#                 directory above it is not; (2) in a linked worktree of it, the
#                 worktree's own rules are read -- the positive a hollow binary
#                 cannot print; (3) in that worktree's subdirectory, the directory
#                 above the worktree is not read; (4) in a plain directory beside
#                 them, with no repository, the directory above IS read -- so the
#                 absences in 1 and 3 are the walk stopping, not a reader that
#                 cannot see a parent.
# NOT CHECKED  -- a submodule, whose `.git` is a file too and stops the walk by
#                 the same test (tests/test_rules.c builds that shape by hand);
#                 the order of the files read.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- the worktree is a real one"

t_plan 4
smoke_home
top=$(smoke_tmp)
# Markers built by arithmetic, so a match can only come from a file jichi read.
above="ABOVE-$((40 + 2))"
own="OWN-$((50 + 7))"
plain="PLAIN-$((60 + 1))"
printf 'Rules above the repository: %s\n' "$above" > "$top/AGENTS.md"
mkdir -p "$top/repo" "$top/plain"
printf 'Rules of the project: %s\n' "$own" > "$top/repo/AGENTS.md"
printf 'Rules of a plain directory: %s\n' "$plain" > "$top/plain/AGENTS.md"
(cd "$top/repo" && git init -q . && git add AGENTS.md \
    && git -c user.name=smoke -c user.email=smoke@example.com \
           commit -q -m init) > "$top/git.log" 2>&1
wt_ok=0
if (cd "$top/repo" && git worktree add ../wt) >> "$top/git.log" 2>&1 \
   && [ -f "$top/wt/.git" ]; then
    wt_ok=1
    mkdir -p "$top/wt/sub"
fi

rules_in() { (cd "$1" && "$BIN" rules < /dev/null 2>&1); }

# --- 1: a checkout reads its own rules, not the directory above -----------------
out=$(rules_in "$top/repo")
case "$out" in
    *"$above"*) t_fail "a checkout read the rules file above its root: $out" ;;
    *"$own"*)   t_ok "a checkout reads its own rules and stops at its root" ;;
    *)          t_fail "a checkout's own rules were not read: $out" ;;
esac

# --- 2 and 3: a linked worktree, whose .git is a file ----------------------------
if [ "$wt_ok" -eq 1 ]; then
    out=$(rules_in "$top/wt")
    case "$out" in
        *"$own"*) t_ok "a linked worktree reads its own rules" ;;
        *)        t_fail "a linked worktree's own rules were not read: $out" ;;
    esac
    out=$(rules_in "$top/wt/sub")
    case "$out" in
        *"$above"*) t_fail "from a worktree, the walk read the rules file above it: $out" ;;
        *"$own"*)   t_ok "from a worktree's subdirectory, the walk stops at the worktree" ;;
        *)          t_fail "a worktree's subdirectory read no rules: $out" ;;
    esac
else
    t_skip_one "this git made no linked worktree ($(tail -n 1 "$top/git.log"))"
    t_skip_one "no linked worktree to walk from"
fi

# --- 4: the control -- with no repository, the walk does read the parent --------
out=$(rules_in "$top/plain")
case "$out" in
    *"$plain"*"$above"*|*"$above"*"$plain"*)
        t_ok "with no repository, the walk reads the directory above (the control)" ;;
    *)  t_fail "with no repository, the directory above was not read: $out" ;;
esac

t_done
