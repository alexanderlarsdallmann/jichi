#!/bin/sh
# tests/bench/mentor_ab/blind.sh -- build the blinded grading pack from
# results/LABEL/drafts/<pair>-{old,new}.md (M605).
#
# The condition is written down ONLY in results/LABEL/.sealed/mapping.json; the
# grader sees <pair>-A.md / <pair>-B.md, FORM.md and counts.txt. The A/B order
# per pair comes from the byte sum of the two drafts (deterministic, not chosen
# by the author). counts.txt is mechanical shape -- what `learn apply` would see
# -- by A/B, never by arm: a grader who reads it learns nothing about the
# condition that the drafts themselves do not show. An EMPTY draft cannot be
# blinded and is not pretended to be; say so in the write-up.
set -u
LABEL=""
while [ $# -gt 0 ]; do
    case "$1" in
        --label) LABEL=$2; shift 2 ;;
        *) echo "usage: $0 --label LABEL" >&2; exit 2 ;;
    esac
done
[ -n "$LABEL" ] || { echo "usage: $0 --label LABEL" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
R="$HERE/results/$LABEL"
D="$R/drafts"; G="$R/grading"; S="$R/.sealed"
[ -d "$D" ] || { echo "no drafts under $D -- run run-arms.sh first" >&2; exit 2; }
mkdir -p "$G" "$S"
: > "$S/mapping.json"; printf '{\n' >> "$S/mapping.json"
first=1
pairs=""
for old in "$D"/*-old.md; do
    [ -f "$old" ] || continue
    pair=$(basename "$old" -old.md)
    new="$D/$pair-new.md"
    [ -f "$new" ] || { echo "missing new arm for $pair" >&2; continue; }
    pairs="$pairs $pair"
    sum=$(( $(wc -c < "$old") + $(wc -c < "$new") ))
    if [ $((sum % 2)) -eq 0 ]; then a=old; b=new; else a=new; b=old; fi
    cp "$D/$pair-$a.md" "$G/$pair-A.md"; cp "$D/$pair-$b.md" "$G/$pair-B.md"
    [ $first -eq 1 ] || printf ',\n' >> "$S/mapping.json"; first=0
    printf '  "%s": {"A": "%s", "B": "%s"}' "$pair" "$a" "$b" >> "$S/mapping.json"
done
printf '\n}\n' >> "$S/mapping.json"
chmod 600 "$S/mapping.json"

{
cat <<'EOF'
# Blind grading: two mentor drafts per pair

For each pair, one draft was produced by the OLD binary and one by the NEW; you
do not know which. `.sealed/mapping.json` does, and is opened only after this
form is filled. Same workspace, same mentor.md, same telemetry, same model, same
prompt. A tick is a preference, not a score.

EOF
for pair in $pairs; do
cat <<EOF
## $pair

| question | A | B | neither |
|---|---|---|---|
| Which draft would you rather \`learn apply\`? | | | |
| Which follows the headings the parser knows? | | | |
| Which proposes lessons that are NEW (not restating memory.md)? | | | |
| Which corrections are directives (\`remove:\` / \`replace:\`) rather than prose? | | | |
| Which is written in the language you expected? | | | |
| Free text -- what decided it: | | | |

EOF
done
cat <<'EOF'
## Mechanical counts (filled in by the harness, not by you)

See `counts.txt` beside this form: headings the parser knows, `- ` bullets under
`## Memory notes`, `remove:`/`replace:` directives, `[pins:` trailers,
`constraint:` checks, bytes. These are what `learn apply` would see; they are not
a judgement of quality.
EOF
} > "$G/FORM.md"

: > "$G/counts.txt"
for f in "$G"/*-A.md "$G"/*-B.md; do
    n=$(basename "$f" .md)
    printf '%s: bytes=%s headings=%s memory_bullets=%s directives=%s pins=%s checks=%s\n' \
        "$n" "$(wc -c < "$f")" \
        "$(grep -c -E '^## (Memory notes|Skills|Corrections|Project rules|Checks|Suggested)' "$f")" \
        "$(awk '/^## Memory notes/{m=1;next} /^## /{m=0} m && /^- /{c++} END{print c+0}' "$f")" \
        "$(grep -c -E '^- (remove|replace):' "$f")" \
        "$(grep -c '\[pins:' "$f")" \
        "$(awk '/^## Checks/{m=1;next} /^## /{m=0} m && /^- constraint:/{c++} END{print c+0}' "$f")" \
        >> "$G/counts.txt"
done
sort -o "$G/counts.txt" "$G/counts.txt"
echo "pack: $G"; echo "sealed: $S/mapping.json"
