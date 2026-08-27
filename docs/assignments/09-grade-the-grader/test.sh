#!/bin/sh
# Meta-grader: runs the learner's check.sh against all four candidates.
# Passes only when it accepts the correct candidate and rejects the three
# subtly wrong ones -- discrimination, both directions. Each candidate is
# copied to a neutral filename (identifying comment stripped) before the
# checker sees it, so a checker keyed on names instead of behaviour fails.
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
