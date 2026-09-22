#!/bin/sh
# smoke lint: every shipped spec carries a `stage:` matching the INDEX.md
# section that lists it (M626).
#
# UNIVERSE, STATED, TWICE (CLAUDE.md "audit the universe"):
#   A. frontmatter route -- docs/assignments/*.md minus INDEX.md and
#      *.solution.md must each carry exactly one `stage:` line whose value is
#      in the closed vocabulary below; floored at today's exact count (90).
#   B. INDEX route -- the section tables of docs/assignments/INDEX.md, parsed
#      here (awk over `## ` headings + `|` table rows, first spec link per
#      row). The LINT parses the prose tables so the BINARY never has to --
#      that split is the deliberate resolution of the old DEFERRED row's
#      objection ("teaching the binary to parse those would couple the tool
#      to a document's formatting").
# A row in B whose spec's frontmatter disagrees, a spec in A that no table
# lists, or a spec listed under two different sections all fail loudly.
#
# The vocabulary (19): shu ha ri memory plain extras migration racket guile
# elixir haskell clojure c-systems c-io zig cpp rust process python.
#
# python joined at M674 with the language course's graded track (tasks 81-84).
# It is its own family heading rather than a "Functional track" or a "Systems
# track", because it is neither and a heading that lies to fit a slug map is a
# worse problem than one more line in this awk.
#
# c-io joined at M636g with the "C: files & structures" course (task 76). It is a
# SECOND stage under the same systems family as c-systems, deliberately: the two
# courses were split so each title is true ("manual memory" / "files &
# structures"), and a shared stage would have re-blurred them in every listing
# that groups by it. The INDEX heading rule below must be MORE SPECIFIC than the
# generic C one and come after it, because awk lets the later match win.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
AD="$SMOKE_ROOT/docs/assignments"
VOCAB="shu ha ri memory plain extras migration racket guile elixir haskell clojure c-systems c-io zig cpp rust process python"

# --- A: the frontmatter route ------------------------------------------------------
na=0
: > "$tmp/bad_a"
: > "$tmp/set_a"
for f in "$AD"/*.md; do
    b=$(basename "$f")
    case "$b" in INDEX.md|*.solution.md) continue ;; esac
    na=$((na+1))
    n=$(grep -c "^stage:" "$f")
    if [ "$n" -ne 1 ]; then
        echo "$b: $n stage: lines (want exactly 1)" >> "$tmp/bad_a"
        continue
    fi
    v=$(sed -n 's/^stage:[ ]*//p' "$f" | head -1)
    ok=0
    for w in $VOCAB; do [ "$v" = "$w" ] && ok=1; done
    if [ "$ok" -eq 1 ]; then
        printf '%s %s\n' "$b" "$v" >> "$tmp/set_a"
    else
        echo "$b: stage '$v' is not in the vocabulary" >> "$tmp/bad_a"
    fi
done

# --- 1: floor at today's exact count ----------------------------------------------
if [ "$na" -eq 90 ]; then
    t_ok "enumerated 90 shipped specs (today's exact count)"
else
    t_fail "enumerated $na specs, not 90 -- recount and refloor"
fi

# --- 2: every spec carries exactly one vocabulary stage ---------------------------
if [ ! -s "$tmp/bad_a" ]; then
    t_ok "every spec carries exactly one stage: from the closed vocabulary"
else
    t_fail "spec(s) without a valid stage: $(tr '\n' '; ' < "$tmp/bad_a" | head_bytes 300)"
fi

# --- B: the INDEX route ------------------------------------------------------------
awk '
    /^## Set A /                     { slug = "shu" }
    /^## Set B /                     { slug = "ha" }
    /^## Set C /                     { slug = "ri" }
    /^## Set D /                     { slug = "memory" }
    /^## Plain-register tier/        { slug = "plain" }
    /^## Extras \(beyond the gates\)/{ slug = "extras" }
    /^## Migration tracks/           { slug = "migration" }
    /^## Functional track .* Racket/ { slug = "racket" }
    /^## Functional track .* Guile/  { slug = "guile" }
    /^## Functional track .* Elixir/ { slug = "elixir" }
    /^## Functional track .* Haskell/{ slug = "haskell" }
    /^## Functional track .* Clojure/{ slug = "clojure" }
    /^## Systems track .* C:/        { slug = "c-systems" }
    /^## Systems track .* C: files/  { slug = "c-io" }
    /^## Systems track .* Zig/       { slug = "zig" }
    /^## Systems track .* C\+\+/     { slug = "cpp" }
    /^## Systems track .* Rust/      { slug = "rust" }
    /^## Process track/              { slug = "process" }
    /^## Language-course track .* Python/ { slug = "python" }
    /^## / && $0 !~ /^## (Set|Plain-register|Extras \(beyond|Migration tracks|Functional track|Systems track|Process track|Language-course track)/ { slug = "" }
    /^\|/ && slug != "" {
        # first spec name in the row, as a code span: `<name>.md` -- the
        # linked form [`<name>.md`](<name>.md) contains the span, and the
        # plain tier lists bare spans with no link, so the span covers both.
        if (match($0, /`[0-9p][A-Za-z0-9-]*\.md`/)) {
            nm = substr($0, RSTART + 1, RLENGTH - 2)
            if (nm !~ /solution/) { print nm " " slug }
        }
    }
' "$SMOKE_ROOT/docs/assignments/INDEX.md" | sort -u > "$tmp/set_b"

nb=$(wc -l < "$tmp/set_b")
if [ "$nb" -eq 90 ]; then
    t_ok "INDEX's tables list 90 specs, each in exactly one section"
else
    t_fail "INDEX route found $nb spec rows, not 90 -- a table moved, a spec is unlisted, or one is listed twice; read $tmp/set_b"
fi

# --- 4: the two routes agree, spec by spec ----------------------------------------
sort "$tmp/set_a" > "$tmp/set_a_sorted"
d=$(comm -3 "$tmp/set_a_sorted" "$tmp/set_b")
if [ -z "$d" ]; then
    t_ok "frontmatter and INDEX agree on every spec's stage"
else
    t_fail "disagreement (frontmatter-only left, INDEX-only right): $(printf '%s' "$d" | tr '\n' '; ' | head_bytes 300)"
fi
t_done
