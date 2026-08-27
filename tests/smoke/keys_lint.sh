#!/bin/sh
# lint: every control chord the line editor handles is documented (M372). The
# M370 coverage shape on the KEYBINDING vocabulary: jc_term's readline
# dispatches 20+ control bytes, the chords grew by milestone (M69 history,
# M126 kill-ring, M127 word ops, M-C undo), and the user-facing docs named
# five -- Ctrl-E, Ctrl-L and Ctrl-N were documented NOWHERE. A chord nobody
# can discover is a feature that was not shipped (the M297 sentence, for
# keys). The byte->chord map lives here; a handled byte the map does not know
# FAILS the build until the author names it and documents it -- the
# session_fields tripwire, for keys. Escape-sequence keys (arrows, Home/End,
# Alt-B/F) are out of scope v1: they are discoverable conventions and their
# extraction is a parser, not a grep; stated, not hidden.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)

# M511: no trailing `\)`. It was `ch == [0-9]+\)`, which requires the comparison
# to END its condition -- so `ch == 127 || ch == 8` contributed only the 8, and
# byte 127 (the DEL most terminals actually send for Backspace, handled in three
# places in jc_term.c) could never enter the universe. The map below has had a
# `127) name="Backspace"` entry all along, unreachable: the author's intent was
# right and the extraction could not deliver it. Bytes 1-31 were complete only
# because each also appears in a parenthesised site -- luck, not coverage, and a
# chord added in a `||` position would have been invisible.
grep -ohE 'ch == [0-9]+' "$SMOKE_ROOT/src/tui/jc_term.c" \
    | grep -oE '[0-9]+' | sort -un > "$tmp/bytes"
nb=$(grep -c . "$tmp/bytes")

# --- 1: extraction floor --------------------------------------------------------
if [ "$nb" -ge 22 ]; then
    t_ok "the readline dispatch handles $nb control bytes (floor 22)"
else
    t_fail "byte extraction too thin ($nb) -- the dispatch shape moved"
fi

# --- 2: every byte maps to a named chord (the tripwire) --------------------------
# --- 3: ...and every chord is documented somewhere a user reads ------------------
unknown=""
undoc=""
smoke_md_corpus "$tmp/corpus" "$SMOKE_ROOT/docs"   # M461: not grep --include
while IFS= read -r b; do
    name=""
    case "$b" in
        1)  name="Ctrl-A" ;;   2)  name="Ctrl-B" ;;
        3)  name="Ctrl-C" ;;   4)  name="Ctrl-D" ;;
        5)  name="Ctrl-E" ;;   6)  name="Ctrl-F" ;;
        7)  name="Ctrl-G" ;;   8)  name="Backspace" ;;
        9)  name="Tab" ;;      11) name="Ctrl-K" ;;
        12) name="Ctrl-L" ;;   14) name="Ctrl-N" ;;
        16) name="Ctrl-P" ;;   17) name="Ctrl-Q" ;;
        18) name="Ctrl-R" ;;   20) name="Ctrl-T" ;;
        21) name="Ctrl-U" ;;   23) name="Ctrl-W" ;;
        25) name="Ctrl-Y" ;;   27) continue ;; # ESC: sequence keys, out of scope
        31) name="Ctrl-_" ;;   127) name="Backspace" ;;
        *)  unknown="$unknown $b"; continue ;;
    esac
    grep -q -e "$name" "$tmp/corpus" 2>/dev/null \
        || undoc="$undoc $name"
done < "$tmp/bytes"

if [ -z "$unknown" ]; then
    t_ok "every handled byte has a chord name in this map"
else
    t_fail "bytes handled but unknown to this map:$unknown -- name and document them"
fi
if [ -z "$undoc" ]; then
    t_ok "every chord is documented in docs/"
else
    t_fail "chords documented nowhere:$undoc"
fi

t_done
