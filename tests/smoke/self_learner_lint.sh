#!/bin/sh
# smoke lint: the pages and sections a self-learner needs exist, and the words
# this project uses are defined somewhere a reader can find (M499).
#
# WHAT THIS EXISTS FOR. M392's documentation review put four reviewer personas
# across thirty pages and produced a 21-item register of what the docs OWED a
# learner working alone. Five of those items are structural -- a page that does
# not exist, or a section missing from a reference page -- and structural gaps
# have a property worth exploiting: they are checkable. The rest (is the prose
# clear? is the example the right one?) are not, and this lint does not pretend
# otherwise; it owns exactly the half a machine can hold.
#
# The specific failure it prevents is REGRESSION BY TIDYING. A "your first hour"
# section inside a reference page looks like an intrusion to anyone editing that
# page for other reasons -- it is the least defended kind of documentation, and
# the most valuable to the reader who knows least. Deleting one is a two-line
# diff nobody would question. Now it fails the tier.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- the three new pages exist and carry their load-bearing
#                   headings; the five tutorial sections exist by heading in
#                   their five reference pages; every word on the REQUIRED list
#                   is defined in VOCABULARY.md; GLOSSARY.md still carries the
#                   sign in front of its name trap.
#   NOT checked  -- whether any of it is any GOOD. A section can be present and
#                   useless. That judgement is DOC_REVIEW.md's rubric and a
#                   person's afternoon; this is the floor under it.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
R="$SMOKE_ROOT"

