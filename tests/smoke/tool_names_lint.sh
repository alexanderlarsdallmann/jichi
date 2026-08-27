#!/bin/sh
# smoke lint: JC_ALL_TOOL_NAMES must equal the tool names the sources define
# (M285).
#
# jc_tool_name_known() answers "is that a real tool name?" for the M285 fence
# lint, and it is backed by a table. A hand-maintained table of 45 names is
# precisely the promise nobody can keep -- M262 already learned this the
# expensive way, when assetval's built-in-command list had drifted to 25 of 53
# and every command added after it was written could be shadowed with no
# warning. So do not audit this table; compare it.
#
# Ground truth, cross-checkable by hand:
#   - a static definition is `static const struct jc_tool X = {` whose NEXT line
#     is the name literal (name is the first struct field; C89, so positional
#     initialisers -- no designated-initialiser form to match)
#   - a dynamic (ctx) tool assigns `t->name = "literal"`
# Deliberately NOT counted: `tool->name = c->name` in jc_tool_user.c, which
# takes its name from config at runtime -- exactly why jc_tool_name_known
# documents user tools as the caller's problem.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
root="$SMOKE_ROOT"

# --- names the sources define ----------------------------------------------
grep -A 1 '^static const struct jc_tool .*= {$' "$root"/src/tools/*.c \
    2>/dev/null | grep -oE '"[a-z_]+",' | tr -d '",' > "$tmp/src_raw"
grep -hoE '\->name = "[a-z_]+"' "$root"/src/tools/*.c 2>/dev/null \
    | grep -oE '"[a-z_]+"' | tr -d '"' >> "$tmp/src_raw"
sort -u "$tmp/src_raw" > "$tmp/from_src"
nsrc=$(grep -c . "$tmp/from_src")

if [ "$nsrc" -ge 40 ]; then
    t_ok "extracted $nsrc tool names from src/tools/"
else
    t_fail "suspiciously few tool names extracted ($nsrc) -- did the definition
 shape change? A shrinking extraction must fail loudly, not silently pass"
fi

# --- names the table claims ------------------------------------------------
sed -n '/^static const char \*const JC_ALL_TOOL_NAMES\[\] = {/,/^};/p' \
    "$root/src/tools/jc_tool.c" | grep -oE '"[a-z_]+"' | tr -d '"' \
    | sort -u > "$tmp/from_table"
ntab=$(grep -c . "$tmp/from_table")

if [ "$ntab" -ge 40 ]; then
    t_ok "extracted $ntab names from JC_ALL_TOOL_NAMES"
else
    t_fail "suspiciously few table names extracted ($ntab)"
fi

# --- they must agree, both directions --------------------------------------
comm -23 "$tmp/from_src" "$tmp/from_table" > "$tmp/missing"
comm -13 "$tmp/from_src" "$tmp/from_table" > "$tmp/extra"

if [ ! -s "$tmp/missing" ]; then
    t_ok "every tool the sources define is in JC_ALL_TOOL_NAMES"
else
    t_fail "tools defined but MISSING from JC_ALL_TOOL_NAMES (a fence naming one
 would be reported as 'no such tool'): $(tr '\n' ' ' < "$tmp/missing")"
fi

if [ ! -s "$tmp/extra" ]; then
    t_ok "JC_ALL_TOOL_NAMES names no tool the sources do not define"
else
    t_fail "JC_ALL_TOOL_NAMES lists names no tool defines (a typo'd fence entry
 would be silently accepted): $(tr '\n' ' ' < "$tmp/extra")"
fi

t_done
