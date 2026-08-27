#!/bin/sh
# Structural floor for a requirements doc (the floor, not the quality -- whether
# these are the RIGHT requirements is your judgment; see the spec). Passes when:
#   * >= 5 requirements each carry an id (R1, R2, ...), and
#   * each is phrased VERIFIABLY -- a testable "shall"/"must", not a vague wish.
cd "$(dirname "$0")" || exit 1
ids=$(grep -cE '(^|[^A-Za-z])R[0-9]+[:.) ]' REQUIREMENTS.md)
[ "$ids" -ge 5 ] || { echo "FAIL: only $ids requirements with an id -- give each a testable requirement an id (R1, R2, ...), >= 5"; exit 1; }
ver=$(grep -cE '(^|[^A-Za-z])R[0-9]+.*(shall|must)' REQUIREMENTS.md)
[ "$ver" -ge 5 ] || { echo "FAIL: only $ver requirements are verifiable -- each needs a testable 'shall'/'must', not 'should be nice'"; exit 1; }
echo "PASS: $ids identified, verifiable requirements"
