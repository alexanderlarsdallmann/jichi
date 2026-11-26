#!/bin/sh
# smoke lint: the release texts a reader acts on say only what the tree shows (2026-11-25, the operator: "The next
# public release has to be correct").
#
# THE DEFECT CLASS. Every public cut so far shipped, or nearly shipped, a release text that claimed more than the
# record. "This tree reads 0.12.0" and "0.12.0, the latest release" stood in the tree after 0.13.0 was cut (found
# 2026-11-05 at cut time). "Every platform row was retested" stood in the v0.14.1 commit and tag (published; the
# correction is dated 2026-11-16). On 2026-11-25, before the 0.15.0 cut, three were found:
# - PROJECT_TIMELINE.md said "the v0.15.0 public cut and tag are pending";
# - the CHANGELOG [0.15.0] heading named "the gate's redfirst stage", which no 0.15.0 entry describes (that stage
#   is 0.14.0's M1157);
# - README's release paragraph named verbs that do not exist and "two essay pages" for four.
# Each was written from intent or memory instead of from the entries, and each was found by a person reading at
# cut time. This lint makes the reading mechanical.
#
# Universe, stated:
#   (a) CHANGELOG.md's NEWEST RELEASED section: from the first `## [X.Y.Z]` heading (so [Unreleased] is skipped)
#       down to the next `## [` line, with its `### ` sub-headings.
#   (b) README.md's release paragraph: from the line starting `**Releases.**` to the next blank line.
#   (c) the shipped Markdown pages: `git ls-files '*.md'` (find, in a tree without git), minus the dated records
#       (CHANGELOG.md, docs/ROADMAP.md, docs/DEFERRED.md, docs/DECISIONS.md, docs/ANECDOTES.md,
#       docs/MILESTONE_HIGHLIGHTS.md) and the dated or translated directories (docs/internal, docs/analysis,
#       docs/plans, docs/case-studies, docs/dialogues, docs/reading, docs/proposals, docs/i18n).
#
# Checks (t_plan 7):
#   C1: the newest release heading reads `## [X.Y.Z] — <date> — item; item; ...`, and each item appears in one of
#       that section's `### ` lines (compared after lower-casing, dropping backticks, and reading the dash forms
#       `—`, `–` and `--` as `-`). So a heading can name only what the section has an entry for. The 0.15.0
#       heading before this commit fails, and the failure names the item.
#   C2: that section's version is JC_VERSION (include/jc_version.h): the version the binary prints has its entry.
#   C3: README's "public release is **X**" is JC_VERSION.
#   C4: README's release paragraph names no milestone, proposal, relay or batch id (M1223, P3-P38, R1-R5b, H12,
#       "rows v4"). It points to CHANGELOG.md instead of restating it: a second copy drifts from the first.
#   C5: no undated present-tense release phrase in (c): "<cut/tag/release> is|are pending", "the latest release",
#       "this tree reads", "not yet released|published|tagged". A line that carries a date (YYYY-MM-DD) is a dated
#       record and passes. The named exemptions below are exact path|text pairs, so an exemption cannot hide a
#       new line.
#   C6: the floors: (c) read >= 576 pages (today's exact count), and (a) has >= 1 item and >= 1 `### ` line.
#   C7: the instruments fire. C1's matcher refuses an item absent from sample sub-headings and accepts a present
#       one. C5's pattern hits an undated sample line and passes a dated one.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
R="$SMOKE_ROOT"
tmp=$(smoke_tmp)
LC_ALL=C
export LC_ALL

# norm: lower-case, no backticks, the three dash forms as '-', single spaces.
norm() {
    sed -e 's/`//g' -e 's/—/-/g' -e 's/–/-/g' -e 's/--/-/g' -e 's/[[:space:]][[:space:]]*/ /g' \
        -e 's/^ //' -e 's/ $//' | tr 'A-Z' 'a-z'
}

# item_in_heads ITEM FILE: 0 when the normalised ITEM is a substring of a line of FILE (already normalised).
item_in_heads() {
    _i=$(printf '%s\n' "$1" | norm)
    [ -n "$_i" ] || return 1
    awk -v i="$_i" 'index($0, i) { f = 1 } END { exit f ? 0 : 1 }' "$2"
}

