#!/bin/sh
# smoke: a documented config default must equal the parser's actual default
# (M261).
#
# `include/jc_config.h` annotates fields with "(def N)", and those annotations
# are what a reader trusts -- nobody cross-checks a comment against the parser
# 400 lines away in another file. They drift silently: M257 flipped type-ahead
# to opt-in and left `int type_ahead; /* ... (def 1) */` behind, so the header
# advertised the opposite of the shipped behaviour.
#
# So compare them mechanically. For every field whose comment states a numeric
# default, find `out-><field> = jc_json_get_{bool,int,long}(root, "...", D)` in
# src/config/jc_config.c and require the two to agree. `lite ? A : B` takes B,
# the non-lite default, which is what the comment describes. Fields whose
# default is computed elsewhere have no literal to compare and are skipped --
# reported as a count, so a shrinking comparison is visible rather than silent.
#
# M393 CLOSES THIS LINT'S OWN STATED EXCLUSION. Taking only the non-lite branch
# means a comment may read "(def 1)" for a key that defaults to 0 whenever the
# low-resource profile is on -- and that profile turns ITSELF on below ~1 GB of
# RAM, so the documented default is false on exactly the small machines this
# project advertises as supported. That is not hypothetical: the M392
# documentation review found `snapshots` documented as "default true" when it is
# 0 under lite (no checkpoints, no /undo, silently) and `references` the same
# (every @file stays literal). Both descended from a header comment stating one
# number for a two-valued default. So check 4 requires any lite-dependent
# default to SAY it is lite-dependent, in the comment the docs are written from.
. "$(dirname "$0")/_smoke.sh"

t_plan 5

root=$(cd "$(dirname "$0")/../.." && pwd)
hdr="$root/include/jc_config.h"
src="$root/src/config/jc_config.c"
mismatch=""
checked=0
skipped=0

# Fields documented as "(def N)" / "(default N)" on their declaration line.
docs=$(grep -E '^ +(int|long|double) +[a-z_0-9]+;.*\(def(ault)? -?[0-9]+' "$hdr" \
       | sed -E 's/^ +(int|long|double) +([a-z_0-9]+);.*\(def(ault)? (-?[0-9]+).*/\2 \4/')

# The parser call may wrap across lines, so join the file first.
joined=$(tr '\n' ' ' < "$src" | tr -s ' ')

echo "$docs" | while read -r field want; do
    [ -n "$field" ] || continue
    echo "$field $want"
done > "$root/.cfgdoc.tmp"

while read -r field want; do
    [ -n "$field" ] || continue
    # M393: absorb an optional cast and a double literal. Without them this
    # lookup silently skipped `= (int)jc_json_get_num(..., lite ? 0.0 : 2.0)`
    # -- and `maxSubagentDepth` was documented "(default 1)" against a parser
    # default of 2 for as long as that blindness lasted. A skipped field looks
    # exactly like a matching one in the count; only the mismatch list differs.
    # NARROW FIRST, then match. `$joined` is the whole of jc_config.c collapsed
    # onto one ~64 KB line (it must be joined, because the assignment wraps).
    # Applying the regex below to all 64 KB is 3 ms under GNU sed and HANGS
    # under BSD sed -- measured on FreeBSD 15.1, where this lint ran for 27
    # minutes under a `timeout 60` that could not kill it (M459). The engine
    # backtracks catastrophically through the nested \{0,1\} groups between two
    # unanchored .* over that much input, and it is the MATCHING field that is
    # slow, not the missing one.
    #
    # A cheap grep first cuts the input from ~64 KB to ~80 bytes, after which
    # any sed is instant. The regex is unchanged, so the semantics are too.
    # ...and pick the PARSE site, not the first assignment. A field is usually
    # assigned twice -- `out->x = lite ? 12 : 25;` for the default and then
    # `out->x = jc_json_get_num(root, "key", ...);` for the override -- and the
    # original regex, being greedy from both ends, anchored on the jc_json_get
    # one. Taking grep's first match instead silently compared against the
    # wrong statement, and every field became "not literal": the lint's own
    # check 1 caught that immediately ("the extraction broke, so this lint is
    # vacuous"), which is exactly what that check is for.
    # `(root,` and `tail -1`, both to mirror the original regex EXACTLY.
    # context_limit is assigned three times -- once from the per-model object
    # (`jc_json_get_num(model, "contextLength", ...)`), once as a plain lite
    # default, and once from the top-level config -- and the original pattern,
    # greedy from the left, anchored on the LAST one that also matched `(root,`.
    # Filtering on jc_json_get alone and taking the first picked the `model`
    # call, whose regex then failed, and the field silently became "not
    # literal": 25 compared / 4 not-literal became 24 / 5. Caught by diffing
    # every field's extraction against the old method rather than by reading
    # the summary line.
    stmt=$(printf '%s' "$joined" | grep -o "out->$field *=[^;]*;" \
           | grep 'jc_json_get_[a-z]*(root,' | tail -1)
    got=$(printf '%s' "$stmt" \
        | sed -n "s/.*out->$field *= *\(([a-z_ ]*) *\)\{0,1\}jc_json_get_[a-z]*(root, *\"[^\"]*\", *\(lite *? *-\{0,1\}[0-9.]* *: *\)\{0,1\}\(-\{0,1\}[0-9]*\)\(\.[0-9]*\)\{0,1\}).*/\3/p" \
        | head -1)
    if [ -z "$got" ]; then
        skipped=$((skipped + 1))
        continue
    fi
    checked=$((checked + 1))
    if [ "$got" != "$want" ]; then
        mismatch="$mismatch $field(header:$want parser:$got)"
    fi
