#!/bin/sh
# smoke: a pulled hint is recorded, and cannot be mistaken for an attempt (M502).
#
# THE DEFECT THIS EXISTS FOR. 74 shipped specs, the scaffold glossary and
# CURRICULUM.md have told learners since M174 that hints are "free, and
# recorded". Nothing wrote the record. `hints_used` reached progress.jsonl only
# from `attempt --record`, i.e. when the AGENT pulled them -- so a teacher
# reading a bench could not tell a learner who solved a task cold from one who
# needed all three rungs, which is the whole diagnostic value of a ladder.
#
# WHY A SEPARATE FILE, asserted here rather than assumed: every reader of
# progress.jsonl treats a line as an attempt with a verdict (jc_progress_scan
# counts `attempts` and reads `passed`), so a hint row there would show up as a
# graded attempt -- and one without a verdict reads as a failure. The same
# reasoning refuses `grade --expect-fail --record` (M412). Two files keeps
# "never penalised" true by construction.
#
# M536 ADDS CHECKS 7-9, and the reason is uncomfortable: everything above tests
# the `jichi hint` CLI, while the paragraph at the top of this file names "when
# the AGENT pulled them" as the very defect being fixed. The `hint` TOOL -- the
# only way a model, or a TUI learner in tutor mode, pulls a rung -- wrote nothing
# at all, and went on advertising "hints are limited and their use is recorded"
# in the description handed to the model on every turn. M502 fixed the half it
# could see and gated exactly that half.
#
# Check 8 is the one worth reading. `attempt` chdirs into a throwaway git
# worktree, so recording against "." or app->cwd -- the two obvious spellings --
# files the teacher's diagnostic INSIDE the sandbox and deletes it with the
# sandbox. That is a writer naming a path its reader cannot read (M533, twice),
# and it is why app->assignment_dir exists rather than a call to getcwd().
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)
cd "$tmp" || exit 1
mkdir -p docs/assignments

cat > docs/assignments/laddered.md <<'EOS'
---
title: three rungs
verify: "test -f done.txt"
points: 2
hints:
  - Look at what the gate actually checks.
  - It wants a file. Which one?
  - Create done.txt.
---
Create the file the gate wants.
EOS

# ---- 1. no hints pulled: nothing is written, and nothing is shown ----------
# A learner who solves it cold must not acquire a file, and the listing must not
# grow noise for them.
out=$(with_deadline 30 "$BIN" assignments < /dev/null 2>&1)
if [ ! -e .jichi/hints.jsonl ] && ! printf '%s' "$out" | grep -q 'hints'; then
    t_ok "no hint pulled: no log, and the listing stays quiet"
else
    t_fail "a hint log or column appeared without any hint being pulled: \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# ---- 2. pulling a rung writes it ------------------------------------------
with_deadline 30 "$BIN" hint docs/assignments/laddered.md < /dev/null > /dev/null 2>&1
if [ -f .jichi/hints.jsonl ] && grep -q '"rung":1' .jichi/hints.jsonl; then
    t_ok "a pulled rung is appended to .jichi/hints.jsonl"
else
    t_fail "the hint went unrecorded, though 74 specs promise it is recorded: \
$(cat .jichi/hints.jsonl 2>/dev/null | head_bytes 160)"
fi

