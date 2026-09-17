#!/bin/sh
# smoke lint: docs/BIBLIOGRAPHY.md keeps its evidence (M636).
#
# WHY THIS FILE EXISTS. A bibliography is the easiest page in a repository to
# write without evidence: titles, years and URLs all look equally confident
# whether they were checked or recalled, and nothing downstream fails when one is
# wrong. This project's register is that a claim carries its evidence
# (docs/APPROACH.md), so every entry on that page carries a marker and a date --
# and a marker nobody enforces decays into decoration.
#
# The page's own first edition got this wrong in a way worth keeping: c-faq.com,
# the canonical home of the comp.lang.c FAQ, refused the connection on the morning
# of 2026-09-16 and was recorded as DEAD. It answered 200 the same afternoon. One
# probe separates "down right now" from "gone" not at all -- so this lint checks
# that every entry carries a dated marker, and deliberately does NOT check
# reachability, which no offline check and no single probe can honestly settle.
#
# THE SPLIT. This lint is OFFLINE and runs in every `make smoke`: it checks the
# SHAPE of the evidence -- that every entry has a marker, a date, and a reason to
# be there, and that the counts in the prose are the counts on the page. Whether
# a URL still answers is a NETWORK question and lives in
# scripts/check-bibliography.sh, which a human runs deliberately. A smoke tier
# that needs a route out is a smoke tier that fails on the boards this project
# exists to run on (docs/LOW_MEMORY.md).
#
# UNIVERSE, measured 2026-09-16: 53 entries between the "## 1." heading and the
# "## How to read these" heading -- 18 craft, 16 C, 11 C++, 8 Zig. Bullets
# OUTSIDE that range ("What this is not", "What is deliberately absent") are
# prose, not entries, and are deliberately excluded: 8 of them, and counting them
# is how the first draft of this lint reported 61.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
G=/usr/bin/grep
[ -x "$G" ] || G=grep
tmp=$(smoke_tmp)
cd "$SMOKE_ROOT" || exit 1

DOC=docs/BIBLIOGRAPHY.md
BODY="$tmp/body.md"

if [ ! -f "$DOC" ]; then
    t_fail "$DOC is missing"
    t_skip "the page is gone; nothing below can be checked"
fi

# The entry range, extracted once. Every check below reads THIS, not the file.
awk '/^## 1\. /{inb=1} /^## How to read these/{inb=0} inb' "$DOC" > "$BODY"
entries=$("$G" -c '^- \*\*' "$BODY" 2>/dev/null || echo 0)

# ---- 1: the extraction is not vacuous -----------------------------------
# Floored at the exact count on the day it was written. A range that collapses
# to nothing -- a renamed heading, a reordered page -- makes checks 2-5 pass on
# an empty file while printing OK, which is the failure this floor exists for.
prose=$("$G" -c '^- \*\*' "$DOC")
if [ "$entries" -ge 53 ] && [ "$prose" -gt "$entries" ]; then
    t_ok "$entries entries extracted from the entry range ($prose bullets on the whole page)"
else
    t_fail "extracted $entries entries (want >= 53) from a page with $prose bullets.
If the headings '## 1. ' or '## How to read these' were renamed, this range is
empty and every check below is vacuous. Fix the extraction, not the floor."
fi

