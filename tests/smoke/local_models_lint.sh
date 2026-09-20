#!/bin/sh
# smoke lint: LOCAL_MODELS.md's worked configs do not name a model the same page
# says jichi cannot use (M678).
#
# THE DEFECT. The page carries a measured table with a `tool calls` column, and
# one row reads **prose** with the note *"jichi cannot use it"* -- the model
# answers well, returns `tool_calls: []`, and puts the call in `content` where
# nothing executes it. The page's own LM Studio worked example configured that
# model. A reader copying it got the exact failure the page spends a paragraph
# explaining, two screens further down.
#
# Nothing caught it because the two halves are prose in one file: a config block
# and a table. This joins them.
#
# WHY IT IS WORTH A CHECK AND NOT A PROOFREAD. The table is re-measured when the
# bench changes -- the qwen row was taken 2026-08-21 and re-taken 2026-09-20 --
# so the set of unusable models is a moving set, and a config that is correct
# today can be contradicted by a measurement tomorrow without anyone editing it.
# That is precisely the shape a lint holds and a reader cannot.
# The counters are named n_cfg and n_bad rather than the obvious two-letter
# pair, because one of those two letters is netcat and smoke_lint check 3
# forbids running it anywhere in the tier. That check is a flat text match: it
# cannot tell a shell variable from a command, nor a comment from code, and
# teaching it to would make it unreliable at the job it does well. So the
# variable was renamed rather than the check exempted -- and then this comment
# was reworded, because naming the collision literally trips it too.
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)
DOC="$SMOKE_ROOT/docs/LOCAL_MODELS.md"

# The unusable set: table rows whose `tool calls` column is not **native**.
# Extraction by awk -- `grep -o` returns only the first match per line on
# illumos (DEFERRED.md, M661b), and these rows carry several code spans.
awk -F'|' '/^\| `[a-z]/ {
        name = $2; verdict = $4
        gsub(/[` ]/, "", name)
        gsub(/^[ \t]+|[ \t]+$/, "", verdict)
        if (verdict !~ /native/) { print name }
    }' "$DOC" > "$tmp/unusable"

# Every model named in a JSON config block on the page.
awk '/^```/ { inblock = !inblock; next }
     inblock && /"model"[ \t]*:/ {
        s = $0
        if (match(s, /"model"[ \t]*:[ \t]*"[^"]+"/)) {
            m = substr(s, RSTART, RLENGTH)
            sub(/.*"model"[ \t]*:[ \t]*"/, "", m)
            sub(/"$/, "", m)
            print m
        }
     }' "$DOC" | sort -u > "$tmp/configured"

# --- 1: both extractions found something ------------------------------------
n_bad=$(wc -l < "$tmp/unusable"); n_cfg=$(wc -l < "$tmp/configured")
if [ "$n_bad" -ge 1 ] && [ "$n_cfg" -ge 3 ]; then
    t_ok "$n_cfg model(s) named in configs, $n_bad marked unusable in the table"
else
    t_fail "extraction floor: $n_cfg configured model(s), $n_bad unusable row(s).
A floor of zero cannot validate -- with either list empty, check 2 passes by
comparing nothing. Fix the awk before the prose."
fi

# --- 2: no config names an unusable model -----------------------------------
: > "$tmp/bad"
while read -r m; do
    [ -n "$m" ] || continue
    grep -Fxq "$m" "$tmp/unusable" && echo "$m" >> "$tmp/bad"
done < "$tmp/configured"
if [ ! -s "$tmp/bad" ]; then
    t_ok "no worked config names a model the page's own table calls unusable"
else
    t_fail "config block(s) naming a model this page says jichi cannot use:
$(tr '\n' ' ' < "$tmp/bad")
The table's \`tool calls\` column is the verdict. A reader copies the config,
gets a model that answers well and never runs a tool, and meets the failure the
page explains two screens later."
fi

# --- 3: the table still carries a verdict column that can say no ------------
# If every row were marked native the check above would be vacuous, and the most
# likely cause is not six good models but a broken column.
if [ "$n_bad" -ge 1 ] && grep -q 'tool calls' "$DOC"; then
    t_ok "the table still distinguishes native from prose ($n_bad non-native row(s))"
else
    t_fail "the model table has no non-native row, or lost its 'tool calls'
column. Check 2 is then comparing against an empty set and cannot fail -- which
is not the same as every model working."
fi

t_done