# ---- 3. the deepest rung is what a teacher reads --------------------------
with_deadline 30 "$BIN" hint docs/assignments/laddered.md 3 < /dev/null > /dev/null 2>&1
out=$(with_deadline 30 "$BIN" assignments < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'hints 2' &&
   printf '%s' "$out" | grep -q 'rung 3'; then
    t_ok "the listing shows the pull count and the deepest rung"
else
    t_fail "hint usage is not visible in the listing: \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 4. and in the machine surface ----------------------------------------
out=$(with_deadline 30 "$BIN" assignments --output json < /dev/null 2>&1)
if printf '%s' "$out" | grep -q '"hint_pulls": *2' &&
   printf '%s' "$out" | grep -q '"hint_rung": *3'; then
    t_ok "assignments --output json carries hint_pulls and hint_rung"
else
    t_fail "no hint fields in the json a gradebook would read: \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 5. THE SEPARATION: a hint is not an attempt --------------------------
# progress.jsonl must still be empty -- the learner has not graded anything.
if [ ! -e .jichi/progress.jsonl ]; then
    t_ok "two hints wrote nothing to progress.jsonl (not an attempt, not a fail)"
else
    t_fail "a hint landed in the progress file, where every reader counts a \
line as a graded attempt: $(cat .jichi/progress.jsonl | head_bytes 200)"
fi

# ---- 6. and grading still records normally --------------------------------
: > done.txt
with_deadline 30 "$BIN" grade docs/assignments/laddered.md --record \
    < /dev/null > /dev/null 2>&1
if grep -q '"passed":true' .jichi/progress.jsonl 2>/dev/null &&
   [ "$(grep -c . .jichi/progress.jsonl)" = 1 ]; then
    t_ok "the grade is the only line in progress.jsonl"
else
    t_fail "progress.jsonl is not one clean grade row: \
$(cat .jichi/progress.jsonl 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 7-9. THE TOOL PATH (M536) ---------------------------------------------
# A scripted learner that calls `hint` twice and then stops. The rungs must land
# in the REAL workspace's log, not in the worktree the attempt runs inside.
ws=$(smoke_tmp)
tmp_mm=$(smoke_tmp)
mkdir -p "$ws/docs/assignments"
cp docs/assignments/laddered.md "$ws/docs/assignments/laddered.md"
( cd "$ws" && git init -q . && git add -A &&   git -c user.email=t@t -c user.name=t commit -qm init ) >/dev/null 2>&1

cat > "$tmp_mm/replies.mm" <<'EOS'
wire openai
rule
  count 1
  tool hint {}
rule
  count 2
  tool hint {}
rule
  text STUCK_AND_STOPPING
EOS
mm_start "$tmp_mm/replies.mm" "$tmp_mm/cap"
cat > "$tmp_mm/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"assignments":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp_mm/config.json" \
  attempt docs/assignments/laddered.md --keep-worktree \
  < /dev/null ) >"$tmp_mm/att.out" 2>&1
mm_stop

# --- 7: the tool's pulls are recorded at all -------------------------------
if [ -f "$ws/.jichi/hints.jsonl" ]; then
    t_ok "a hint pulled by the TOOL is recorded, not only by the CLI"
else
    t_fail "the tool pulled hints and wrote no record, while its own \
description promises one: $(ls -a "$ws/.jichi" 2>&1 | tr '\n' ' ')"
fi

# --- 8: in the REAL workspace, not inside the attempt's worktree -----------
# Two rungs pulled, two rows, and they are in $ws itself.
#
# --keep-worktree ABOVE IS LOAD-BEARING, and finding out why was this driver's
# own red-before-green lesson. Without it the worktree is removed when the
# attempt ends, so a row wrongly filed inside it is DELETED along with it and
# `stray` reads 0 either way -- a check that looks like it guards the worktree
# mistake while being unable to observe it. Perturbing the fix to the obvious
# wrong spelling (jc_progress_hint_append(".", ...)) is what exposed that: rows
# went to 0 as intended, and stray stayed 0, proving only that the evidence had
# been destroyed. Keeping the worktree turns stray into a real reading: 0 with
# the fix, 1 with the wrong directory.
rows=$(grep -c '"rung"' "$ws/.jichi/hints.jsonl" 2>/dev/null || echo 0)
# The worktree lives under $HOME/.jichi.d/worktrees (smoke_home points HOME at a
# tmp dir), NOT under the workspace -- so the search has to cover both roots or
# it proves nothing. Exactly one hint log must exist, and it must be the
# workspace's own.
logs=$(find "$ws" "$HOME" -name hints.jsonl 2>/dev/null | wc -l | tr -d ' ')
stray=$((logs - 1))
[ "$rows" -ge 2 ] || stray=$logs
if [ "$rows" -ge 2 ] && [ "$logs" = 1 ]; then
    t_ok "both rungs landed in the real workspace, and it is the ONLY hint log"
else
    t_fail "rows in the workspace=$rows (want >=2), hint logs on disk=$logs \
(want exactly 1, in the workspace). Found: \
$(find "$ws" "$HOME" -name hints.jsonl 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# --- 9: the rungs are 1-based and distinct, as the reader expects ----------
# jc_progress_hints_scan reports the DEEPEST rung, and jc_progress_hint_append
# rejects rung<=0 -- so an off-by-one here silently drops the first pull.
if grep -q '"rung":1' "$ws/.jichi/hints.jsonl" 2>/dev/null &&
   grep -q '"rung":2' "$ws/.jichi/hints.jsonl" 2>/dev/null; then
    t_ok "rungs are 1-based and distinct (rung<=0 would be dropped silently)"
else
    t_fail "rungs are not 1 and 2: \
$(cat "$ws/.jichi/hints.jsonl" 2>/dev/null | tr '\n' ' ' | head_bytes 240)"
fi

t_done
