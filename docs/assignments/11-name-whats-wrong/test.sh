#!/bin/sh
# Structural floor for the review task: three findings, each anchored to a
# line. The QUALITY of the findings is judged against the reference review
# and /check -- a script cannot read an argument (three-layer assessment).
cd "$(dirname "$0")" || exit 1

echo "1..3"
rc=0
if [ ! -f REVIEW.md ]; then
    echo "not ok 1 - REVIEW.md is missing"
    exit 1
fi
n="$(grep -c '^## Smell' REVIEW.md)"
if [ "$n" -ge 3 ]; then
    echo "ok 1 - three findings ($n '## Smell' sections)"
else
    echo "not ok 1 - only $n '## Smell' sections (need 3)"
    rc=1
fi
refs="$(grep -c 'smelly\.c:[0-9]' REVIEW.md)"
if [ "$refs" -ge 3 ]; then
    echo "ok 2 - each finding is anchored (smelly.c:<line>)"
else
    echo "not ok 2 - only $refs smelly.c:<line> anchors (need 3)"
    rc=1
fi
whys="$(grep -ci 'why it matters' REVIEW.md)"
if [ "$whys" -ge 3 ]; then
    echo "ok 3 - each finding argues its consequence"
else
    echo "not ok 3 - only $whys 'why it matters' lines (need 3)"
    rc=1
fi
exit $rc
