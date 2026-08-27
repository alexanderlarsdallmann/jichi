#!/bin/sh
# smoke lint: scripts/minimal-curl.sh builds the recipe docs/LOW_MEMORY.md prints (M445).
#
# THE PROMISE THIS MAKES REAL. The script's own header says its flag list "is copied from
# the recipe printed in docs/LOW_MEMORY.md so that what gets measured is what the page
# tells a reader to build; if the two ever diverge, the page is wrong or this script is."
# For a while that sentence also claimed a `tests/smoke/mincurl_recipe_lint.sh` would
# catch the divergence -- and no such file existed on either side of the M430 merge. A
# reader who believed it would not check by hand, and nothing else would either. M431
# catalogued it and reduced the claim to "NOT yet automated"; this is the file.
#
# WHY IT WAS DEFERRED RATHER THAN WRITTEN IN PASSING, in the row's own words: "the check
# is a set-comparison between a shell variable and a prose table, which is the shape that
# produces false positives". Two lints in this tier have already opened by telling their
# author to delete correct entries. So the extraction here is deliberately NARROW:
#
#   * only tokens matching --disable-<name> or --without-<name>. Not `--prefix`, not
#     `--with-<tls>`, not `--host` -- those legitimately differ between a page teaching
#     one build and a script parameterised over three (glibc/musl, openssl/mbedtls).
#   * from BOUNDED regions, not whole files: the doc's fenced block that contains the
#     recipe, and the script's single `set --` argument list. A whole-file grep would
#     pick up the prose that discusses these flags and compare a sentence to a command.
#   * with a FLOOR on each side, so a renamed variable or a moved fence fails loudly
#     instead of comparing two empty sets and passing (docs/TEST_INTEGRITY.md).
#
# Compiles nothing, downloads nothing, runs no jichi -- hence *_lint.sh. The script
# itself is deliberately not in any runner: it fetches and builds a third-party library.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)
SCRIPT="$SMOKE_ROOT/scripts/minimal-curl.sh"
DOC="$SMOKE_ROOT/docs/LOW_MEMORY.md"

if [ ! -f "$SCRIPT" ] || [ ! -f "$DOC" ]; then
    t_fail "missing input: script=$([ -f "$SCRIPT" ] && echo ok || echo NO) doc=$([ -f "$DOC" ] && echo ok || echo NO)"
    t_fail "(no inputs: doc set not extracted)"
    t_fail "(no inputs: script set not extracted)"
    t_fail "(no inputs: doc-only not checked)"
    t_fail "(no inputs: script-only not checked)"
    t_done
fi

# --- the doc's recipe: the fenced block containing --disable-ldap --------------
# Anchored on a flag rather than on a heading, because a heading is prose that can be
# reworded while the recipe stays put. The block runs from the fence above the anchor
# line to the next fence after it.
anchor=$(grep -n -- '--disable-ldap' "$DOC" | head -1 | cut -d: -f1)
if [ -n "$anchor" ]; then
    open=$(awk -v a="$anchor" 'NR<=a && /^```/ {n=NR} END{print n}' "$DOC")
    close=$(awk -v a="$anchor" 'NR>a && /^```/ {print NR; exit}' "$DOC")
    sed -n "${open},${close}p" "$DOC" \
        | grep -oE -- '--(disable|without)-[a-z0-9]+[a-z0-9-]*' \
        | sort -u > "$tmp/doc"
else
    : > "$tmp/doc"
fi

# --- the script's recipe: the `set --` argument list --------------------------
# From the `set -- \` line to the first line NOT ending in a backslash. The musl rung
# below it appends more flags with `set -- "$@"`, and those are deliberately excluded:
# they are the cross-compile rung, not the recipe the page prints.
sopen=$(grep -n '^set -- \\' "$SCRIPT" | head -1 | cut -d: -f1)
if [ -n "$sopen" ]; then
    awk -v s="$sopen" 'NR>=s {print; if (NR>s && $0 !~ /\\$/) exit}' "$SCRIPT" \
        | grep -oE -- '--(disable|without)-[a-z0-9]+[a-z0-9-]*' \
        | sort -u > "$tmp/script"
else
    : > "$tmp/script"
fi

nd=$(grep -c . "$tmp/doc")
ns=$(grep -c . "$tmp/script")

# --- 1+2: the extraction floors ----------------------------------------------
# Separate checks, so a failure says WHICH side stopped being readable. The recipe has
# 19 such flags today; 15 leaves room for it to shrink without the lint going quiet.
if [ "$nd" -ge 15 ]; then
    t_ok "extracted $nd recipe flags from docs/LOW_MEMORY.md"
else
    t_fail "only $nd flags found in the doc's fenced recipe (floor 15) -- the block moved or the fence changed; fix the extraction, never the floor"
fi
if [ "$ns" -ge 15 ]; then
    t_ok "extracted $ns recipe flags from scripts/minimal-curl.sh"
else
    t_fail "only $ns flags found in the script's 'set --' list (floor 15) -- the invocation was reshaped; fix the extraction, never the floor"
fi

# --- 3: nothing the page teaches is missing from the script -------------------
# This direction is the one that matters for a MEASUREMENT: a flag in the page and not
# in the script means the number the script reports was produced by a different build
# than the one a reader is told to make.
missing=$(comm -23 "$tmp/doc" "$tmp/script" | tr '\n' ' ')
if [ -z "$missing" ]; then
    t_ok "every flag the page prints is in the script"
else
    t_fail "the page teaches flags the script does not build with: $missing"
fi

# --- 4: and nothing extra in the script ------------------------------------
# The other direction matters for the PAGE: a flag the script uses and the page omits
# means a reader following the page gets a bigger binary than the measurement claims.
extra=$(comm -13 "$tmp/doc" "$tmp/script" | tr '\n' ' ')
if [ -z "$extra" ]; then
    t_ok "the script adds no flag the page omits"
else
    t_fail "the script builds with flags the page does not print: $extra"
fi

# --- 5: the matcher can miss --------------------------------------------------
# The M295 rule: prove the comparison is capable of failing before trusting a green.
# An invented flag must be in neither set -- if it were "found", the extraction is
# matching something other than what it names.
if ! grep -qx -- '--disable-invented-protocol' "$tmp/doc" &&
   ! grep -qx -- '--disable-invented-protocol' "$tmp/script"; then
    t_ok "the matcher can miss: an invented flag is in neither set"
else
    t_fail "the extraction matches a flag that appears nowhere"
fi

t_done
