#!/bin/sh
# Meta-grader (the 09 mechanism, aimed at memory): runs the learner's
# check.sh against all four candidates under a neutral filename with the
# identifying comment stripped. Passes only on full discrimination -- accept
# the correct candidate, reject the polite whole-file borrower (the PEAK
# class a start-vs-end measurement cannot see), the one-buffer leaker (the
# LIVE class), and the off-by-one (the ANSWER class).
cd "$(dirname "$0")" || exit 1

echo "1..4"
if [ ! -f check.sh ]; then
    echo "not ok 1 - check.sh is missing: write the checker first"
    exit 1
fi
rc=0
i=0
for cand in a b c d; do
    i=$((i + 1))
    sed '/^\/\* candidate/d' "candidates/cand_$cand.c" > _under_test.c
    sh check.sh _under_test.c >/dev/null 2>&1
    got=$?
    if [ "$cand" = "a" ]; then
        if [ "$got" = 0 ]; then
            echo "ok $i - the correct candidate is accepted"
        else
            echo "not ok $i - your checker rejects the correct candidate"
            rc=1
        fi
    else
        if [ "$got" != 0 ]; then
            echo "ok $i - a wrong candidate is rejected"
        else
            echo "not ok $i - your checker accepts a wrong candidate"
            rc=1
        fi
    fi
done
rm -f _under_test.c
exit $rc
