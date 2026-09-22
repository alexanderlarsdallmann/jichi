#!/bin/sh
# Structural floor for a grounded discussion with a model. The learner's
# DISCOURSE.md must make all FIVE moves GROUNDED_DISCOURSE.md names -- claim,
# grounds that resolve, warrant, counter-argument, revision -- cite where it
# looked, and END SOMEWHERE OTHER THAN WHERE IT STARTED. Whether the warrant is
# sound and the objection the strongest one is your judgement; that the moves
# are all present, the citations resolve, and the claim actually moved is what
# a script can check.
#
# Two tiers, the same split 74-read-the-turn makes. The subject is the real
# tree, so the honest check is "do your citations resolve?" -- but the
# two-sided proof in tests/e2e/curriculum_graders.py runs this grader in a COPY
# of docs/assignments with no src/ beside it. So the floor below runs anywhere
# and is what the proof rests on; the resolution gate runs only when the tree
# is reachable, where it catches the invented citation that is this whole
# page's subject. Skipping a gate you cannot run is not passing it, and the
# output says which happened.
cd "$(dirname "$0")" || exit 1

[ -f DISCOURSE.md ] || {
    echo "FAIL: DISCOURSE.md missing -- write your discussion, the five sections GROUNDED_DISCOURSE.md names"
    exit 1; }
[ "$(grep -c . DISCOURSE.md)" -ge 12 ] || {
    echo "FAIL: DISCOURSE.md is basically empty"; exit 1; }

# --- 1: all five moves, by section ------------------------------------------
for section in '## 1. Claim' '## 2. Grounds' '## 3. Warrant' \
               '## 4. Counter-argument' '## 5. Revision'; do
    grep -q "^$section" DISCOURSE.md || {
        echo "FAIL: DISCOURSE.md is missing the '$section' section -- one move per section, all five"
        exit 1; }
done

# --- 2: the objection is stated BEFORE it is answered ------------------------
# The move is worthless in the other order: a rebuttal written first and an
# "objection" fitted to it afterwards is a straw man with extra steps. Line
# numbers are the cheapest possible check and they catch exactly that.
obj=$(grep -n '^Objection:' DISCOURSE.md | head -1 | cut -d: -f1)
rep=$(grep -n '^Reply:' DISCOURSE.md | head -1 | cut -d: -f1)
[ -n "$obj" ] || {
    echo "FAIL: no line starting 'Objection:' -- state the strongest case against your claim, in its own words"
    exit 1; }
[ -n "$rep" ] || {
    echo "FAIL: no line starting 'Reply:' -- an objection you do not answer is not a counter-argument"
    exit 1; }
[ "$obj" -lt "$rep" ] || {
    echo "FAIL: 'Reply:' (line $rep) comes before 'Objection:' (line $obj) -- write the objection first"
    exit 1; }

# --- 3: the grounds are citations, not gestures ------------------------------
# Code anchors in the reading guides' form, path/file.c:symbol.
anchors=$(grep -oE '[A-Za-z0-9_./-]*[A-Za-z0-9_]\.[ch]:[A-Za-z_][A-Za-z0-9_]*' DISCOURSE.md | sort -u)
na=$(printf '%s\n' "$anchors" | grep -c .)
[ "$na" -ge 4 ] || {
    echo "FAIL: only $na distinct file:symbol citation(s) -- cite where you looked, as src/.../file.c:symbol (need >= 4)"
    exit 1; }

# The rule under test is written down somewhere; a claim about it that cites no
# page has not said which rule it means.
docs=$(grep -oE '[A-Za-z0-9_./-]*[A-Za-z0-9_]\.md' DISCOURSE.md | sort -u)
nd=$(printf '%s\n' "$docs" | grep -c .)
[ "$nd" -ge 1 ] || {
    echo "FAIL: no page cited -- name the document that states the rule you are testing"; exit 1; }

# --- 4: the discussion concluded something -----------------------------------
# A revision identical to the claim means the objection changed nothing, which
# is either a weak objection or one that was not meant. Either way it is not a
# discussion, and this is the one part of "did you think about it" a script can
# reach: compare the two bodies with whitespace and case normalised away.
# index(), not a dynamic regex: awk warns "escape sequence \\. treated as
# plain ." when a string carrying a backslash is used as one, and a driver that
# prints warnings teaches the reader to ignore warnings.
sect_body() {
    awk -v want="$1" '
        index($0, want) == 1 { inb = 1; next }
        /^## / { inb = 0 }
        inb { printf "%s ", $0 }
    ' DISCOURSE.md | tr 'A-Z' 'a-z' | tr -s ' \t' ' ' | sed 's/^ //; s/ $//'
}
claim=$(sect_body '## 1. Claim')
revision=$(sect_body '## 5. Revision')
[ -n "$claim" ] || { echo "FAIL: the Claim section is empty"; exit 1; }
[ -n "$revision" ] || { echo "FAIL: the Revision section is empty"; exit 1; }
[ "$claim" != "$revision" ] || {
    echo "FAIL: the Revision repeats the Claim word for word -- say what the objection changed, or why it survives"
    exit 1; }

# --- 5: the resolution gate, where the tree is reachable ---------------------
root=$(cd ../../.. 2>/dev/null && pwd)
if [ -n "$root" ] && [ -d "$root/src" ]; then
    for a in $anchors; do
        f=${a%%:*}; s=${a##*:}
        case "$f" in
            */*) target="$root/$f" ;;
            *)   target=$(find "$root/src" "$root/include" -name "$f" 2>/dev/null | head -1) ;;
        esac
        [ -n "$target" ] && [ -f "$target" ] || {
            echo "FAIL: citation $a names a file that does not exist in this tree"; exit 1; }
        grep -q "$s" "$target" || {
            echo "FAIL: citation $a: '$s' is not in $f -- a citation of the right SHAPE that resolves to nothing is exactly what this task is about"
            exit 1; }
    done
    # A bare page name may be a root file (CLAUDE.md, README.md) or a docs/
    # page; try both before calling it unresolved. Resolving only to docs/ was
    # this grader's own fluent-but-wrong citation, caught by its first use.
    for d in $docs; do
        dt=""
        for cand in "$root/$d" "$root/docs/$d"; do
            [ -f "$cand" ] && { dt="$cand"; break; }
        done
        [ -n "$dt" ] || {
            echo "FAIL: cited page $d does not exist in this tree (looked in the root and in docs/)"
            exit 1; }
    done
    echo "PASS: five moves, objection before reply, $na citations and $nd page(s) resolved, and the claim moved"
else
    echo "PASS: five moves, objection before reply, $na citations and $nd page(s) (form only -- src/ not reachable from here, so nothing was resolved), and the claim moved"
fi
exit 0
