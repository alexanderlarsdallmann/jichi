#!/bin/sh
# smoke lint: no page carries an unfilled template placeholder (M872).
#
# WHY THIS EXISTS. M869 and M870 were committed, gated by a green `make ci` and
# pushed to master with "**Held by.** @HELD@" in both ROADMAP entries. The script
# that wrote the entries left a placeholder for the evidence, to be filled once the
# red-first runs were in, and nothing filled it. The runs had happened -- their logs
# named every red check -- but the record of them had not, and no docs lint reads a
# placeholder as a defect. Found two commits later, by reading the ROADMAP.
#
# CHECKED      -- every Markdown page in the tree, .git excluded, for a template
#                 token: @WORD@ or {{WORD}}, an upper-case word of three characters
#                 or more. None held one once the two were filled.
# THE FLOOR    -- counts the pages that SHIP: those outside docs/internal/, the one
#                 directory .gitattributes marks export-ignore. M872 set it to the
#                 checkout's count, 925, and every tree a rig ships with `git
#                 archive` failed it: 856 pages, on the first board it met (the Pi
#                 400, M879). A checkout, a shipped tree and a release tarball hold
#                 the same pages outside docs/internal/, so one floor fits all three,
#                 and none of them needs git (M451: on-target trees often have none).
# NOT CHECKED  -- a placeholder in any other form ("TBD", "XXX", "<fill in>"), which
#                 prose also uses for real; and whether a filled section is true,
#                 which only a reader can tell.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
root=$(cd "$(dirname "$0")/../.." && pwd)

# The floor is the shipped page count when it was set (M879): a find over the wrong
# directory would find nothing, and nothing would hold no placeholder.
JC_PH_FLOOR=856
PAT='@[A-Z][A-Z0-9_]{2,}@|\{\{ ?[A-Z][A-Z0-9_]* ?\}\}'

# --- 1: the universe is the tree's pages -------------------------------------------
n=$(find "$root" \( -name .git -o -path "$root/docs/internal" \) -prune -o \
        -type f -name '*.md' -print | wc -l | tr -d ' ')
if [ "$n" -ge "$JC_PH_FLOOR" ]; then
    t_ok "the pages read are the tree's ($n shipped Markdown pages, floor $JC_PH_FLOOR)"
else
    t_fail "only $n shipped Markdown pages found under $root (floor $JC_PH_FLOOR)"
fi

# --- 2: none carries a template token ---------------------------------------------
hits=$(find "$root" -name .git -prune -o -type f -name '*.md' \
           -exec grep -nE "$PAT" {} + 2>/dev/null)
if [ -z "$hits" ]; then
    t_ok "no page carries an unfilled template placeholder"
else
    t_fail "unfilled placeholders: $(printf '%s\n' "$hits" | sed "s#^$root/##" | head -5)"
fi

t_done