# ---- 1. the matcher works, and the corpus is there (the floor) -------------
# Two empty sets agree perfectly: prove the heading matcher flags a planted
# absence before trusting it on the real pages.
mkdir -p "$tmp/self"
printf '# p\n\n## Your first hour\n\ntext\n' > "$tmp/self/good.md"
printf '# p\n\n## Something else\n\ntext\n' > "$tmp/self/bad.md"
_hits=$(grep -l '^## Your first hour$' "$tmp/self"/*.md 2>/dev/null | wc -l \
    | tr -d '[:space:]')
_pages=$(ls "$R"/docs/*.md 2>/dev/null | wc -l | tr -d '[:space:]')
if [ "$_hits" -eq 1 ] && [ "$_pages" -ge 100 ]; then
    t_ok "heading matcher flags a planted absence; $_pages docs pages in scope"
else
    t_fail "floor tripped: matcher hit $_hits of 2 planted files (expect 1), \
$_pages pages found (expect >= 100) -- fix the extraction, not the floor"
fi

# ---- 2. the three pages M392 said did not exist ---------------------------
: > "$tmp/missing"
# M502 adds TEACHING.md: the teacher's progression, whose whole claim is that a
# teacher walks the steps first. It is in the same class as the other three --
# a page a newcomer needs and nothing else supplies.
for _p in TOOL_DECISIONS STATE VOCABULARY TEACHING; do
    [ -f "$R/docs/$_p.md" ] || echo "docs/$_p.md" >> "$tmp/missing"
done
_m=$(wc -l < "$tmp/missing" 2>/dev/null | tr -d '[:space:]'); [ -n "$_m" ] || _m=0
if [ "$_m" -eq 0 ]; then
    t_ok "the four pages a newcomer needs, and nothing else supplies, exist"
else
    t_fail "$_m page(s) a newcomer depends on are missing again:
$(cat "$tmp/missing")"
fi

# ---- 3. THE FIVE TUTORIAL SECTIONS ----------------------------------------
# page|heading -- each shape was specified by the review, and the heading is the
# contract. A renamed heading is a fine change; renaming it HERE too is the
# one-line cost of keeping this check honest.
SECTIONS="AGENT_MODES.md|## Your first hour
AUTONOMY.md|## Your first bounded run
SKILLS.md|## Your first skill
MODELS.md|## The smallest config that works
DOCTOR.md|## A check failed — now what"
: > "$tmp/gone"
echo "$SECTIONS" | while IFS='|' read -r _pg _hd; do
    [ -n "$_pg" ] || continue
    grep -Fq "$_hd" "$R/docs/$_pg" 2>/dev/null || echo "docs/$_pg: $_hd" >> "$tmp/gone"
done
_g=$(wc -l < "$tmp/gone" 2>/dev/null | tr -d '[:space:]'); [ -n "$_g" ] || _g=0
if [ "$_g" -eq 0 ]; then
    t_ok "all five reference pages carry their tutorial-shaped section"
else
    t_fail "$_g tutorial section(s) gone from a reference page -- the reader who \
knows least is the one who loses them:
$(cat "$tmp/gone")"
fi

# ---- 4. the load-bearing words are defined -------------------------------
# The list is the M392 register's own item 15, verbatim, plus the words the smoke
# tier itself forces on any reader of the tests. A word counts as defined only
# when VOCABULARY.md carries it as a bolded term OPENING a bullet -- the page's
# one format -- so a passing mention in prose cannot satisfy this.
#
# Newline-separated and read whole, because two entries are multi-word ("two-sided
# proof"); the first version split on spaces and needed a `\b` fallback for the
# tail word, which posix_utils_lint check 14 correctly rejected: GNU word
# boundaries match NOTHING on a BSD grep, so that fallback would have silently
# never fired on three of the four kernels this tier runs on.
WORDS="role
posture
fence
envelope
green
chokepoint
fix-forward
quantized
TAP
lint
immutable
invariant
turn
token
verifier
checkpoint
rollback
baseline
two-sided proof
floor
seam
register
drift"
: > "$tmp/undef"
printf '%s\n' "$WORDS" | while IFS= read -r _w; do
    [ -n "$_w" ] || continue
    grep -q "^- \*\*$_w\*\*" "$R/docs/VOCABULARY.md" 2>/dev/null || \
        echo "$_w" >> "$tmp/undef"
done
_u=$(wc -l < "$tmp/undef" 2>/dev/null | tr -d '[:space:]'); [ -n "$_u" ] || _u=0
_need=$(printf '%s\n' "$WORDS" | grep -c .)
_defs=$(grep -c '^- \*\*' "$R/docs/VOCABULARY.md" 2>/dev/null)
if [ "$_u" -eq 0 ] && [ "${_defs:-0}" -ge 40 ]; then
    t_ok "VOCABULARY.md defines all $_need required words ($_defs terms total)"
else
    t_fail "$_u of $_need required word(s) undefined, $_defs terms parsed \
(expect >= 40):
$(head -n 8 "$tmp/undef")"
fi

# ---- 5. the sign in front of the name trap -------------------------------
# docs/GLOSSARY.md documents a FEATURE (the file you write for your project's
# terms). A learner looking for "what does posture mean" lands there by name and
# finds a config page -- the same shape as docs/DOCS.md, which documents
# search_docs. The pointer is the fix; losing it restores the trap.
if grep -q 'VOCABULARY.md' "$R/docs/GLOSSARY.md" 2>/dev/null &&
   grep -qi 'trap' "$R/docs/GLOSSARY.md" 2>/dev/null; then
    t_ok "GLOSSARY.md still points a misdirected reader at VOCABULARY.md"
else
    t_fail "GLOSSARY.md no longer warns that it documents the FEATURE, not the \
project's vocabulary -- the name trap is back"
fi

# ---- 6. and the pages are findable ---------------------------------------
# docs_index_lint proves every page is in the index; this proves the three that
# exist FOR the newcomer are reachable from a page a newcomer actually opens,
# which the index alone does not (M297: a feature nobody can discover was not
# shipped).
: > "$tmp/orphan"
for _p in TOOL_DECISIONS STATE VOCABULARY TEACHING; do
    _n=$(grep -rl "$_p.md" "$R"/docs/*.md "$R/README.md" 2>/dev/null \
        | grep -v "docs/$_p.md" | grep -vc '^$')
    [ "${_n:-0}" -ge 2 ] || echo "$_p.md linked from $_n page(s)" >> "$tmp/orphan"
done
_o=$(wc -l < "$tmp/orphan" 2>/dev/null | tr -d '[:space:]'); [ -n "$_o" ] || _o=0
if [ "$_o" -eq 0 ]; then
    t_ok "each new page is linked from at least two others, not only the index"
else
    t_fail "$_o new page(s) reachable from too few places:
$(cat "$tmp/orphan")"
fi

t_done
