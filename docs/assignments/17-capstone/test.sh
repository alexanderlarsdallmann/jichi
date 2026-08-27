#!/bin/sh
# The capstone's mechanical floor: the portfolio's structure and evidence.
# The project itself is judged by the rubric -- /check alone, an instructor
# in a course, and best of all live. Journal greps match the compact JSONL
# jc_env_journal_end writes (same checks as task 13).
cd "$(dirname "$0")" || exit 1

echo "1..5"
rc=0
P=portfolio
if [ ! -f "$P/PROPOSAL.md" ]; then
    echo "not ok 1 - $P/PROPOSAL.md is missing (copy the template and fill it)"
    exit 1
fi
ok=1
for sec in "Goal" "Scope and non-goals" "Envelope" "Verify strategy" "Risks"; do
    grep -q "^## $sec" "$P/PROPOSAL.md" || { ok=0; missing="$sec"; }
done
if [ "$ok" = 1 ] && [ "$(wc -c < "$P/PROPOSAL.md")" -ge 1200 ]; then
    echo "ok 1 - the proposal is complete and not a sketch"
else
    if [ "$ok" = 1 ]; then
        echo "not ok 1 - the proposal is under 1200 bytes"
    else
        echo "not ok 1 - proposal is missing '## $missing'"
    fi
    rc=1
fi
if grep -q '<!--' "$P/PROPOSAL.md"; then
    echo "not ok 2 - template comments still present: fill in, don't append"
    rc=1
else
    echo "ok 2 - the template's scaffolding comments are gone"
fi
J="$P/journal.jsonl"
if [ -f "$J" ] && grep -q '"event":"start"' "$J" &&
   grep '"event":"end"' "$J" | grep -q '"outcome":"ok"' &&
   grep '"event":"verify"' "$J" | grep -q '"exit":0' &&
   ! grep -q '"event":"out_of_scope"' "$J"; then
    echo "ok 3 - a bounded run's journal: ended ok, verified, in scope"
else
    echo "not ok 3 - $J missing, or its run was not ok/verified/in-scope"
    rc=1
fi
if [ -f "$P/RECORD.md" ] && [ "$(grep -c '^## Symptom' "$P/RECORD.md")" -ge 2 ]; then
    echo "ok 4 - at least two record entries from this project"
else
    echo "not ok 4 - $P/RECORD.md needs >= 2 four-section entries"
    rc=1
fi
if [ -f "$P/RECORD.md" ] &&
   [ "$(grep -c '^## Root cause' "$P/RECORD.md")" -ge 2 ] &&
   [ "$(grep -c '^## Lesson' "$P/RECORD.md")" -ge 2 ]; then
    echo "ok 5 - the entries carry root causes and lessons"
else
    echo "not ok 5 - record entries missing Root cause / Lesson sections"
    rc=1
fi
exit $rc
