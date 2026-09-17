#!/bin/sh
# Artifact-check floor for the design task: structure only, by design --
# quality is /check's and a human's job (the three-layer assessment model).
cd "$(dirname "$0")" || exit 1

echo "1..6"
rc=0
if [ ! -f DESIGN.md ]; then
    echo "not ok 1 - DESIGN.md is missing"
    exit 1
fi
ok=1
for sec in "Problem" "Requirements" "Design" "Alternatives considered" "Test plan"; do
    grep -q "^## $sec" DESIGN.md || { ok=0; missing="$sec"; }
done
if [ "$ok" = 1 ]; then
    echo "ok 1 - all five sections present"
else
    echo "not ok 1 - missing section '## $missing'"
    rc=1
fi
if [ "$(grep -c '^```' DESIGN.md)" -ge 2 ]; then
    echo "ok 2 - a fenced block (pseudo-code / format example / diagram)"
else
    echo "not ok 2 - no fenced block in the design"
    rc=1
fi
alts="$(awk '/^## Alternatives considered/{f=1;next} /^## /{f=0} f' DESIGN.md | grep -c '^- ')"
if [ "$alts" -ge 2 ]; then
    echo "ok 3 - at least two rejected alternatives"
else
    echo "not ok 3 - fewer than two bullets under Alternatives considered"
    rc=1
fi
tests="$(awk '/^## Test plan/{f=1;next} /^## /{f=0} f' DESIGN.md | grep -c '^- ')"
if [ "$tests" -ge 3 ]; then
    echo "ok 4 - a test plan with at least three bullets"
else
    echo "not ok 4 - fewer than three bullets under Test plan"
    rc=1
fi
if [ "$(wc -c < DESIGN.md)" -ge 1500 ]; then
    echo "ok 5 - not trivially short"
else
    echo "not ok 5 - under 1500 bytes; this is a sketch, not a design"
    rc=1
fi
# M633: the steelman shape. The section is OPTIONAL (module 06's gate lets you
# rebut /check in the doc); when it is present, every "- " entry must STATE the
# objection (an `Objection:` label) before answering it (a `Reply:` label). A
# reply to an objection nobody can read is the straw man's home; the script sees
# two labels, never whether the objection is in its strongest form -- that part
# is yours, and /check's.
if grep -q '^## Objections' DESIGN.md; then
    counts="$(awk '
        function flush() { if (n > 0 && !(cur ~ /Objection:/ && cur ~ /Reply:/)) b++ }
        /^## Objections/ { f = 1; next }
        /^## / { if (f) flush(); f = 0 }
        f && /^- / { flush(); n++; cur = $0; next }
        f && n > 0 { cur = cur " " $0 }
        END { if (f) flush(); print n + 0, b + 0 }
    ' DESIGN.md)"
    nobj="${counts% *}"
    nbad="${counts#* }"
    if [ "$nobj" -eq 0 ]; then
        echo 'not ok 6 - ## Objections has no "- " entry; the heading alone rebuts nothing'
        rc=1
    elif [ "$nbad" -eq 0 ]; then
        echo "ok 6 - every objection is stated (Objection:) before it is answered (Reply:)"
    else
        echo "not ok 6 - $nbad objection(s) lack an Objection: or a Reply: label -- state the reviewer's point in its strongest form, then answer it"
        rc=1
    fi
else
    echo "ok 6 - no ## Objections section (optional; when present each entry needs Objection: and Reply:)"
fi
exit $rc