JCV=$(sed -n 's/^#define JC_VERSION "\([^"]*\)".*/\1/p' "$R/include/jc_version.h")

# --- (a) the newest released section --------------------------------------------------------------------------
# (a release heading is `## [` then a digit; written with substr() because smoke_lint check 4 forbids the two-
# bracket sequence a `## \[` regex followed by a bracket expression would hold)
awk 'function relhead(s) { return substr(s, 1, 4) == "## [" && substr(s, 5, 1) ~ /^[0-9]$/ }
     relhead($0) { if (f) exit; f = 1 }
     f && substr($0, 1, 4) == "## [" && !relhead($0) { exit }
     f' "$R/CHANGELOG.md" > "$tmp/section"
_head=$(head -n 1 "$tmp/section")
_ver=$(printf '%s\n' "$_head" | sed -n 's/^## \[\([0-9][0-9.]*\)\].*/\1/p')
# the subtitle: everything after the SECOND " — " of the heading
_sub=$(printf '%s\n' "$_head" | awk '{
    s = $0
    for (k = 0; k < 2; k++) { p = index(s, " — "); if (!p) { print ""; exit } s = substr(s, p + length(" — ")) }
    print s }')
grep '^### ' "$tmp/section" | norm > "$tmp/heads"
printf '%s\n' "$_sub" | awk '{ n = split($0, a, "; "); for (k = 1; k <= n; k++) if (length(a[k])) print a[k] }' > "$tmp/items"
_nitems=$(grep -c . "$tmp/items")
_nheads=$(grep -c . "$tmp/heads")

# --- C1: each heading item has a sub-heading --------------------------------------------------------------------
: > "$tmp/c1_missing"
while IFS= read -r _it; do
    item_in_heads "$_it" "$tmp/heads" || printf '%s\n' "$_it" >> "$tmp/c1_missing"
done < "$tmp/items"
if [ "$_nitems" -ge 1 ] && [ ! -s "$tmp/c1_missing" ]; then
    t_ok "C1: the [$_ver] heading's $_nitems item(s) each appear in one of the section's $_nheads ### lines"
else
    t_fail "C1: the [$_ver] heading names what no ### line of its section has (items are '; '-separated after the second ' — '): $(tr '\n' '|' < "$tmp/c1_missing")"
fi

# --- C2: the section is the version the binary prints -----------------------------------------------------------
if [ -n "$_ver" ] && [ "$_ver" = "$JCV" ]; then
    t_ok "C2: the newest released section [$_ver] is JC_VERSION ($JCV)"
else
    t_fail "C2: the newest released section is [$_ver], JC_VERSION is $JCV -- the version the binary prints has no entry, or the entry is not the newest"
fi

# --- (b) README's release paragraph -----------------------------------------------------------------------------
awk '/^\*\*Releases\.\*\*/ { f = 1 } f && /^[[:space:]]*$/ { exit } f' "$R/README.md" | tr '\n' ' ' > "$tmp/readme_rel"
_rv=$(sed -n 's/.*public release is \*\*v\{0,1\}\([0-9][0-9.]*\)\*\*.*/\1/p' "$tmp/readme_rel")

# --- C3: README's current public release is JC_VERSION ----------------------------------------------------------
if [ -n "$_rv" ] && [ "$_rv" = "$JCV" ]; then
    t_ok "C3: README's release paragraph names $_rv as the current public release, which is JC_VERSION"
else
    t_fail "C3: README's release paragraph says the current public release is '$_rv', JC_VERSION is $JCV (or the paragraph starting **Releases.** is missing)"
fi

# --- C4: README's release paragraph restates no ids -------------------------------------------------------------
_ids=$(grep -oE '(^|[^A-Za-z0-9])(M[0-9]{3,4}|P[0-9]+|R[0-9]+[a-z]?|H[0-9]+)([^0-9]|$)|rows v[0-9]' "$tmp/readme_rel" \
       | sed 's/^[^A-Za-z]*//; s/[^A-Za-z0-9 ]*$//' | sort -u | tr '\n' ' ')
if [ -s "$tmp/readme_rel" ] && [ -z "$_ids" ]; then
    t_ok "C4: README's release paragraph names no milestone, proposal, relay or batch id (it points to CHANGELOG.md)"
