#!/bin/sh
# Structural floor for session notes: >= 3 DATED entries, each with the
# did / decided / next spine. Whether they are notes you would thank yourself for
# is your judgment; whether they are dated and structured a script can check.
cd "$(dirname "$0")" || exit 1
n=$(ls notes/ 2>/dev/null | grep -cE '^[0-9]{4}-[0-9]{2}-[0-9]{2}\.md$')
[ "$n" -ge 3 ] || { echo "FAIL: only $n dated notes (notes/YYYY-MM-DD.md) -- keep at least 3 over the project"; exit 1; }
for f in notes/[0-9]*.md; do
    grep -qiE 'did|done' "$f" && grep -qiE 'decided|decision' "$f" && grep -qiE '(^|[^A-Za-z0-9_])next([^A-Za-z0-9_]|$)' "$f" || { echo "FAIL: $f is missing the did / decided / next spine"; exit 1; }
done
echo "PASS: $n dated notes, each with the did/decided/next spine"
