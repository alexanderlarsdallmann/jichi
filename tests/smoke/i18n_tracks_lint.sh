#!/bin/sh
# smoke lint: a translation may fall behind its English source, but not SILENTLY (M582).
#
# THE DEFECT THIS EXISTS FOR. docs/i18n/README.md has documented a provenance
# convention since the translations were written -- every translated file carries
# `<!-- tracks: <relpath> @ <commit> -->`, and "a translation whose tracked commit
# lags far behind should carry a visible note". Nothing checked it. Measured at
# M582: all 40 commit-bearing markers were behind (1 to 17 commits), five files
# carried no marker at all, and the README's own status table still described
# de/es/ja/zh as "maintained alongside en".
#
# The cost was not abstract. Four decks told a German, Spanish, Japanese and
# Chinese audience that the binary is ~700 KB (measured: 1.2 MB size-optimized,
# 1.7 MB as built) and that the suite has 7,170 checks (measured: 12,960). A
# reader of the German deck was given a number wrong by a factor of 2.6, on the
# slide that exists to make the project credible.
#
# DELIBERATELY NOT GIT-DEPENDENT. The obvious check -- `git log <tracked>..HEAD`
# -- is refused for the reason changelog_coverage_lint.sh already writes down: the
# published snapshot ships a FRESH history (docs/plans/2026-08-public-snapshot.md),
# so a history-dependent gate behaves differently in the tree people actually
# acquire. Every check below reads only file CONTENT, and therefore gives the same
# verdict here and in the snapshot.
#
# WHAT IS CHECKED, and what is not:
#
#   checked      -- that the marker EXISTS and is well formed (check 1), that the
#                   path it names resolves (check 2), that a deck carries the same
#                   number of slide separators as its English counterpart or
#                   declares the gap with the right number (check 3), and that a
#                   translation introduces no figure of three digits or more that
#                   its English counterpart does not contain, or declares that gap
#                   with the right number (check 4).
#
#   NOT checked  -- whether the prose is a faithful translation. That needs a
#                   reader of the language, which is what docs/DEFERRED.md's
#                   review rows are for. A green here means "no figure was invented
#                   and no slide vanished unannounced", never "this is accurate".
#
#   NOT checked  -- a figure spelled in words. `四千五百多` was a stale count in
#                   docs/i18n/zh/PHILOSOPHY.md and check 4 could not see it; it was
#                   found by reading the three sibling translations. A digit filter
#                   finds digits.
#
#   NOT checked  -- drift INSIDE a slide. docs/presentations/02-using-jichi.md
#                   gained 141 lines with no change in slide count, so check 3 lets
#                   that by. The checks bound the gap; they do not close it.
#
# THE TWO-STATE SHAPE (checks 3 and 4), which is license_lint.sh's: a gap may be
# declared instead of fixed, but the declaration carries the COUNT and the count is
# verified. A declaration therefore cannot be written once and forgotten -- when the
# English page moves again, the number stops matching and the gate fires.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(smoke_tmp)

# Files exempt from a tracks marker, each with the reason. Kept as a checked list
# rather than a pattern, so adding a translation cannot silently inherit an
# exemption it was never granted.
#   en/GETTING_STARTED.md  -- the canonical source the others track; it tracks nothing.
#   de/EINFACHE_SPRACHE.md -- not a translation. It declares itself an independent
#                             page ("kein Ersatz fuer die anderen Seiten") and links
#                             PLAIN_LANGUAGE.md as a sibling, not as a source.
EXEMPT="docs/i18n/en/GETTING_STARTED.md
docs/i18n/de/EINFACHE_SPRACHE.md"

is_exempt() {
    printf '%s\n' "$EXEMPT" | while IFS= read -r e; do
        [ "$e" = "$1" ] && { echo yes; break; }
    done
}

# ---- check 1: the denominator -------------------------------------------------
# Four of the five checks below are ABSENCE assertions: no missing marker, no
# broken path, no undeclared gap, no invented figure. Every one of them holds
# trivially over an empty corpus, so a `find` that matched nothing -- a renamed
# directory, a moved docs tree in the snapshot -- would print four greens and mean
# nothing. This check is what those four are measured against.
n_tr=$(cd "$ROOT" && find docs/i18n -name '*.md' | /usr/bin/grep -c .)
n_mk=$(cd "$ROOT" && /usr/bin/grep -rl '<!-- tracks:' docs/i18n | /usr/bin/grep -c .)
n_dk=$(cd "$ROOT" && find docs/i18n -path '*/presentations/*.md' | /usr/bin/grep -c .)
n_en=$(cd "$ROOT" && find docs/presentations -name '*.md' | /usr/bin/grep -c .)
if [ "$n_tr" -ge 40 ] && [ "$n_mk" -ge 40 ] && [ "$n_dk" -ge 24 ] && [ "$n_en" -ge 7 ]; then
    t_ok "corpus: $n_tr translated pages, $n_mk carrying a marker, $n_dk decks against $n_en English"
else
    t_fail "the corpus collapsed, so the four absence checks below would pass over
   nothing. Saw $n_tr translated pages ($n_mk with a marker), $n_dk decks and
   $n_en English decks; floors are 40/40/24/7. Either the tree moved or this
   lint is looking in the wrong place."
fi

