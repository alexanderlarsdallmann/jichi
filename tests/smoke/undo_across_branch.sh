#!/bin/sh
# smoke: an undo restores CONTENT, and does not know which branch you are on
# (M514). Needs git; skips without it.
#
# WHAT THIS PINS, AND WHY IT IS NOT A BUG REPORT. A checkpoint is a commit in a
# shadow repo at ~/.jichi.d/checkpoints/<key>/, where <key> is derived from the
# WORKSPACE PATH -- not from the branch -- and whose work tree is your workspace
# (docs/SNAPSHOTS.md). So `undo` makes the worktree match a recorded state, and
# the branch you happen to be standing on is not part of that decision. That is
# the design: your real .git, its history and its branches are never touched.
#
# The consequence is sharp enough to deserve a test, and it was measured before
# it was written down (docs/SNAPSHOTS.md "Checkpoints do not know about your
# branches"): take a checkpoint on a feature branch that carries a file master
# does not have, switch to master, undo -- and the file ARRIVES on master, as an
# untracked file. The self-hosting workflow prescribes exactly this shape ("run
# it on a branch, never master", examples/self-hosting/README.md), so the sharp
# edge sits on the recommended path.
#
# The tool does say so first, and this driver pins that too: `undo --dry-run`
# names the file before anything happens, and the discarded state is preserved
# with a `recover` handle. A sharp edge that announces itself is a documented
# tool; one that does not is a trap.
. "$(dirname "$0")/_smoke.sh"

# The skip test precedes the plan (M672): `t_plan` then `t_skip` emits TWO TAP
# plan lines and a runner reads that as malformed output, failing a driver that
# exited 0. progress_write_fails was the last red driver on the OpenBSD row for
# exactly that, and it was not a defect -- it was a skip the harness could not
# parse.
command -v git >/dev/null 2>&1 || t_skip "git not on PATH (snapshots are git-backed)"
t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws="$tmp/ws"
mkdir -p "$ws"

# --- a repo with a feature branch that carries a file of its own -------------
# `git checkout`, not `git switch`: switch arrived in git 2.23, and on Debian 9's
# git 2.11 the branch was never made -- check 2 then passed with nothing to test
# and checks 3 and 5 failed for a fixture's reason (V2f, 2026-09-24). The base
# branch is whatever `git init` named it, read back rather than assumed.
(cd "$ws" && git init -q . && git config user.email t@example.com && git config user.name t &&
 printf 'buy milk\nfeed the cat\n' > notes.txt && git add -A && git commit -qm base &&
 git checkout -q -b feat && printf 'only on the feature branch\n' > feat-only.txt &&
 git add -A && git commit -qm feat-file) >/dev/null 2>&1
basebr=$(cd "$ws" && git for-each-ref --format='%(refname:short)' refs/heads/ | grep -v '^feat$' | head -1)

cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool read_file {"path":"notes.txt"}
rule
  count 2
  tool edit_file {"path":"notes.txt","old_string":"buy milk","new_string":"buy OAT milk"}
rule
  count 3
  text edited.
rule
  status 500
  body {"error":"unexpected request"}
MM
mm_start "$tmp/replies.mm" "$tmp/cap" 3
# Written inline rather than through write_config: that helper pins
# "snapshots":false, and adding a second "snapshots" key produced a config where
# the false won and every check below reported "snapshots unavailable" -- a
# driver measuring a feature it had switched off. lowResource is pinned here for
# the same reason it is pinned there (smoke_lint check 8: auto-lite must not
# reshape a fixture on a small machine).
cat > "$tmp/config.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
CFG

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto --no-session \
    -q -p "change buy milk to buy OAT milk in notes.txt" < /dev/null) > "$tmp/run.out" 2>&1
mm_stop

# --- 1: the run edited the file, so a pre-edit checkpoint exists -------------
# The floor: with no edit there is no checkpoint, and every check below would be
# vacuous. (The read_file round is in the fixture for exactly this reason -- the
# read-before-edit guard refuses an edit otherwise, and the mock cheerfully
# reports success anyway.)
if grep -q 'OAT' "$ws/notes.txt" 2>/dev/null; then
    t_ok "the run edited notes.txt on the feature branch"
else
    t_fail "no edit landed, so no checkpoint exists: $(head_bytes 200 < "$tmp/run.out")"
fi

(cd "$ws" && git add -A && git commit -qm "the loop's work") >/dev/null 2>&1
(cd "$ws" && git checkout -q "$basebr") >/dev/null 2>&1

# The floor as well as the absence: the file must exist on feat, or its absence
# here proves nothing.
if [ ! -f "$ws/feat-only.txt" ] && [ -n "$basebr" ] &&
   (cd "$ws" && git cat-file -e feat:feat-only.txt) 2>/dev/null; then
    t_ok "on $basebr, the branch-only file is absent (as git intends)"
else
    t_fail "the fixture's branches are wrong: base='$basebr', feat-only.txt on feat: $(cd "$ws" && git cat-file -e feat:feat-only.txt 2>&1 && echo yes)"
fi

# --- 3: the dry run NAMES the file it would bring across ---------------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" undo --dry-run) \
    > "$tmp/dry.out" 2>&1
if grep -q 'feat-only.txt' "$tmp/dry.out"; then
    t_ok "undo --dry-run names the branch-only file before anything happens"
else
    t_fail "the dry run does not mention feat-only.txt -- the warning is gone: $(head_bytes 200 < "$tmp/dry.out")"
fi
if [ ! -f "$ws/feat-only.txt" ]; then
    t_ok "and --dry-run changed nothing"
else
    t_fail "--dry-run wrote to the workspace"
fi

# --- 5: the real undo restores content regardless of the branch -------------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" undo) > "$tmp/undo.out" 2>&1
if [ -f "$ws/feat-only.txt" ] && grep -q 'recover' "$tmp/undo.out"; then
    t_ok "undo restored the checkpoint's content onto $basebr, and printed a recover handle"
else
    t_fail "undo did not behave as documented (file=$([ -f "$ws/feat-only.txt" ] && echo present || echo absent)): $(head_bytes 200 < "$tmp/undo.out")"
fi

t_done
