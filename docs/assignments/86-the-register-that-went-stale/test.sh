#!/bin/sh
# Grades an AUDIT of a register, not the writing of one.
#
# WHY THIS SHAPE. `DEFERRED.md` carried "a graded assignment for the records
# practice" for a long time with a good objection: a grader "could only grade
# the SHAPE of a register -- it cannot know whether a decision was real,
# whether the dates are true, or whether `Where:` points anywhere", and the
# revisit condition was "when there is a way to grade the HABIT rather than
# the headings".
#
# This is that way. The learner is handed a register and the tree it describes,
# and must say of each row whether it still STANDS or has gone STALE. The
# habit -- check the checkable part of a reason before believing it -- is then
# exactly what the verdicts measure, and a script can check verdicts against a
# tree it can read.
#
# THE TRAP IS THE POINT, and it is why this grades a habit rather than a
# search. R5 says there is no rate limiting. `project/upload.c` contains the
# word "rate" three times -- a sampler's rate, which is not a limit and says
# so in its own comment. A learner who greps and does not open the file calls
# R5 stale and fails, having done the thing this task exists to catch. Both
# directions are graded: a missed stale row and an invented one are the same
# defect wearing different clothes.
#
# A STALE verdict must cite the file that makes it stale -- not any file that
# exists. A citation of the right shape pointing at the wrong thing is the
# failure `GROUNDED_DISCOURSE.md` is about, met here in a register.
cd "$(dirname "$0")" || exit 1

[ -f AUDIT.md ] || {
    echo "FAIL: AUDIT.md missing -- write one verdict line per row, R1 to R5"
    exit 1; }

# --- 1: a verdict for every row, and only the two words --------------------
for r in R1 R2 R3 R4 R5; do
    line=$(grep "^$r:" AUDIT.md | head -1)
    [ -n "$line" ] || {
        echo "FAIL: no verdict for $r -- every row needs one, including the ones that stand"
        exit 1; }
    case "$line" in
        "$r: STALE"*|"$r: STANDS"*) ;;
        *) echo "FAIL: $r's verdict is neither STALE nor STANDS: $line"; exit 1;;
    esac
done

verdict() { grep "^$1:" AUDIT.md | head -1 | awk '{print $2}'; }

# --- 2: the verdicts themselves --------------------------------------------
# Ground truth, each checkable in the fixture and stated here so a reader of
# this grader can verify it rather than trust it:
#   R1 STALE  -- project/upload.c retries MAX_ATTEMPTS times
#   R2 STANDS -- nothing in project/ sets any timeout
#   R3 STALE  -- project/FORMAT.md documents the wire format
#   R4 STANDS -- nothing in project/ computes a checksum
#   R5 STANDS -- "rate" in upload.c is a sampler's rate, not a limit
_wrong=""
for pair in "R1 STALE" "R2 STANDS" "R3 STALE" "R4 STANDS" "R5 STANDS"; do
    r=${pair% *}; want=${pair#* }
    got=$(verdict "$r")
    [ "$got" = "$want" ] || _wrong="$_wrong $r(said $got)"
done
if [ -n "$_wrong" ]; then
    echo "FAIL: wrong verdict(s):$_wrong"
    echo "Two rows have gone stale and three still stand. A row you wrongly call"
    echo "stale is not a smaller mistake than one you miss -- it retires work that"
    echo "still needs doing. Open the file before you decide; one of these rows is"
    echo "a word that appears in the tree meaning something else."
    exit 1
fi

# --- 3: a STALE verdict cites the file that makes it stale ------------------
# Not any file that exists -- the one that carries the evidence. A citation of
# the right shape that resolves to the wrong thing is the defect, not a typo.
for pair in "R1 project/upload.c" "R3 project/FORMAT.md"; do
    r=${pair% *}; want=${pair#* }
    line=$(grep "^$r:" AUDIT.md | head -1)
    case "$line" in
        *"$want"*) ;;
        *) echo "FAIL: $r is stale, but its line does not cite $want -- name the file that"
           echo "      makes it stale, so a reader can check you rather than believe you:"
           echo "      $line"
           exit 1;;
    esac
done

# --- 4: the cited files exist ----------------------------------------------
# Belt and braces: if the fixture is ever moved, this says so rather than
# passing an audit of a tree that is not there.
for f in project/upload.c project/FORMAT.md; do
    [ -f "$f" ] || { echo "FAIL: fixture missing: $f -- the audit cannot be graded"; exit 1; }
done

echo "PASS: five verdicts, both stale rows found, neither standing row retired,"
echo "      and each stale verdict cites the file that makes it so"
exit 0
