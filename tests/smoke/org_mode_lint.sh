#!/bin/sh
# smoke lint: every org claim in docs/ORG_MODE.md is measured, not remembered.
#
# The page is the first thing in this repository to assert anything about org
# at all, and org's defaults are exactly the kind of fact that is easy to state
# confidently and wrongly -- `NEXT` looks like a built-in state and is not,
# `org-capture-templates` looks bound and is not, and a diary entry inserted by
# hand lands ABOVE the day heading rather than under it. That last one I got
# wrong while writing the page, which is why this driver exists.
#
# THE ELISP MEASURES; THIS SHELL COMPARES. tests/elisp/org-checks.el holds no
# expected values at all: it opens fixtures extracted from the page and prints
# KEY=VALUE. The expectations are scraped from the page's own VISIBLE PROSE, so
# a wrong sentence fails even when the code behind it is right. Hiding the
# expected values in a marker comment would let the sentence a reader actually
# reads drift while this stayed green.
#
# Markers, mirroring project_records_lint.sh:
#   <!-- file: NAME -->   before a ```org fence -- extract it as fixture NAME
#   <!-- fragment -->     before a ```org fence -- illustrative, not extracted
# An unclassified ```org fence is a failure.
#
# WITHOUT EMACS this driver does not skip: t_skip is all-or-nothing and would
# throw away the structural checks too, which need nothing but the page. It
# emits a DYNAMIC PLAN instead -- the four structural checks alone. t_done and
# run.sh both require only that the plan equals the number of checks emitted,
# so this is legal; it is a new idiom in the tier and is stated here as one.
. "$(dirname "$0")/_smoke.sh"

DOC="$SMOKE_ROOT/docs/ORG_MODE.md"
EL="$SMOKE_ROOT/tests/elisp/org-checks.el"
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

have_emacs=no
if command -v emacs >/dev/null 2>&1 && [ -f "$EL" ]; then
    have_emacs=yes
fi

if [ "$have_emacs" = yes ]; then
    t_plan 12
else
    t_plan 4
    echo "# no emacs (or no org-checks.el): structural checks only"
fi

if [ ! -f "$DOC" ]; then
    t_fail "docs/ORG_MODE.md is missing"
    t_fail "(no page: fences not classified)"
    t_fail "(no page: claims not scraped)"
    t_fail "(no page: links not resolved)"
    if [ "$have_emacs" = yes ]; then
        for i in 5 6 7 8 9 10 11 12; do
            t_fail "(no page: measurement $i not run)"
        done
    fi
    t_done
fi

