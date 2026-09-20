#!/bin/sh
# smoke lint: the design tutorials' claims about THIS TREE resolve in it (M678).
#
# WHY THIS EXISTS, and it is the third time this session. `reading_refs_lint.sh`
# holds docs/reading/*.md to the source, for the reason its own header gives:
# prose about code is the fastest-rotting kind of prose. The design tutorials --
# INTERFACE_TUTORIAL, CODE_REVIEW, DOC_REVIEW and the rest -- make exactly the
# same kind of claim and were held by nothing, because that lint's universe is
# the directory it was written for.
#
# A universe drawn around the files that existed when a check was written is the
# defect this session has now found three times (rig_live_lint check 3, whose
# universe was the rigs that already complied; docs_counts_lint check 19, where
# SCAFFOLDING.md's pack count was unchecked while SDLC.md's copy was; and this).
# The pattern is worth naming: when you add a check, ask what it does NOT look
# at, and whether that set can grow.
#
# WHAT IT CHECKS, and deliberately only this:
#   - every in-tree PATH a tutorial names in a code span or a link exists;
#   - every `symbol()` it names in a code span exists somewhere in src/.
# Not that the prose about them is true -- no script can check that, and
# docs/DOC_REVIEW.md is the instrument that can. A path that has moved is the
# part a script CAN catch, and it is the part that rots first.
# THE `\/` INSIDE THE BRACKET IS DELIBERATE (M678). It was written as
# `[A-Za-z0-9_./-]`, which gawk and mawk accept and a STRICTER awk does not: in
# an awk regex LITERAL, an unescaped `/` ends the regex, and a parser that
# applies that rule inside a bracket expression too stops at `[A-Za-z0-9_.` and
# reports "Unmatched [". Measured with busybox awk, which rejects it outright.
#
# The driver failed on illumos the day it was written, on a row where the awk is
# not gawk. Whether THAT awk fails for THIS reason is not established -- the VM
# had been torn down before the per-driver output could be read, and a proxy is
# not the platform. What is established is that the regex was not portable and
# now is, which was worth fixing on its own terms.
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)
D="$SMOKE_ROOT/docs"

# The universe: the design tutorials, by name, because they are a deliberate
# set rather than a directory. A tutorial added here must be added here too --
# which is the trade this file's header is about, and the honest one is to say
# so rather than glob a directory and pretend the list maintains itself.
TUTS="INTERFACE_TUTORIAL.md CODE_REVIEW.md DOC_REVIEW.md LANGUAGE_COURSE.md
      CRAFT_AB_TUTORIAL.md READING_OPEN_SOURCE.md CONFIG_TUTORIAL.md"

n=0
: > "$tmp/badpath"
: > "$tmp/badsym"
: > "$tmp/seen"
for t in $TUTS; do
    f="$D/$t"
    [ -f "$f" ] || { echo "$t: the tutorial itself is missing" >> "$tmp/badpath"; continue; }
    n=$((n+1))

    # Paths: a code span or a relative link naming src/, tests/, scripts/ or
    # examples/. Extraction by awk, not `grep -o`: on illumos `grep -o` returns
    # only the FIRST match per line (DEFERRED.md, M661b) and these lines carry
    # several.
    awk '{ s = $0
           while (match(s, /(src|tests|scripts|examples)\/[A-Za-z0-9_.\/-]+/)) {
               p = substr(s, RSTART, RLENGTH)
               sub(/[.,;:)]+$/, "", p)
               print p
               s = substr(s, RSTART + RLENGTH)
           } }' "$f" | sort -u > "$tmp/paths"
    while read -r p; do
        [ -n "$p" ] || continue
        echo "$p" >> "$tmp/seen"
        [ -e "$SMOKE_ROOT/$p" ] || echo "$t names $p, which is not in the tree" >> "$tmp/badpath"
    done < "$tmp/paths"

    # Symbols: `name()` in a code span, only those that look like this tree's
    # own (jc_-prefixed or lower_snake), and only when the tutorial also names
    # a src/ path -- a tutorial talking about printf() is not making a claim
    # about this tree.
    if grep -q 'src/' "$f"; then
        awk '{ s = $0
               while (match(s, /`[a-z_][a-zA-Z0-9_]*\(\)`/)) {
                   sym = substr(s, RSTART + 1, RLENGTH - 4)
                   print sym
                   s = substr(s, RSTART + RLENGTH)
               } }' "$f" | sort -u > "$tmp/syms"
        while read -r sym; do
            [ -n "$sym" ] || continue
            grep -rq "$sym" "$SMOKE_ROOT/src" 2>/dev/null \
                || echo "$t names $sym(), which is in no file under src/" >> "$tmp/badsym"
        done < "$tmp/syms"
    fi
done

# --- 1: the universe is real ------------------------------------------------
_np=$(sort -u "$tmp/seen" | wc -l)
if [ "$n" -ge 5 ] && [ "$_np" -ge 10 ]; then
    t_ok "$n design tutorials scanned, $_np distinct in-tree paths named"
else
    t_fail "scanned $n tutorials and found $_np in-tree paths (want >= 5 and >= 10).
A floor of zero cannot validate: if the extraction broke, every check below
passes by finding nothing. Fix the glob or the awk before the prose."
fi

# --- 2: every named path exists ---------------------------------------------
if [ ! -s "$tmp/badpath" ]; then
    t_ok "every path the tutorials name exists in the tree"
else
    t_fail "tutorial(s) naming a path that is not there:
$(cat "$tmp/badpath")
Prose about code is the fastest-rotting kind. A moved file is the part a script
can catch; whether the sentence about it is still true is a reviewer's job."
fi

# --- 3: every named symbol exists -------------------------------------------
if [ ! -s "$tmp/badsym" ]; then
    t_ok "every symbol the tutorials name is somewhere under src/"
else
    t_fail "tutorial(s) naming a symbol that no longer exists:
$(cat "$tmp/badsym")
A renamed function leaves the prose reading perfectly and pointing nowhere."
fi

t_done