# ---- check 2: every translated file carries a well-formed tracks marker --------
missing=""
malformed=""
for f in $(cd "$ROOT" && find docs/i18n -name '*.md' | sort); do
    [ "$f" = "docs/i18n/README.md" ] && continue
    [ -n "$(is_exempt "$f")" ] && continue
    line=$(sed -n 's/.*\(<!-- tracks:[^>]*-->\).*/\1/p' "$ROOT/$f" | sed -n 1p)
    if [ -z "$line" ]; then
        missing="$missing $f"
        continue
    fi
    # two accepted forms: "<rel> @ <sha>" and "<rel> (canonical)"
    body=$(printf '%s\n' "$line" | sed 's|<!-- tracks: *||; s| *-->||')
    case "$body" in
        *" @ "*)        ;;
        *"(canonical)") ;;
        *) malformed="$malformed $f" ;;
    esac
done
if [ -z "$missing" ] && [ -z "$malformed" ]; then
    t_ok "every translated page names the source it tracks"
else
    t_fail "a translation with no provenance cannot be checked against anything, and
   docs/i18n/README.md makes the marker a rule. Missing:${missing:- none}.
   Malformed (want '<relpath> @ <sha>' or '<relpath> (canonical)'):${malformed:- none}"
fi

# ---- check 3: the path a marker names resolves --------------------------------
broken=""
for f in $(cd "$ROOT" && find docs/i18n -name '*.md' | sort); do
    [ "$f" = "docs/i18n/README.md" ] && continue
    body=$(sed -n 's/.*<!-- tracks: *\([^>]*\) *-->.*/\1/p' "$ROOT/$f" | sed -n 1p)
    [ -z "$body" ] && continue
    rel=$(printf '%s\n' "$body" | sed 's| @ .*||; s| (canonical)||; s| *$||')
    d=$(dirname "$ROOT/$f")
    [ -f "$d/$rel" ] || broken="$broken $f->$rel"
done
if [ -z "$broken" ]; then
    t_ok "every tracks marker resolves to a file that exists"
else
    t_fail "a marker pointing at nothing is worse than no marker: it reads as
   provenance while proving nothing, and a moved English page produces exactly
   this. Broken:$broken"
fi

# ---- check 4: slide-count parity, or a counted declaration --------------------
# docs/i18n/README.md: "Slide translations keep the same slide count as the English
# deck." Measured at M582: 16 of 28 did not, by one or two slides each.
bad3=""
for f in $(cd "$ROOT" && find docs/i18n -path '*/presentations/*.md' | sort); do
    b=$(basename "$f")
    en="$ROOT/docs/presentations/$b"
    [ -f "$en" ] || continue
    k=$(sed 's/\r$//' "$ROOT/$f" | /usr/bin/grep -c '^---$')
    m=$(sed 's/\r$//' "$en" | /usr/bin/grep -c '^---$')
    [ "$k" = "$m" ] && continue
    want=$((m - k))
    got=$(sed -n 's/.*<!-- slides-behind: *\([0-9][0-9]*\).*/\1/p' "$ROOT/$f" | sed -n 1p)
    [ "$got" = "$want" ] || bad3="$bad3 $f(en=$m,this=$k,declared=${got:-none})"
done
if [ -z "$bad3" ]; then
    t_ok "every deck matches its English slide count or declares the gap, with the number"
else
    t_fail "a deck short of slides has lost CONTENT, not formatting -- the English
   deck gained sections the reader of this language never sees. Fix it by
   translating them, or declare it with '<!-- slides-behind: N -->' where N is
   exact. An approximate declaration is refused on purpose: a number nobody
   maintains is how the previous convention rotted.$bad3"
fi

# ---- check 5: no invented figure of three digits or more ----------------------
# The check that would have caught ~700 KB and 7,170 the day the English deck moved.
# HTML comments are stripped first, so a declaration's own numbers are not evidence.
bad4=""
nums() {
    awk 'BEGIN{RS="-->"} {sub(/<!--.*/,"")} {print}' "$1" |
        /usr/bin/grep -oE '[0-9]+([.,][0-9]{3})+|[0-9]{3,}' |
        tr -d '.,' | sort -u
}
for f in $(cd "$ROOT" && find docs/i18n -name '*.md' | sort); do
    case "$f" in
        docs/i18n/README.md|docs/i18n/en/*) continue ;;
    esac
    b=$(basename "$f")
    case "$f" in
        */presentations/*) en="$ROOT/docs/presentations/$b" ;;
        *)                 en="$ROOT/docs/$b" ;;
    esac
    [ -f "$en" ] || continue
    nums "$ROOT/$f" > "$tmp/t.n"; nums "$en" > "$tmp/e.n"
    want=$(comm -23 "$tmp/t.n" "$tmp/e.n" | /usr/bin/grep -c . || true)
    [ "$want" = "0" ] && continue
    got=$(sed -n 's/.*<!-- figures-behind: *\([0-9][0-9]*\).*/\1/p' "$ROOT/$f" | sed -n 1p)
    if [ "$got" != "$want" ]; then
        bad4="$bad4 $f(extra=$want,declared=${got:-none}: $(comm -23 "$tmp/t.n" "$tmp/e.n" | tr '\n' ' '))"
    fi
done
if [ -z "$bad4" ]; then
    t_ok "no translation carries a figure its English source does not, undeclared"
else
    t_fail "a figure that appears only in a translation is a claim nobody wrote and
   nobody can check -- and it is how four decks came to promise a ~700 KB binary
   for months. Bring the English figure across, or declare the gap with
   '<!-- figures-behind: N -->' where N is exact.$bad4"
fi

t_done
