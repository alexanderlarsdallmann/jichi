#!/bin/sh
# smoke: `undo` reports the size of what it destroyed (M537).
#
# THE DEFECT, and how it was found. `undo --dry-run` has printed a full preview
# since M337 -- every tracked file a reset would revert, every untracked file a
# clean would remove. The DESTRUCTIVE path printed the checkpoint's label and
# nothing else. So a revert of 768 files and a revert of none produced the same
# line on screen, and the only way to learn which had happened was to think of
# running `git status` yourself.
#
# That has the safety backwards: the reversible path was the informed one and the
# irreversible path was the silent one. M337b already made this argument one step
# further along -- "a save the user is not told about is a store nobody knows to
# read" -- and the same sentence holds earlier: a destruction whose SIZE you are
# not told is damage nobody knows to look for.
#
# It was found by an agent (me) running `undo` in this repository as an
# EXISTENCE PROBE while checking the man page's claims -- no --dry-run, no glance
# at the source that would have answered the same question for free. The tree
# went back 768 files and the terminal said "reverted to checkpoint 1: <label>".
# Nothing was lost, because the milestone had been pushed first, and the recovery
# was one `git restore`. The lesson that survives is not "be careful": it is that
# a destructive command must state its magnitude, so that carelessness is CHEAP
# to notice. docs/ANECDOTES.md #66 has the whole account.
#
# WHY THE MEASUREMENT HAPPENS BEFORE THE RESTORE. Afterwards there is nothing
# left to measure -- the tree already matches the checkpoint, so every `git diff`
# is empty. That is the whole reason this could not be a return value of the
# restore call, and check 3 is what proves the ordering is right rather than
# accidentally reporting zeros.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws="$tmp/ws"
mkdir -p "$ws"

# A repo whose tree will differ from the checkpoint in a COUNTABLE way: three
# tracked files the agent will edit, and one untracked file a clean would remove.
# The counts are what the checks assert, so they must be unambiguous.
(cd "$ws" && git init -q . && git config user.email t@example.com &&
 git config user.name t &&
 printf 'alpha\n' > one.txt && printf 'beta\n' > two.txt &&
 printf 'gamma\n' > three.txt &&
 git add -A && git commit -qm base) >/dev/null 2>&1

cat > "$tmp/replies.mm" <<'MM'
wire openai
rule
  count 1
  tool read_file {"path":"one.txt"}
rule
  count 2
  tool edit_file {"path":"one.txt","old_string":"alpha","new_string":"ALPHA"}
rule
  count 3
  tool read_file {"path":"two.txt"}
rule
  count 4
  tool edit_file {"path":"two.txt","old_string":"beta","new_string":"BETA"}
rule
  count 5
  tool write_file {"path":"four.txt","content":"a file that did not exist\n"}
rule
  count 6
  text edited.
rule
  status 500
  body {"error":"unexpected request"}
MM
# The read_file before each edit_file is REQUIRED, not decoration: without it
# both edits came back as tool errors and the fixture produced a one-file diff
# where the checks want two. undo_across_branch.sh's fixture reads first for the
# same reason; the first cut of this one did not, and check 1 caught it.
mm_start "$tmp/replies.mm" "$tmp/cap" 9
# Inline, not write_config: that helper pins "snapshots":false and a second key
# would lose to the first, leaving this driver measuring a feature it had turned
# off -- the mistake undo_across_branch.sh records in its own header.
cat > "$tmp/config.json" <<CFG
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
CFG

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --auto \
    --no-session -q -p "edit one.txt and two.txt, then create four.txt" \
    < /dev/null) > "$tmp/run.out" 2>&1
mm_stop

# ---- 1: the run changed the tree, so there is a blast radius to report -----
# The floor. With no edits there is no checkpoint and nothing below means
# anything -- and a report of "0 files" would pass a naive check vacuously.
if grep -q ALPHA "$ws/one.txt" 2>/dev/null &&
   grep -q BETA "$ws/two.txt" 2>/dev/null; then
    t_ok "the run edited two tracked files (there is something to revert)"
else
    t_fail "no edit landed, so no checkpoint exists: \
$(head_bytes 200 < "$tmp/run.out")"
fi

# ---- 2: the report names the magnitude, not just the checkpoint ------------
# The defect itself. `undo` used to print one line naming the label; it must now
# also state how much it changed, in a form a human reads at a glance.
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" undo \
      < /dev/null 2>&1)
case "$out" in
    *"file"*"changed"*)
        t_ok "undo reports the magnitude: $(printf '%s' "$out" | grep changed \
             | head -1 | sed 's/^ *//' | head_bytes 90)" ;;
    *)  t_fail "undo printed no file count -- a revert of everything and a \
revert of nothing still look the same: \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)" ;;
esac

# ---- 3: the magnitude is NON-ZERO, i.e. measured before the restore --------
# The ordering check, and the one that catches the obvious wrong implementation.
# Measure after restoring and every diff is empty, so the report would read
# "0 files changed" -- present, well-formed, and useless. The run above changed
# two tracked files, so the number must be at least 2.
n=$(printf '%s' "$out" | grep -o '[0-9][0-9]* file' | head -1 | tr -d ' file')
[ -n "$n" ] || n=0
if [ "$n" -ge 2 ]; then
    t_ok "the count is $n, so it was measured before the restore (not after)"
else
    t_fail "the report says $n files -- measured AFTER the restore, when there \
is nothing left to measure: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 4: a file the run CREATED is counted, not just the ones it edited ----
# The run edited two tracked files and created a third, so the count must be 3.
# A report covering only modifications would understate the damage in the case
# where it is least recoverable: an edited file's old content is in the
# checkpoint, but a created file simply ceases to exist.
#
# WHERE THE FIRST CUT OF THIS CHECK WAS WRONG, because it matters for anyone
# extending this driver. It asserted the word "untracked" appears, on the
# assumption that a `write_file` creation shows up in the `git clean` half. It
# does not: a checkpoint is a commit in a SHADOW repo whose work tree is the
# whole workspace, so from that repo's view four.txt is a tracked addition, and
# `undo --dry-run` prints it under "Tracked changes" with "Untracked files that
# would be removed: (none)". Asserting the untracked clause would therefore have
# been a check that cannot fail in the scenario it was written for. The clause
# stays in the implementation -- it mirrors the dry run's universe exactly, which
# is the point -- but it is NOT asserted here, because this fixture cannot
# produce it and a check must not claim coverage it does not have.
if [ "$n" -ge 3 ]; then
    t_ok "the created file is counted too ($n = 2 edited + 1 created)"
else
    t_fail "the count is $n; a created file is missing from the report, and a \
creation is the least recoverable kind of loss: \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 5: and the undo still actually worked -------------------------------
# A report is worthless if the operation it describes stopped happening. This is
# the control: the point of M537 is a louder undo, not a broken one.
if grep -q alpha "$ws/one.txt" 2>/dev/null && [ ! -e "$ws/four.txt" ]; then
    t_ok "the revert happened: tracked content restored, new file removed"
else
    t_fail "the tree was not restored: one.txt=$(head -1 "$ws/one.txt" \
2>/dev/null) four.txt exists=$([ -e "$ws/four.txt" ] && echo yes || echo no)"
fi

t_done
