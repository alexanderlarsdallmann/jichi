#!/bin/sh
# smoke: the TUI's built-in slash commands, its /help, doctor's shadow registry and
# the scaffold packs must agree (M262, rewritten at M751).
#
# THE DEFECTS THIS EXISTS FOR.
#   M262: jc_assetval's BUILTIN_CMDS[] is how `jichi doctor` warns that a project
#     command file is shadowed -- the TUI dispatches built-ins BEFORE it looks up
#     .jichi/commands/, so a `context.md` there never runs. The list carried a
#     "keep in sync" comment and had drifted to 25 of 53 entries.
#   M751: this lint then went vacuous itself. It scraped `cmds\[\] = {`, and the
#     table has been `TUI_CMDS[]` since M345 -- a case-sensitive pattern that
#     matched nothing, while the floor was met by the strcmp names alone. So nothing
#     noticed that `design` and `predict` never reached the registry, that /help
#     omits /accessible, that /help presents bare /learn (a PACK command) as built
#     in, or that the sdlc pack scaffolds commands/design.md, which the built-in
#     /design hides in the TUI while headless `-p "/design"` expands it.
#
# UNIVERSES, each extracted from the file that decides it and printed:
#   table    -- the first word of every entry of TUI_CMDS[] in jc_tui.c (what Tab
#               completes and the did-you-mean suggester offers).
#   whole    -- names the TUI dispatches as a WHOLE command: strcmp(line, "/x") or
#               strncmp(line, "/x" ...) whose literal has no inner space. This
#               includes the easter eggs, which the table hides on purpose and which
#               shadow a project command just as effectively.
#   sub      -- literals with an inner space (`/learn analyze`): a subcommand. A name
#               that is ONLY ever dispatched this way (today: learn) is not a
#               shadowing built-in -- bare /learn runs the project's learn.md.
#   help     -- every `/x` token in the string literals of help() (mentions), and
#               separately the names that START a help line (`put("  /x`), which
#               present /x as a command you can type.
#   registry -- the literals of BUILTIN_CMDS[] in jc_assetval.c.
#   packs    -- every "commands/<name>.md" a scaffold pack in jc_scaffold.c ships.
#
# Portable on purpose: awk's match()/RSTART, not `grep -o`, which printed nothing
# and exited 0 on illumos and made a cluster of lints vacuous there (M661b).
. "$(dirname "$0")/_smoke.sh"

t_plan 5

root=$(cd "$(dirname "$0")/../.." && pwd)
tui="$root/src/tui/jc_tui.c"
av="$root/src/util/jc_assetval.c"
sc="$root/src/scaffold/jc_scaffold.c"
tmp=$(smoke_tmp)

# literals "/..." on stdin -> one literal per line, without the quotes/slash
lits() {
    awk '{ s = $0
           while (match(s, /"\/[a-z][a-z -]*"/)) {
               print substr(s, RSTART + 2, RLENGTH - 3)
               s = substr(s, RSTART + RLENGTH)
           } }'
}