else
    t_fail "C4: README's release paragraph restates the CHANGELOG with ids ($_ids): point to CHANGELOG.md instead, a second copy drifts"
fi

# --- (c) + C5: no undated present-tense release phrase -----------------------------------------------------------
PAT='(cut|tag|release)s?( and (its )?(public )?(cut|tag)s?)? (is|are) pending|the latest release|this tree reads|not yet (released|published|tagged)'
EXEMPT='docs/SCREEN.md|on the latest release, **5.0.2**'
if (cd "$R" && git rev-parse --is-inside-work-tree > /dev/null 2>&1); then
    (cd "$R" && git ls-files '*.md') > "$tmp/pages0"
else
    (cd "$R" && find . -name '*.md' -type f | sed 's#^\./##') > "$tmp/pages0"
fi
grep -vE '^docs/(internal|analysis|plans|case-studies|dialogues|reading|proposals|i18n)/|^(CHANGELOG|docs/ROADMAP|docs/DEFERRED|docs/DECISIONS|docs/ANECDOTES|docs/MILESTONE_HIGHLIGHTS)\.md$' \
    "$tmp/pages0" | sort > "$tmp/pages"
_npages=$(grep -c . "$tmp/pages")
: > "$tmp/c5_hits"
while IFS= read -r _pg; do
    [ -f "$R/$_pg" ] || continue
    grep -nIiE "$PAT" "$R/$_pg" 2>/dev/null | grep -vE '20[0-9]{2}-[0-9]{2}-[0-9]{2}' | while IFS= read -r _ln; do
        _txt=${_ln#*:}
        _ex=0
        printf '%s\n' "$EXEMPT" | while IFS='|' read -r _ep _et; do
            [ "$_ep" = "$_pg" ] && case "$_txt" in *"$_et"*) exit 9 ;; esac
        done
        [ $? -eq 9 ] && _ex=1
        [ "$_ex" -eq 1 ] || printf '%s:%s\n' "$_pg" "$_ln"
    done >> "$tmp/c5_hits"
done < "$tmp/pages"
if [ ! -s "$tmp/c5_hits" ]; then
    t_ok "C5: no undated present-tense release phrase in the $_npages shipped pages"
else
    t_fail "C5: an undated present-tense release claim goes stale the day the release ships -- date it or drop it: $(cut -c1-160 < "$tmp/c5_hits" | tr '\n' '|')"
fi

# --- C6: the floors ---------------------------------------------------------------------------------------------
if [ "$_npages" -ge 576 ] && [ "$_nitems" -ge 1 ] && [ "$_nheads" -ge 1 ]; then
    t_ok "C6: floors hold (pages $_npages >= 576; heading items $_nitems >= 1; ### lines $_nheads >= 1)"
else
    t_fail "C6: an extraction read too little (pages $_npages, floor 576; heading items $_nitems; ### lines $_nheads) -- the lint broke, not the texts"
fi

# --- C7: the instruments fire -----------------------------------------------------------------------------------
printf '### Milestone M1: the alpha `beta` -- gamma\n### Batch X: delta\n' | norm > "$tmp/sample_heads"
_c7=""
item_in_heads "the gate's redfirst stage" "$tmp/sample_heads" && _c7="$_c7 C1-matcher-accepted-an-absent-item"
item_in_heads "the alpha beta - gamma" "$tmp/sample_heads" || _c7="$_c7 C1-matcher-refused-a-present-item"
printf '| Commits | 3,701 (the v0.15.0 public cut and tag are pending) |\n' | grep -qiE "$PAT" || _c7="$_c7 C5-missed-an-undated-line"
printf '*2026-11-25 (M1223): the public v0.15.0 cut and its public tag are pending*\n' | grep -iE "$PAT" \
    | grep -qvE '20[0-9]{2}-[0-9]{2}-[0-9]{2}' && _c7="$_c7 C5-flagged-a-dated-line"
if [ -z "$_c7" ]; then
    t_ok "C7: the instruments fire (C1's matcher refuses an absent item and accepts a present one; C5 hits an undated line and passes a dated one)"
else
    t_fail "C7: an instrument is wrong:$_c7"
fi