done < "$root/.cfgdoc.tmp"
# The loop above runs in this shell (redirect, not a pipe), so the counters
# survive -- a pipeline would run it in a subshell and lose them.
rm -f "$root/.cfgdoc.tmp"

if [ "$checked" -lt 5 ]; then
    t_fail "only $checked defaults were comparable -- the extraction broke, so this lint is vacuous"
else
    t_ok "compared $checked documented defaults against the parser ($skipped not literal)"
fi

if [ -z "$mismatch" ]; then
    t_ok "every documented config default matches the parser"
else
    t_fail "documented default disagrees with the parser:$mismatch"
fi

# --- 3. shipped example configs must not advertise keys jichi ignores -------
# Top-level keys in the compiled-in config.example.json chunks are written at
# exactly two spaces of indentation; nested ones (inside lspServers) are deeper,
# so this picks out only the keys a user would set at the top level.
# `formatCommand` sat in nine packs suggesting a formatter jichi never read -- a
# promise the binary does not keep (M262).
keys=$(grep -oE '"  \\"[a-zA-Z]+\\":' "$root/src/scaffold/jc_scaffold.c" \
       | sed -E 's/.*\\"([a-zA-Z]+)\\":/\1/' | sort -u)
unknown=""
for k in $keys; do
    case "$k" in
        comment|models) continue ;;   # documentation string; the model list
    esac
    grep -q "\"$k\"" "$src" || unknown="$unknown $k"
done

if [ -z "$keys" ]; then
    t_fail "extracted no example-config keys -- the pattern broke, this check is vacuous"
elif [ -z "$unknown" ]; then
    t_ok "every key in a shipped example config is one jichi parses"
else
    t_fail "example configs advertise keys jichi never reads:$unknown"
fi

# --- 4 (M393): a lite-dependent default must disclose that it is one ---------
# Pairs of (field, key) whose parser default is `lite ? A : B` with A != B. The
# source is joined above, so a call wrapped across lines is still matched.
# The `\(...\)` group absorbs an optional cast: eight of the thirteen are written
# `out->max_retries = (int)jc_json_get_num(...)`, and a pattern demanding the call
# right after `=` silently found only five. The floor caught that -- which is what
# a floor is for (M295): fix the extraction, never the floor.
pairs=$(printf '%s' "$joined" \
    | grep -oE 'out->[a-z_.]+ = (\([a-z_ ]+\) *)?jc_json_get_[a-z]+\(root, "[a-zA-Z]+", *lite \? *[0-9.]+ *: *[0-9.]+' \
    | sed -E 's/out->([a-z_.]+) = (\([a-z_ ]+\) *)?jc_json_get_[a-z]+\(root, "([a-zA-Z]+)".*/\1 \3/' \
    | sort -u)
npairs=$(printf '%s\n' "$pairs" | grep -c .)

silent=""
nolookup=0
printf '%s\n' "$pairs" > "$root/.cfglite.tmp"
while read -r field key; do
    [ -n "$field" ] || continue
    leaf=${field##*.}
    # ALL matching declarations, not the first: two structs legitimately share a
    # field name (`context_limit` is a per-model window AND the top-level
    # compaction budget, and only the latter is lite-dependent). Checking just
    # the first sent this lint at the wrong declaration -- and "fixing" that one
    # would have written a false claim onto a field that has no lite branch. So
    # the contract is "some declaration of this name discloses it", and the
    # ambiguity is stated rather than silently resolved the wrong way.
    decl=$(grep -E "^ +[a-zA-Z_]+ +\**$leaf;" "$hdr")
    if [ -z "$decl" ]; then
        nolookup=$((nolookup + 1))
        continue
    fi
    printf '%s' "$decl" | grep -qiE 'lite|lowresource' || silent="$silent $key"
done < "$root/.cfglite.tmp"
rm -f "$root/.cfglite.tmp"

if [ "$npairs" -ge 10 ]; then
    t_ok "found $npairs lite-dependent defaults ($nolookup with no scalar declaration)"
else
    t_fail "only $npairs lite-dependent defaults found -- the extraction moved; fix it, not the floor"
fi

if [ -z "$silent" ]; then
    t_ok "every lite-dependent default says so in its header comment"
else
    t_fail "default differs under --lite but the header comment does not say so:$silent
    -- a reader (and every doc written from these comments) is told one number
       for a two-valued default; name lite/lowResource and give both values"
fi

t_done