# table: first words of TUI_CMDS[] entries
sed -n '/TUI_CMDS\[\] = {/,/^};/p' "$tui" | lits | sed 's/ .*//' | sort -u > "$tmp/table"
# handler literals: strcmp(line, "/x...") and strncmp(line, "/x...", n)
awk '{ s = $0
       while (match(s, /strn?cmp\(line, "\/[a-z][a-z -]*"/)) {
           t = substr(s, RSTART, RLENGTH); sub(/^strn?cmp\(line, "\//, "", t)
           sub(/"$/, "", t); print t; s = substr(s, RSTART + RLENGTH)
       } }' "$tui" | sed 's/ *$//' > "$tmp/handlers"
grep -v ' ' "$tmp/handlers" | sort -u > "$tmp/whole"
grep ' ' "$tmp/handlers" | sed 's/ .*//' | sort -u > "$tmp/subfirst"
comm -23 "$tmp/subfirst" "$tmp/whole" > "$tmp/subonly"
# help(): the function body, from its opening line to the first column-0 brace
awk '/^static void help\(struct jc_app \*app\)/ {on = 1} on {print} on && /^}/ {exit}' \
    "$tui" > "$tmp/helpfn"
# a mention is a `/x` NOT glued to a word or a path (.jichi/memory.md, docs/x/)
awk '{ s = " " $0
       while (match(s, /[^A-Za-z0-9._\/-]\/[a-z][a-z-]*/)) {
           print substr(s, RSTART + 2, RLENGTH - 2); s = substr(s, RSTART + RLENGTH)
       } }' "$tmp/helpfn" | sort -u > "$tmp/helpmention"
sed -n 's/^ *put("  \/\([a-z][a-z-]*\).*/\1/p' "$tmp/helpfn" | sort -u > "$tmp/helplead"
sed -n '/BUILTIN_CMDS\[\] = {/,/^};/p' "$av" \
    | awk '{ s = $0
             while (match(s, /"[a-z][a-z-]*"/)) {
                 print substr(s, RSTART + 1, RLENGTH - 2); s = substr(s, RSTART + RLENGTH)
             } }' | sort -u > "$tmp/registry"
sed -n 's/.*"commands\/\([a-z][a-z0-9-]*\)\.md".*/\1/p' "$sc" | sort -u > "$tmp/packs"

n_table=$(grep -c . "$tmp/table"); n_whole=$(grep -c . "$tmp/whole")
n_help=$(grep -c . "$tmp/helplead"); n_reg=$(grep -c . "$tmp/registry")
n_packs=$(grep -c . "$tmp/packs")

# --- 1: every extraction read something, at today's floors (M751: 55 table
# names, 59 whole handlers, 53 help lines, 59 registry names, 24 pack commands).
if [ "$n_table" -ge 55 ] && [ "$n_whole" -ge 59 ] && [ "$n_help" -ge 53 ] \
   && [ "$n_reg" -ge 59 ] && [ "$n_packs" -ge 24 ]; then
    t_ok "extractions: $n_table table, $n_whole whole handlers, $n_help help lines, $n_reg registry, $n_packs pack commands; subcommand-only: $(tr '\n' ' ' < "$tmp/subonly")"
else
    t_fail "an extraction fell below its floor -- the pattern broke and the checks below would be vacuous: table=$n_table whole=$n_whole help=$n_help registry=$n_reg packs=$n_packs"
fi

# --- 2: doctor's registry knows every whole built-in (it can warn of a shadow)
sort -u "$tmp/table" "$tmp/whole" | comm -23 - "$tmp/subonly" > "$tmp/builtins"
missing=$(comm -23 "$tmp/builtins" "$tmp/registry" | tr '\n' ' ')
if [ -z "$missing" ]; then
    t_ok "every built-in slash command is in jc_assetval's BUILTIN_CMDS[]"
else
    t_fail "built-ins missing from BUILTIN_CMDS[] (doctor cannot warn about shadowing): $missing"
fi

# --- 3: /help mentions every command the table completes
missing=$(comm -23 "$tmp/table" "$tmp/helpmention" | tr '\n' ' ')
if [ -z "$missing" ]; then
    t_ok "/help mentions every command in TUI_CMDS[]"
else
    t_fail "in TUI_CMDS[] (Tab completes it) but not in /help: $missing"
fi

# --- 4: a /help line that starts with /x names a built-in -- or says it is a pack's
bad=""
for c in $(cat "$tmp/helplead"); do
    if grep -qx "$c" "$tmp/builtins"; then
        continue
    fi
    if grep -qx "$c" "$tmp/packs" && \
       grep -E "^ *put\\(\"  /$c[^a-z-]" "$tmp/helpfn" | grep -qi 'pack command'; then
        continue
    fi
    bad="$bad$c "
done
if [ -z "$bad" ]; then
    t_ok "every command /help presents as built-in is one (pack commands say so)"
else
    t_fail "/help presents these as built-in, and they are not: $bad"
fi

# --- 5: no scaffold pack ships a command a built-in would hide in the TUI
clash=$(comm -12 "$tmp/packs" "$tmp/builtins" | tr '\n' ' ')
if [ -z "$clash" ]; then
    t_ok "no scaffold pack ships a command file named after a built-in"
else
    t_fail "scaffolded commands the TUI's built-ins shadow (headless expands them, the TUI does not): $clash"
fi

t_done
