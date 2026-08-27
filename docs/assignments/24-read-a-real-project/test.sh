#!/bin/sh
# Two-sided by construction, twice over: (1) pristine fixtures fail (no
# ANALYSIS.md, no trim test); (2) the learner's OWN trim test is held to the
# two-sided bar -- it must FAIL against the as-found journal.c (the pristine
# snapshot) and PASS against their fixed one. A "test" that passes on both
# proves nothing about the bug they claim to have found; a fix without the
# test is reading without proof.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -Werror -Ijournal -o _own \
    journal/journal.c journal/test_journal.c || {
    echo "FAIL: the project no longer builds"; exit 1; }
./_own >/dev/null || { rm -f _own; echo "FAIL: the project's own tests broke"; exit 1; }
rm -f _own

[ -f journal/test_trim.c ] || {
    echo "FAIL: journal/test_trim.c missing -- prove the finding with a test"
    exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -Werror -Ijournal -o _fixed \
    journal/journal.c journal/test_trim.c || {
    echo "FAIL: your trim test does not compile with journal.c"; exit 1; }
./_fixed >/dev/null 2>&1
fixed_rc=$?
cc -std=c89 -pedantic -Wall -Wextra -Werror -Ijournal -o _asfound \
    journal/pristine/journal.c journal/test_trim.c || {
    rm -f _fixed
    echo "FAIL: your trim test does not compile with the pristine snapshot"
    exit 1; }
./_asfound >/dev/null 2>&1
asfound_rc=$?
rm -f _fixed _asfound

[ "$fixed_rc" -eq 0 ] || {
    echo "FAIL: your trim test fails on your own journal.c -- fix not done?"
    exit 1; }
[ "$asfound_rc" -ne 0 ] || {
    echo "FAIL: your trim test PASSES on the as-found code -- it does not" \
         "actually catch the bug you claim to have found"
    exit 1; }

[ -f ANALYSIS.md ] || { echo "FAIL: ANALYSIS.md missing"; exit 1; }
for section in "## The map" "## The suspect" "## Proof"; do
    grep -q "^$section" ANALYSIS.md || {
        echo "FAIL: ANALYSIS.md is missing the '$section' section"; exit 1; }
done
grep -q "journal_trim" ANALYSIS.md || {
    echo "FAIL: ANALYSIS.md never names the function it convicts"; exit 1; }

echo "PASS: own tests green, your test convicts as-found and clears fixed,"
echo "      the analysis names its suspect"
exit 0
