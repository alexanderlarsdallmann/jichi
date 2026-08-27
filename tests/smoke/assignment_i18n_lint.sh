#!/bin/sh
# smoke lint: a translated assignment may not change the grade (M309).
#
# docs/i18n/<lang>/assignments/*.md are the first translated documents in
# this repository that carry an EXECUTABLE field: `verify:` is the grader,
# a shell command, not prose. A translator working through a German page
# will reasonably localise every string on it -- and localising that one
# silently breaks grading, in the tier whose readers are least able to
# tell a broken grader from their own mistake.
#
# So the graded frontmatter is copied byte-for-byte from the English
# original and this lint holds it there. Five fields, exact match:
#
#   verify:      the grader (code)
#   points:      a translation must not change what the task is worth
#   difficulty:  the tier
#   phase:       the gate it belongs to
#   audience:    who it is for
#
# Everything else on the page -- title, body, hints -- is free prose and
# is deliberately NOT compared: that is the whole point of a translation.
#
# Corollary the byte-match also enforces: the German editions share the
# ENGLISH fixture directories (docs/assignments/pN-.../), because a
# byte-identical verify command names those paths. One set of fixtures
# cannot drift from itself.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)

specs=$(ls "$SMOKE_ROOT"/docs/i18n/*/assignments/*.md 2>/dev/null)
nspecs=$(printf '%s\n' "$specs" | grep -c .)
if [ "$nspecs" -ge 1 ]; then
    t_ok "scanning $nspecs translated assignment spec(s)"
else
    t_fail "no translated assignments found under docs/i18n/*/assignments/ -- layout moved?"
    t_done
fi

# 1. every translated spec has an English counterpart of the same basename
: > "$tmp/orphans"
for s in $specs; do
    base=$(basename "$s")
    [ -f "$SMOKE_ROOT/docs/assignments/$base" ] || \
        echo "$s: no English original at docs/assignments/$base" >> "$tmp/orphans"
done
if [ ! -s "$tmp/orphans" ]; then
    t_ok "every translated spec has its English original"
else
    t_fail "translated spec without an original ($(grep -c . "$tmp/orphans")):"
    sed 's/^/# /' "$tmp/orphans"
fi

# 2. the graded fields are byte-identical
: > "$tmp/drift"
for s in $specs; do
    base=$(basename "$s")
    en="$SMOKE_ROOT/docs/assignments/$base"
    [ -f "$en" ] || continue
    for key in verify points difficulty phase audience; do
        # the frontmatter line, first occurrence only (a body line quoting
        # the key would otherwise be compared against nothing)
        a=$(grep "^$key:" "$en" | head -1)
        b=$(grep "^$key:" "$s" | head -1)
        if [ -z "$a" ]; then
            echo "$base: English original has no '$key:' line" >> "$tmp/drift"
            continue
        fi
        if [ "$a" != "$b" ]; then
            echo "$s: '$key:' differs from the English original" >> "$tmp/drift"
            echo "    en: $a" >> "$tmp/drift"
            echo "    de: $b" >> "$tmp/drift"
        fi
    done
done
if [ ! -s "$tmp/drift" ]; then
    t_ok "graded frontmatter (verify/points/difficulty/phase/audience) is byte-identical"
else
    t_fail "translated assignment changes the grade:"
    sed 's/^/# /' "$tmp/drift" | head -30
fi

t_done
