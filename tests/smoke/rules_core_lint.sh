#!/bin/sh
# smoke lint: CLAUDE.md opens with its rules, short, one keyed to each moment (M883).
#
# WHY THIS EXISTS. On 2026-10-02 I wrapped a crash reproduction in an unmeasured
# `timeout 120` while CLAUDE.md said caps stay off for measurement runs (ANECDOTES
# #120). The rule was in the file: one bullet of 35 in its four rule sections, 17 KB
# that carried their incidents with them. It was keyed to a label I had not
# applied, and the file itself was not in the session at the time. M883 put a
# short block first: one line per rule, each keyed to the moment it applies, each
# naming the section that says why. This lint keeps it that way. A block that
# grows back into narrative, slides below another section, or points at a page or
# a section that is gone is the state the incident came from.
#
# CHECKED      -- in CLAUDE.md: (1) the first `## ` section is `## Before you act`;
#                 (2) it holds at least 12 numbered rules, today's count, and at
#                 most 16; (3) no rule runs past three lines; (4) the block is at
#                 most 2,600 bytes; (5) every `docs/...` path it names exists --
#                 except under `docs/internal/`, which is export-ignored and absent
#                 from a shipped tree (M879), so it is checked only where it exists;
#                 (6) every section a rule names in its closing parentheses is a
#                 `## ` heading or a bold bullet lead below the block -- at least
#                 10 of them, today's count, so an extraction that reads nothing
#                 fails.
# NOT CHECKED  -- whether a rule is right, or whether the section it names still
#                 says why -- a reader's job, at every change of the block. (M883's
#                 first draft pointed rule 5 at pages that did not hold one of its
#                 three reasons; a reading found it, not this lint.)
. "$(dirname "$0")/_smoke.sh"

t_plan 6
f="$SMOKE_ROOT/CLAUDE.md"
blk=$(smoke_tmp)/block.md
awk '/^## /{n++} n==1{print} n>1{exit}' "$f" > "$blk"

# --- 1: the rules come first ------------------------------------------------------
first=$(awk '/^## /{print; exit}' "$f")
if [ "$first" = "## Before you act" ]; then
    t_ok "CLAUDE.md's first section is its rules block"
else
    t_fail "CLAUDE.md's first section is '$first', not '## Before you act'"
fi

# --- 2: a rule per moment, today's count as the floor --------------------------------
nr=$(grep -c -E '^[0-9]+\. \*\*' "$blk")
if [ "$nr" -ge 12 ] && [ "$nr" -le 16 ]; then
    t_ok "the block holds $nr rules (floor 12, ceiling 16)"
else
    t_fail "the block holds $nr rules, outside 12..16"
fi

# --- 3: a rule is a line, not a story ----------------------------------------------
long=$(awk '
    /^[0-9]+\. \*\*/ { if (n > 3) print rule; rule = $0; n = 1; next }
    /^   / && n > 0   { n++; next }
    /^$/             { if (n > 3) print rule; n = 0 }
    END              { if (n > 3) print rule }' "$blk")
if [ -z "$long" ]; then
    t_ok "no rule runs past three lines"
else
    t_fail "a rule runs past three lines: $(printf '%s' "$long" | head -n 1 | cut -c1-80)"
fi

# --- 4: short enough to be read every time -------------------------------------------
size=$(wc -c < "$blk" | tr -d ' ')
if [ "$size" -le 2600 ]; then
    t_ok "the block is $size bytes (at most 2,600)"
else
    t_fail "the block is $size bytes, over 2,600: move the reasons to their sections"
fi

# --- 5: every page it points at is there ---------------------------------------------
missing=""
for p in $(awk -F'`' '{for (i = 2; i <= NF; i += 2) if ($i ~ /^docs\//) print $i}' "$blk"); do
    case "$p" in
        docs/internal/*) [ -d "$SMOKE_ROOT/docs/internal" ] || continue ;;
    esac
    [ -e "$SMOKE_ROOT/$p" ] || missing="$missing $p"
done
if [ -z "$missing" ]; then
    t_ok "every docs/ path the block names exists"
else
    t_fail "the block names pages that do not exist:$missing"
fi

# --- 6: every section a rule points at is there ------------------------------------
# A rule's pointer is its closing "(...)": items split on ';', a `docs/` path left to
# check 5. Each must open a `## ` heading or a bold bullet lead after the block.
ptrs=$(smoke_tmp)/pointers
awk '
    function flush() {
        if (rule == "") return
        sub(/ +$/, "", rule)
        if (match(rule, /\([^()]*\)$/)) {
            n = split(substr(rule, RSTART + 1, RLENGTH - 2), it, ";")
            for (i = 1; i <= n; i++) {
                sub(/^ +/, "", it[i]); sub(/ +$/, "", it[i])
                if (it[i] !~ /^`docs\//) print it[i]
            }
        }
        rule = ""
    }
    /^[0-9]+\. \*\*/ { flush(); rule = $0; next }
    /^   / && rule != "" { rule = rule " " $0; next }
    { flush() }
    END { flush() }' "$blk" > "$ptrs"
rest=$(smoke_tmp)/rest.md
awk '/^## /{n++} n>=2{print}' "$f" > "$rest"
np=$(wc -l < "$ptrs" | tr -d ' ')
dangling=""
while IFS= read -r s; do
    awk -v s="$s" 'index($0, "## " s) == 1 || index($0, "**" s) {found = 1}
                   END {exit !found}' "$rest" || dangling="$dangling [$s]"
done < "$ptrs"
if [ "$np" -ge 10 ] && [ -z "$dangling" ]; then
    t_ok "the $np sections the rules name all exist: $(tr '\n' ',' < "$ptrs" | sed 's/,$//')"
elif [ "$np" -lt 10 ]; then
    t_fail "only $np section pointers extracted, floor 10: $(tr '\n' ',' < "$ptrs")"
else
    t_fail "rules name sections that are not in CLAUDE.md:$dangling"
fi

t_done
