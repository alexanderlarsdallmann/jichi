#!/bin/sh
# smoke lint: the reading guides may not drift from the source (M222).
#
# docs/reading/ is PROSE ABOUT CODE, the fastest-rotting kind of
# documentation -- so per the house rule (prefer a lint to an audit,
# docs/TEST_INTEGRITY.md) this lint exists BEFORE the first chapter's
# prose. Three contracts over every inline-backticked token in
# docs/reading/*.md:
#
#   1. `src/...`, `include/...`, `tests/...`, `docs/...`, `examples/...`,
#      `man/...`, `editors/...` -- the path must exist in the repo.
#   2. `path.c:function` (or .h) -- the path must exist AND the function
#      name must appear followed by "(" in that file (a grep-level
#      definition-or-use check; renaming the function flags the chapter).
#   3. `path.c:123`-style LINE-NUMBER anchors are FORBIDDEN outright:
#      they rot fastest and no lint can defend their meaning.
#
# Only inline backticks are scanned (fenced code blocks are excerpts and
# may legitimately show elided or historical code); a path mentioned
# outside backticks is invisible to this lint -- write anchors in
# backticks, that is the convention the guides document.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)

guides=$(ls "$SMOKE_ROOT/docs/reading/"*.md 2>/dev/null)
nfiles=$(printf '%s\n' "$guides" | grep -c .)
if [ "$nfiles" -ge 2 ]; then
    t_ok "scanning $nfiles reading-guide files"
else
    t_fail "no reading guides found under docs/reading/ -- layout moved?"
fi

: > "$tmp/offenders"
for g in $guides; do
    # one inline-backtick token per line, with the guide's line number
    awk -v fname="$g" '
    {
        line = $0
        # strip fenced-block state: a line starting with ``` toggles
        if (line ~ /^```/) { fence = !fence; next }
        if (fence) next
        rest = line
        while (match(rest, /`[^`]+`/) > 0) {
            tok = substr(rest, RSTART + 1, RLENGTH - 2)
            printf "%s\t%d\t%s\n", fname, FNR, tok
            rest = substr(rest, RSTART + RLENGTH)
        }
    }' "$g"
done | while IFS="$(printf '\t')" read -r gf gl tok; do
    case "$tok" in
        src/*|include/*|tests/*|docs/*|examples/*|man/*|editors/*|Makefile|CLAUDE.md|CONTRIBUTING.md|CHANGELOG.md) ;;
        *) continue ;;
    esac
    path=${tok%%:*}
    anchor=${tok#*:}
    if [ "$anchor" = "$tok" ]; then
        anchor=""
    fi
    if [ ! -e "$SMOKE_ROOT/$path" ]; then
        echo "$gf:$gl: path does not exist: $tok" >> "$tmp/offenders"
        continue
    fi
    [ -n "$anchor" ] || continue
    case "$anchor" in
        *[!0-9]*) ;;
        *)
            echo "$gf:$gl: LINE-NUMBER anchor (forbidden, use file:function): $tok" \
                >> "$tmp/offenders"
            continue ;;
    esac
    case "$anchor" in
        *[!A-Za-z0-9_]*)
            echo "$gf:$gl: malformed anchor (want file:function): $tok" \
                >> "$tmp/offenders"
            continue ;;
    esac
    if ! grep -q "$anchor(" "$SMOKE_ROOT/$path"; then
        echo "$gf:$gl: '$anchor(' not found in $path (renamed?): $tok" \
            >> "$tmp/offenders"
    fi
done

if [ ! -s "$tmp/offenders" ]; then
    t_ok "every backticked path and file:function anchor resolves"
else
    t_fail "stale reading-guide reference(s) ($(grep -c . "$tmp/offenders")):"
    sed 's/^/# /' "$tmp/offenders" | head -20
fi

# (The shell-block locator check moved to docs_locators_lint.sh at M405, when
#  it grew to cover docs/curriculum/ and docs/assignments/ too -- one driver, one
#  subject. Checks 3-4 below stay here: they are about THIS guide.)

# --- 3: both index pages carry the orientation the chapters assume ----------
# Where to type, which binary, and -- the reader's second finding -- that the
# source is meant to be OPEN. Neither guide said so, while pointing at code a
# hundred times.
oriented=0
nindex=0
for f in "$SMOKE_ROOT/docs/reading/ANNAI.md" "$SMOKE_ROOT/docs/reading/FUKABORI.md" \
         "$SMOKE_ROOT/docs/reading/TSUISEKI.md"; do
    nindex=$((nindex + 1))
    if grep -q 'Before you start' "$f" &&
       grep -q 'throwaway directory' "$f" &&
       grep -qi 'have the source open' "$f" &&
       grep -q './jichi' "$f"; then
        oriented=$((oriented + 1))
    fi
done
if [ "$oriented" -eq "$nindex" ]; then
    t_ok "all $nindex index pages state where to type, which binary, and to have the source open"
else
    t_fail "only $oriented of $nindex index pages carry the orientation block"
fi

# --- 4: each series' stated anchor count is its own counted one --------------
# A hand-written number in prose is a number that rots (M260). The first draft of
# the orientation block said 103 -- the placeholder `file.c:function_name` in each
# index page had inflated the count by exactly two. Ground truth excludes it.
#
# PER SERIES since M508, and that is a correction, not a widening. The count was
# one number over docs/reading/*.md while BOTH index pages said "the chapters
# point at code 101 times" -- so each page's sentence, read in its own context,
# was already wrong by the other series' anchors, and the lint was green. A third
# series would have made it wronger while staying green: the exact defect the
# check exists to prevent, passed by the check. One claim per index page, counted
# over that series' chapter files only (lowercase prefix; the uppercase index
# pages hold the claims and contribute no anchors of their own).
: > "$tmp/counts"
for series in annai:ANNAI fukabori:FUKABORI tsuiseki:TSUISEKI; do
    pfx=${series%%:*}
    idx="$SMOKE_ROOT/docs/reading/${series#*:}.md"
    real=$(grep -ohE '`[a-z_/]+\.[ch]:[a-z_]+`' "$SMOKE_ROOT"/docs/reading/"$pfx"*.md 2>/dev/null |
           grep -v 'file\.c:function_name' | grep -c .)
    claim=$(sed -n 's/.*point at code \*\*\([0-9][0-9]*\) times\*\*.*/\1/p' "$idx" | head -1)
    if [ -z "$claim" ]; then
        printf '%s: no longer states how many code anchors its chapters carry\n' \
            "$(basename "$idx")" >> "$tmp/counts"
    elif [ "$claim" != "$real" ]; then
        printf '%s: claims %s anchors, %s present\n' \
            "$(basename "$idx")" "$claim" "$real" >> "$tmp/counts"
    fi
done
if [ ! -s "$tmp/counts" ]; then
    t_ok "each series' stated anchor count is the counted one"
else
    t_fail "anchor count(s) off -- recount, do not round: $(tr '\n' ' ' < "$tmp/counts")"
fi

t_done