# ---- 2: every entry says why it is there --------------------------------
# A bibliography without this line is a list of links. The page's whole claim is
# that each entry answers a question jichi's own docs decline to answer.
missing=$(awk '/^- \*\*/{if(pend!="") print NR": "substr(pend,1,60); pend=$0; next}
               /^  Read it for:/{pend=""}
               END{if(pend!="") print "EOF: "substr(pend,1,60)}' "$BODY")
if [ -z "$missing" ]; then
    t_ok "all $entries entries carry a 'Read it for:' line"
else
    t_fail "entries with no 'Read it for:' line:
$(printf '%s\n' "$missing" | sed 's/^/    /')
An entry that cannot say what it is for belongs in someone else's bibliography."
fi

# ---- 3: every entry carries a dated verification marker -----------------
# [read YYYY-MM-DD] | [probed YYYY-MM-DD: HTTP nnn] | [ISBN verified YYYY-MM-DD]
# | [DOI ...]. A marker without a date is a claim without a shelf life.
unmarked=$(awk '
  /^- \*\*/{ if(cur!="" && !seen) print cl": "substr(cur,1,60); cur=$0; cl=NR; seen=0 }
  /\[read [0-9]{4}-[0-9]{2}-[0-9]{2}\]|\[probed [0-9]{4}-[0-9]{2}-[0-9]{2}: HTTP [0-9]+\]|\[ISBN verified [0-9]{4}-[0-9]{2}-[0-9]{2}\]|\[DOI /{ if(cur!="") seen=1 }
  END{ if(cur!="" && !seen) print cl": "substr(cur,1,60) }' "$BODY")
if [ -z "$unmarked" ]; then
    t_ok "all $entries entries carry a dated [read]/[probed]/[ISBN verified]/[DOI] marker"
else
    t_fail "entries with no dated verification marker:
$(printf '%s\n' "$unmarked" | sed 's/^/    /')
docs/BIBLIOGRAPHY.md's own table promises one per entry. An unmarked entry is
indistinguishable from a recalled one."
fi

# ---- 4: every cited URL is marked, and every ISBN is marked -------------
# Counted rather than paired line-by-line, because an entry may cite two URLs
# (the Zig reference is deliberately cited floating AND pinned). The assertion
# is that no citation form outruns its marker form.
nurl=$("$G" -o '<https://[^>]*>' "$BODY" | "$G" -c .)
nurlmark=$("$G" -oE '\[read [0-9]{4}-[0-9]{2}-[0-9]{2}\]|\[probed [0-9]{4}-[0-9]{2}-[0-9]{2}: HTTP [0-9]+\]' "$BODY" | "$G" -c .)
nisbn=$("$G" -oE 'ISBN 97[0-9]' "$BODY" | "$G" -c .)
nisbnmark=$("$G" -c '\[ISBN verified [0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\]' "$BODY")
if [ "$nurl" -ge 30 ] && [ "$nurlmark" -ge "$nurl" ] &&
   [ "$nisbn" -ge 25 ] && [ "$nisbnmark" -ge "$nisbn" ]; then
    t_ok "$nurl URLs with $nurlmark markers, $nisbn ISBNs with $nisbnmark markers"
else
    t_fail "citations outrun their evidence: URLs=$nurl marked=$nurlmark \
(want marked >= URLs, URLs >= 30); ISBNs=$nisbn marked=$nisbnmark (want marked \
>= ISBNs, ISBNs >= 25).
Every URL on that page was fetched or probed on a named date, and every ISBN was
resolved against a catalogue. A citation added without one breaks that promise
for the whole page, not just its own line."
fi

# ---- 5: the prose counts are the page's real counts ---------------------
# M259: eight consecutive milestones incremented a hand-maintained count instead
# of recounting. The page states its total and a per-section breakdown; this
# recomputes every one of them.
#
# sec() takes a section NUMBER and counts entries between that heading and the
# next numbered one. The first spelling hard-coded the terminators per section
# ("/^## [34]\./" and so on) and the LAST section had none at all -- so when Rust
# arrived as section 5 (M636c), zig silently reported 8+9=17 while the sum still
# equalled the total. A count that absorbs a new section without noticing is the
# same drift M259 is about, wearing a different hat; adding a section must now
# also extend the list below, deliberately.
sec() {
    awk -v n="$1" '$0 ~ "^## "n"\\. " {s=1; next}
                   /^## [0-9]+\./        {s=0}
                   s && /^- \*\*/        {c++}
                   END                   {print c+0}' "$BODY"
}
craft=$(sec 1); cc=$(sec 2); cpp=$(sec 3); zig=$(sec 4); rust=$(sec 5)
# Newlines collapsed first: the sentence wraps in the source, and a line-oriented
# grep found nothing and reported "<no count sentence found>" for a sentence that
# was there and correct. \s is not ERE either -- that was the second half of the
# same mistake.
claim=$(tr '\n' ' ' < "$DOC" | tr -s ' ' \
        | "$G" -oE '\*\*[0-9]+ entries\*\* below \([0-9]+ craft, [0-9]+ C, [0-9]+ C\+\+, [0-9]+ Zig, [0-9]+ Rust\)')
want="**$entries entries** below ($craft craft, $cc C, $cpp C++, $zig Zig, $rust Rust)"
sum=$((craft + cc + cpp + zig + rust))
if [ "$claim" = "$want" ] && [ "$sum" -eq "$entries" ]; then
    t_ok "the stated counts match the page: $entries = $craft + $cc + $cpp + $zig + $rust"
else
    t_fail "the counts in the prose are not the counts on the page.
  states:   ${claim:-<no count sentence found>}
  counted:  $want   (sections sum to $sum, entries=$entries)
Recount; do not increment. That distinction is M259. If a SECTION was added, the
sec() list above needs its number too -- a sum that still matches while a section
is uncounted is how this check was wrong on its first run after Rust landed."
fi

# ---- 6: the page is reachable from the documentation index --------------
# A page nothing links to is a page nobody finds. docs/README.md is the routing
# table the whole tree is indexed by.
if "$G" -q 'BIBLIOGRAPHY\.md' docs/README.md &&
   "$G" -q 'BIBLIOGRAPHY\.md' docs/CURRICULUM.md; then
    t_ok "BIBLIOGRAPHY.md is linked from both docs/README.md and docs/CURRICULUM.md"
else
    t_fail "BIBLIOGRAPHY.md is not linked from docs/README.md and docs/CURRICULUM.md.
The curriculum is what sends a reader outward; an unlinked bibliography serves
nobody."
fi

# ---- 7: the counts quoted OFF the page match the page --------------------
# This gap was live for three commits. docs/README.md's index line and
# CURRICULUM.md's routing paragraph each quote the entry count, and nothing
# checked them: both still said "53 checked entries" while the page had grown to
# 55, then 64. Check 5 audits the page against itself, which is precisely the
# single-route mistake TEST_INTEGRITY warns about -- and the number a READER
# meets first is the index line, which was the one nobody was counting.
#
# CHANGELOG.md is deliberately OUT of this universe: a changelog entry describes
# what shipped at a moment and is allowed to freeze its numbers.
bad=""
for f in docs/README.md docs/CURRICULUM.md; do
    quoted=$("$G" -oE '[0-9]+ (checked )?entries' "$f" | "$G" -oE '^[0-9]+' | sort -u)
    if [ -z "$quoted" ]; then
        bad="$bad $f(no-count-found)"
        continue
    fi
    for q in $quoted; do
        [ "$q" = "$entries" ] || bad="$bad $f(says-$q)"
    done
done
if [ -z "$bad" ]; then
    t_ok "docs/README.md and CURRICULUM.md both quote $entries entries"
else
    t_fail "the entry count quoted away from the page is stale:$bad (page has $entries).
A reader meets the index line before the page itself. Update every place that
states the number -- or state it once and route the others through it."
fi

# ---- 8 (M650): the design tutorials must route the reader HERE ----------
# WHY. Six tutorials each ended with a list of works under the instruction
# "search these; prefer primary sources" -- naming Evans, Vernon, Cockburn,
# Nygard, Brown, Wirth and Knuth with no publisher, year, ISBN, DOI or URL
# between them, in a repository whose whole register is that a claim carries its
# evidence. The operator found it by reading one tutorial's last paragraph.
#
# Those works are now entries above, and each tutorial points at them. This check
# holds the ROUTE, which is the part that silently rots: M510's lesson is that a
# guide nobody is routed to is a guide nobody reads, and the fix there was a
# link, checked.
#
# WHAT THIS DOES NOT DO, stated rather than implied. It cannot tell that a NEWLY
# added book carries a citation -- that needs a classifier over prose, and the
# M645 measurement against this tree says such a gate fires overwhelmingly on
# correct history. This checks reachability only. CHOOSING_A_MODEL.md is
# deliberately NOT in the universe: its outward list names concepts (scaling
# laws, quantization, benchmark contamination) and no works, so it has nothing
# to cite and telling a reader to search a concept is correct.
b8=""
n8=0
for f in docs/USE_CASE_TUTORIAL.md docs/UML_TUTORIAL.md \
         docs/DOMAIN_MODELLING_TUTORIAL.md docs/ARCHITECTURE_TUTORIAL.md \
         docs/PSEUDOCODE_TUTORIAL.md docs/TESTING_TUTORIAL.md; do
    if [ ! -f "$f" ]; then
        b8="$b8 $f(missing)"
        continue
    fi
    n8=$((n8 + 1))
    "$G" -q 'BIBLIOGRAPHY.md' "$f" || b8="$b8 $f"
done
if [ "$n8" -lt 6 ]; then
    t_fail "only $n8 of the 6 design tutorials were found ($b8) -- a renamed page \
makes this check read nothing. Fix the list, not the floor."
elif [ -z "$b8" ]; then
    t_ok "all $n8 design tutorials route the reader to BIBLIOGRAPHY.md"
else
    t_fail "design tutorial(s) that name outside reading but do not link the \
bibliography:$b8 -- a work named without a citation is the defect this page \
exists against, and a citation nobody is routed to is the next one."
fi

t_done
