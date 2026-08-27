#!/bin/sh
# The delegation grader: the inner product AND the journal evidence that the
# run was bounded, verified, and in-scope. The journal lines are the compact
# JSONL jc_env_journal_end writes.
cd "$(dirname "$0")" || exit 1

echo "1..5"
rc=0
if sh work/check.sh; then
    echo "ok 1 - the inner task succeeded (work/report.txt)"
else
    echo "not ok 1 - work/report.txt is missing or wrong"
    rc=1
fi
J=journal.jsonl
if [ -f "$J" ] && grep -q '"event":"start"' "$J"; then
    echo "ok 2 - the journal exists and records a start"
else
    echo "not ok 2 - no journal at journal.jsonl (pass --journal <this path>)"
    exit 1
fi
if grep '"event":"end"' "$J" | grep -q '"outcome":"ok"'; then
    if grep '"event":"end"' "$J" | grep -q '"rolled_back":false'; then
        echo "ok 3 - the run ended ok, nothing rolled back"
    else
        echo "not ok 3 - the run ended ok but was rolled back"
        rc=1
    fi
else
    echo "not ok 3 - no end event with outcome ok (budget? verify-failed?)"
    rc=1
fi
if grep '"event":"verify"' "$J" | grep -q '"exit":0'; then
    echo "ok 4 - a verify event passed inside the run"
else
    echo "not ok 4 - no passing verify event (did you pass --verify?)"
    rc=1
fi
if grep -q '"event":"out_of_scope"' "$J"; then
    echo "not ok 5 - the run wrote outside its edit scope"
    rc=1
else
    echo "ok 5 - no out-of-scope writes"
fi
exit $rc
