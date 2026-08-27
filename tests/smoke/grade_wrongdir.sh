#!/bin/sh
# smoke: a FAIL whose verify references a directory that does not exist from
# here says so (M617, seam B1 of docs/analysis/2026-08-27-the-teaching-seams.md).
#
# THE SEAM. M502's reachability guard fires only when the verify PROGRAM
# contains '/'; 12 of the shipped specs -- the intro tier and the plain-language
# tier, exactly the learners least able to tell a broken grader from their own
# mistake -- use `grep`/`[`, so from the wrong directory they grade
# `FAIL / score: 0%` as a real grade. Arguments stay uninspected on purpose (a
# missing FILE may be the learner's deliverable, jc_assign.h), so the mend is a
# NOTE, never a refusal: when a FAIL's verify names a directory that does not
# exist from the grading directory, the grade line carries one pointed hint.
# A directory can also be part of the task, so the wording says that too.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$ws/docs/assignments/wd"
cat > "$ws/docs/assignments/wd.md" <<'SPEC'
---
title: wrong-dir fixture
audience: student
verify: "grep -qx 'ok' docs/assignments/wd/answer.txt"
points: 1
---
Write `ok` into docs/assignments/wd/answer.txt.
SPEC
printf 'ok\n' > "$ws/docs/assignments/wd/answer.txt"

# --- 1: control -- from the right directory it PASSes, no note -----------------
out=$(cd "$ws" && with_deadline 20 "$BIN" grade docs/assignments/wd.md < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'PASS'; then
    t_ok "control: PASS from the repository root"
else
    t_fail "control rc=$rc: $(printf '%s' "$out" | head_bytes 120)"
fi

# --- 2: from the WRONG directory the FAIL carries the pointed note --------------
out=$(cd "$ws/docs/assignments" && with_deadline 20 "$BIN" grade wd.md < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "does not exist from here"; then
    t_ok "wrong-dir FAIL names the unresolvable directory"
else
    t_fail "no note (rc=$rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# --- 3: the note names WHICH directory ------------------------------------------
case "$out" in
    *"docs/assignments/wd"*) t_ok "the note names the directory the verify references" ;;
    *) t_fail "directory not named: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)" ;;
esac

# --- 4: an honest FAIL (right dir, wrong content) carries NO note ----------------
printf 'nope\n' > "$ws/docs/assignments/wd/answer.txt"
out=$(cd "$ws" && with_deadline 20 "$BIN" grade docs/assignments/wd.md < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 1 ] && ! printf '%s' "$out" | grep -q "does not exist from here"; then
    t_ok "a genuine FAIL from the right directory stays noise-free"
else
    t_fail "false note or wrong rc ($rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi
t_done
