#!/bin/sh
# Structural floor for a decision register: >= 3 decisions, each with the
# option chosen, the alternative rejected WITH why it lost, and the CRITERION
# that decided between them -- traced to a requirement id. Whether the
# criterion is the right one, or the alternative a fair one, is your judgment
# (the harder half; a straw man passes this floor and fails a reader). What a
# script can check: that every decision names a scale, not only a winner.
cd "$(dirname "$0")" || exit 1
[ -f DECISIONS.md ] || { echo "FAIL: DECISIONS.md missing -- write the register (## D1, ## D2, ...)"; exit 1; }
n=$(grep -c '^## D[0-9]' DECISIONS.md)
[ "$n" -ge 3 ] || { echo "FAIL: only $n decision(s) headed '## D<n> -- ...' -- a design has more than $n choices in it; write >= 3"; exit 1; }

# The requirement ids the decisions may trace to (whole tokens; `\b` is GNU-only).
ids=$(tr -cs 'A-Za-z0-9_' '\n' < REQUIREMENTS.md | grep -xE 'R[0-9]+' | sort -u)
[ -n "$ids" ] || { echo "FAIL: REQUIREMENTS.md carries no R<n> ids -- the given fixture was changed"; exit 1; }

total=$(wc -l < DECISIONS.md)
starts=$(grep -n '^## D[0-9]' DECISIONS.md | cut -d: -f1)
prev=""
check_block() {
    # $1 = first line, $2 = last line of one decision
    title=$(sed -n "${1}p" DECISIONS.md | cut -c1-60)
    block=$(sed -n "${1},${2}p" DECISIONS.md)
    printf '%s\n' "$block" | grep -q '^Chose:' || {
        echo "FAIL: '$title' has no 'Chose:' line -- name the option you took"; exit 1; }
    printf '%s\n' "$block" | grep '^Rejected:' | grep -qE -- '--|—' || {
        echo "FAIL: '$title' has no 'Rejected: <alternative> -- <why it lost>' line -- an alternative without its reason is a list, not an argument"; exit 1; }
    because=$(printf '%s\n' "$block" | sed -n 's/^Because:[ ]*//p' | head -1)
    [ -n "$because" ] || {
        echo "FAIL: '$title' has no 'Because:' line -- name the CRITERION that decided (the requirement or property the options were weighed on)"; exit 1; }
    [ "${#because}" -ge 20 ] || {
        echo "FAIL: '$title': 'Because: $because' is too short to name a criterion"; exit 1; }
    hit=0
    for r in $(printf '%s\n' "$block" | tr -cs 'A-Za-z0-9_' '\n' | grep -xE 'R[0-9]+' | sort -u); do
        for k in $ids; do [ "$r" = "$k" ] && hit=1; done
    done
    [ "$hit" -eq 1 ] || {
        echo "FAIL: '$title' traces to no requirement id from REQUIREMENTS.md -- a decision that serves no requirement is a preference"; exit 1; }
}
for s in $starts; do
    if [ -n "$prev" ]; then check_block "$prev" "$((s - 1))"; fi
    prev=$s
done
[ -n "$prev" ] && check_block "$prev" "$total"
echo "PASS: $n decisions, each with an option, a reasoned rejection, and a criterion traced to a requirement"
exit 0