# --- extract the fixtures ----------------------------------------------------
awk -v dir="$ws" '
/^<!-- file: /   { pend = "file"; pf = $3; next }
/^<!-- fragment/ { pend = "frag"; next }
/^```/ {
    if (inf) { close(out); inf = 0; pend = ""; next }
    if (pend == "file") { out = dir "/" pf; inf = 1; pend = ""; next }
    pend = ""; next
}
inf { print > out }
' "$DOC"

# --- the claims, scraped from the visible prose ------------------------------
claim() { sed -n "s/.*$1.*/\1/p" "$DOC" | head -1; }

C_DEFAULT=$(claim '\*\*default states: \([^*]*\)\*\*')
C_STATES=$(claim '\*\*states: \([^*]*\)\*\*')
C_NOTDONE=$(claim '\*\*unfinished: \([^*]*\)\*\*')
C_DONE=$(claim '\*\*finished: \([^*]*\)\*\*')
# The page separates the cycle with a multi-byte arrow for readability. The
# tier pins LC_ALL=C, so `.` matches one BYTE and ` . ` silently matched
# nothing -- the claim reached the comparison still arrowed. Match the whole
# run of non-space bytes between the spaces instead, which is locale-proof and
# does not require a UTF-8 literal in a POSIX-sh driver.
C_CYCLE=$(claim '\*\*cycle: \([^*]*\)\*\*' | sed 's/ [^ ][^ ]* />/g')
C_AGENDA=$(claim '\*\*\([0-9][0-9]*\) agenda entries\*\*')
C_TANGLE=$(claim '\*\*tangles to `\([^`]*\)`\*\*')
C_BYTES=$(claim '\*\*\([0-9][0-9]*\) bytes\*\*')
C_DAY=$(claim '\*\*\(20[0-9][0-9]-[0-9][0-9]-[0-9][0-9] [A-Za-z][A-Za-z]*\)\*\*')
C_PRIO=$(claim '\*\*priorities \([^*]*\)\*\*')

# --- 1: extraction + claim floor (the denominator) ---------------------------
nfix=$(ls "$ws" 2>/dev/null | grep -c '\.org$' || true)
nclaim=0
for c in "$C_DEFAULT" "$C_STATES" "$C_NOTDONE" "$C_DONE" "$C_CYCLE" \
         "$C_AGENDA" "$C_TANGLE" "$C_BYTES" "$C_DAY" "$C_PRIO"; do
    [ -n "$c" ] && nclaim=$((nclaim + 1))
done
if [ "$nfix" -ge 2 ] && [ "$nclaim" -eq 10 ]; then
    t_ok "extracted $nfix org fixtures and scraped all $nclaim prose claims"
else
    t_fail "extraction/scrape is incomplete ($nfix fixtures, $nclaim/10 claims) -- a claim was reworded out of reach"
fi

# --- 2: every ```org fence is classified -------------------------------------
# Counted by ADJACENCY, not by totalling markers: the page also fences elisp,
# whose fragments would otherwise pad the total and let an unmarked org block
# hide behind them. Only the line immediately above a ```org fence counts.
set -- $(awk '
/^```org$/ { n++; if (prev ~ /^<!-- file: / || prev ~ /^<!-- fragment/) m++ }
{ prev = $0 }
END { print n+0, m+0 }' "$DOC")
norg=$1; nmark=$2
nfile=$(grep -c '^<!-- file: ' "$DOC" || true)
if [ "$norg" -eq "$nmark" ] && [ "$norg" -gt 0 ]; then
    t_ok "all $norg org blocks are classified ($nfile of them extracted as fixtures)"
else
    t_fail "$norg org blocks, $nmark carry a marker -- an unmeasured example exists"
fi

# --- 3: the page states its own honest limits --------------------------------
# The nil agenda is the single most common "org is broken" report, and the page
# is only trustworthy if it says so before the reader hits it.
if grep -q 'org-agenda-files defaults to nil' "$DOC" \
    && grep -q 'org-capture-templates' "$DOC" \
    && grep -q 'PROJECT_RECORDS.md' "$DOC"; then
    t_ok "the page states the nil agenda, the unbound templates, and its prerequisite"
else
    t_fail "the page dropped one of: nil agenda / unbound capture templates / the practice page"
fi

# --- 4: every relative doc link resolves -------------------------------------
dead=""
for l in $(grep -o '](\([A-Za-z0-9_./-]*\.md\))' "$DOC" | sed 's/^](//; s/)$//' | sort -u); do
    [ -f "$SMOKE_ROOT/docs/$l" ] || dead="$dead $l"
done
if [ -z "$dead" ]; then
    t_ok "every relative documentation link resolves"
else
    t_fail "dead links:$dead"
fi

[ "$have_emacs" = yes ] || t_done

# --- measure ------------------------------------------------------------------
# < /dev/null is mandatory: an interactive prompt in batch mode (the agenda has
# several) would otherwise wait forever on the runner's stdin.
with_deadline 60 env LC_ALL=C emacs -Q --batch --load "$EL" "$ws" \
    > "$tmp/m" 2> "$tmp/err" < /dev/null || true
mval() { sed -n "s/^$1=//p" "$tmp/m" | head -1; }

# --- 5: the measurement ran ---------------------------------------------------
nkeys=$(grep -c '^[A-Z_][A-Z_]*=' "$tmp/m" || true)
nerr=$(grep -c '=ERROR:' "$tmp/m" || true)
if [ "$nkeys" -ge 18 ] && [ "$nerr" -eq 0 ]; then
    t_ok "org-checks.el measured $nkeys facts with no probe errors"
else
    t_fail "measurement failed ($nkeys keys, $nerr errors): $(grep '=ERROR:' "$tmp/m" | head -1)$(head -2 "$tmp/err")"
fi

# --- 6: the fixtures parse ----------------------------------------------------
# The denominator for everything below: org-lint clean, and the src-block count
# the page shows equals the count org found.
nfence=$(grep -c '^#+begin_src' "$ws/literate.org" 2>/dev/null || true)
if [ "$(mval LINT)" = "0" ] && [ "$(mval SRCBLOCKS)" = "$nfence" ]; then
    t_ok "the example file is org-lint clean and its $nfence src block(s) are seen"
else
    t_fail "fixture problem: org-lint=$(mval LINT), src blocks $(mval SRCBLOCKS) vs $nfence in the page"
fi

# --- 7: unconfigured defaults, as a paired presence/absence -------------------
# Both halves matter: that TODO/DONE ARE the defaults, and that the states the
# page teaches are NOT. A one-sided check passes on an Emacs with someone
# else's config leaking in.
if [ "$(mval DEFAULT_KEYWORDS)" = "$C_DEFAULT" ] \
    && [ "$(mval DEFAULT_AGENDA)" = "nil" ] \
    && [ "$(mval PRIO)" = "$C_PRIO" ]; then
    t_ok "unconfigured Emacs gives '$C_DEFAULT', a nil agenda, and priorities $C_PRIO"
else
    t_fail "defaults drifted: keywords=$(mval DEFAULT_KEYWORDS) agenda=$(mval DEFAULT_AGENDA) prio=$(mval PRIO)"
fi

# --- 8: the file's own vocabulary ---------------------------------------------
if [ "$(mval STATES)" = "$C_STATES" ]; then
    t_ok "#+TODO: under emacs -Q yields exactly '$C_STATES'"
else
    t_fail "states: page says '$C_STATES', org says '$(mval STATES)'"
fi

# --- 9: the | split -----------------------------------------------------------
# The one piece of syntax on the page, and the thing the /! query depends on.
if [ "$(mval NOTDONE)" = "$C_NOTDONE" ] && [ "$(mval DONEKW)" = "$C_DONE" ]; then
    t_ok "the | splits '$C_NOTDONE' from '$C_DONE'"
else
    t_fail "| split: page says $C_NOTDONE / $C_DONE, org says $(mval NOTDONE) / $(mval DONEKW)"
fi

# --- 10: the cycle ORDER ------------------------------------------------------
# Order, not membership: a set comparison passes on a shuffled sequence, and
# the order is what the reader's C-c C-t actually walks.
if [ "$(mval CYCLE)" = "$C_CYCLE" ]; then
    t_ok "C-c C-t walks $C_CYCLE"
else
    t_fail "cycle: page says '$C_CYCLE', org walks '$(mval CYCLE)'"
fi

# --- 11: the agenda count, with an empty-week control -------------------------
if [ "$(mval AGENDA_N)" = "$C_AGENDA" ] && [ "$(mval AGENDA_EMPTY)" = "0" ]; then
    t_ok "the pinned week has $C_AGENDA entries and an empty week has none"
else
    t_fail "agenda: page says $C_AGENDA, got $(mval AGENDA_N); empty week got $(mval AGENDA_EMPTY)"
fi

# --- 12: tangle, and the datetree position ------------------------------------
# DATETREE_UNDER is the teeth of the page's warning: the hand-inserted entry
# lands ABOVE the day heading, and only a position check can tell them apart.
if [ "$(mval TANGLE)" = "$C_TANGLE" ] && [ "$(mval TANGLE_BYTES)" = "$C_BYTES" ] \
    && [ "$(mval DATETREE_DAY)" = "$C_DAY" ] && [ "$(mval DATETREE_UNDER)" = "yes" ]; then
    t_ok "tangles to $C_TANGLE ($C_BYTES bytes); capture files under '$C_DAY'"
else
    t_fail "tangle/datetree: $(mval TANGLE)/$(mval TANGLE_BYTES) vs $C_TANGLE/$C_BYTES; day='$(mval DATETREE_DAY)' under=$(mval DATETREE_UNDER)"
fi

t_done
