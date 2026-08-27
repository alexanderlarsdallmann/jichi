#!/bin/sh
# smoke: every built-in slash command must be known to the collision registry
# (M262).
#
# The TUI dispatches built-ins BEFORE it looks up .jichi/commands/, so a project
# command file named after one never runs. jc_assetval's BUILTIN_CMDS[] exists so
# `jichi doctor` can warn about exactly that -- and it carried a "keep in sync
# with jc_tui.c" comment while drifting to 25 of 53 entries, silently
# under-covering every command added since it was written.
#
# Ground truth is the TUI's own completion table (cmds[]) plus the names it
# dispatches with strcmp/strncmp -- i.e. what a user can actually type.
. "$(dirname "$0")/_smoke.sh"

t_plan 2

root=$(cd "$(dirname "$0")/../.." && pwd)
tui="$root/src/tui/jc_tui.c"
av="$root/src/util/jc_assetval.c"

# Built-ins offered by Tab completion, plus those dispatched directly.
sed -n '/cmds\[\] = {/,/};/p' "$tui" | grep -o '"/[a-z-]*"' \
    | tr -d '"/' | sort -u > "$root/.bi_all.tmp"
grep -o 'strcmp(line, "/[a-z-]*")' "$tui" | sed 's/.*"\/\([a-z-]*\)")/\1/' \
    >> "$root/.bi_all.tmp"
grep -o 'strncmp(line, "/[a-z-]* "' "$tui" | sed 's/.*"\/\([a-z-]*\) "/\1/' \
    >> "$root/.bi_all.tmp"
sort -u "$root/.bi_all.tmp" -o "$root/.bi_all.tmp"

sed -n '/BUILTIN_CMDS\[\] = {/,/};/p' "$av" | grep -o '"[a-z-]*"' \
    | tr -d '"' | sort -u > "$root/.bi_reg.tmp"

nall=$(grep -c . "$root/.bi_all.tmp")
missing=$(comm -23 "$root/.bi_all.tmp" "$root/.bi_reg.tmp" | tr '\n' ' ')
rm -f "$root/.bi_all.tmp" "$root/.bi_reg.tmp"

# Guard against a vacuous pass if either extraction ever stops matching.
if [ "$nall" -ge 30 ]; then
    t_ok "found $nall built-in slash commands in the TUI to check"
else
    t_fail "only $nall built-ins extracted -- the pattern broke, this lint is vacuous"
fi

if [ -z "$missing" ]; then
    t_ok "every built-in slash command is in jc_assetval's BUILTIN_CMDS[]"
else
    t_fail "built-ins missing from BUILTIN_CMDS[] (doctor cannot warn about shadowing): $missing"
fi

t_done
