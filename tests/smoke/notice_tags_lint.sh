#!/bin/sh
# lint: the bracketed-tag registry stays true (M369). Every tag is a
# three-party protocol -- the model receives it, drivers grep for it, prompts
# teach it -- and M368 found the cost of keeping it as folklore: the M355
# flight-plan sentence contained the budget-notice driver's grep needle, and
# the driver was red for thirteen milestones. Four checks: the extraction
# floor (a broken scrape fails loudly, M295), every tag emitted in runtime C
# has a registry row, every row names a tag something emits, and each
# model-facing STRUCTURAL FORM exists in exactly one render file -- so a
# prompt or a second emitter can never quietly contain a driver's needle.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
DOC="$SMOKE_ROOT/docs/NOTICES.md"

FILES=$(find "$SMOKE_ROOT/src" -name '*.c' ! -path '*/scaffold/*')

# --- extraction: tag shapes at literal start or after \n / %s ------------------
# OB keeps the open bracket out of the regex literal so the smoke lint's
# bashism scan does not trip on an adjacent bracket pair (the M362 trick).
OB='\['
grep -hoE '("|\\n|%s)'"$OB"'[a-z-]{2,}[]:]' $FILES \
    | sed -E 's/^("|\\n|%s)//' | sort -u > "$tmp/src.tags"
nsrc=$(grep -c . "$tmp/src.tags")

# --- documented union: both tables + the known-prose list ----------------------
# every backticked tag token anywhere in the page (all three sections);
# a row carrying two tags (codebase/docs:) contributes both.
grep -oE '`'"$OB"'[a-z-]+[]:]?`' "$DOC" | tr -d '`' \
    | sed -E 's/([a-z-])$/\1]/' | sort -u > "$tmp/doc.tags"
ndoc=$(grep -c . "$tmp/doc.tags")

# --- 1: extraction floors -------------------------------------------------------
if [ "$nsrc" -ge 35 ] && [ "$ndoc" -ge 35 ]; then
    t_ok "ground truth: $nsrc emitted tags, $ndoc registry rows (floor 35)"
else
    t_fail "extraction too thin: src=$nsrc doc=$ndoc -- the scrape broke"
fi

# --- 2: every emitted tag is registered -----------------------------------------
miss=$(comm -23 "$tmp/src.tags" "$tmp/doc.tags" | tr '\n' ' ')
if [ -z "$miss" ]; then
    t_ok "every emitted tag has a registry row"
else
    t_fail "tags emitted but unregistered:$miss -- add a NOTICES.md row"
fi

# --- 3: every registered tag is emitted ------------------------------------------
ghost=$(comm -13 "$tmp/src.tags" "$tmp/doc.tags" | grep -v '\[codebase\]' \
        | tr '\n' ' ')
if [ -z "$ghost" ]; then
    t_ok "no registry row names a tag nothing emits"
else
    t_fail "registry rows with no emitter:$ghost"
fi

# --- 4: each structural form has exactly ONE render file (the anti-M368) --------
# form|render-file pairs; the form must appear in its render file and NOWHERE
# else in runtime C -- a prompt sentence or a second emitter containing a
# driver's needle is exactly the M368 defect.
bad=""
while IFS='|' read -r form home; do
    n_home=$(grep -rlF -- "$form" "$SMOKE_ROOT/src/$home" 2>/dev/null | wc -l)
    n_else=$(grep -rlF -- "$form" $FILES 2>/dev/null \
             | grep -v "src/$home" | wc -l)
    if [ "$n_home" -lt 1 ] || [ "$n_else" -gt 0 ]; then
        t_note "form '$form': home=$n_home elsewhere=$n_else" 2>/dev/null || true
        bad="$bad '$form'(home=$n_home,other=$n_else)"
    fi
done <<'EOF'
[envelope] budget check:|chat/jc_envelope.c
[envelope] the verifier just PASSED|chat/jc_envelope.c
[context] this turn has used|chat/jc_compact.c
[undo] the operator restored|snapshot/jc_snapshot.c
[resume] since this conversation last ran|session/jc_session.c
[note: the arguments|tools/jc_tool.c
EOF
if [ -z "$bad" ]; then
    t_ok "every structural form lives in exactly one render file"
else
    t_fail "structural-form collisions:$bad"
fi

t_done
