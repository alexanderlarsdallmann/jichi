#!/bin/sh
# smoke lint: the asset frontmatter key tables may not drift from their loaders
# (M389) -- and every asset kind must have its own table.
#
# jc_assetval.c holds one allowed-key table per project-asset kind, and doctor
# validates each asset against it (unknown/misspelled keys, unterminated '---',
# missing description). The tables carry the comment "keep in sync with the
# loaders" -- a hand-maintained invariant, and it has already failed once: M302
# added `style:` to the agent and skill parsers without adding it here, so doctor
# reported the new FEATURE as a typo (found by accident, by M302's own driver).
#
# The lesson was already paid for SIX LINES BELOW those tables, on the
# BUILTIN_CMDS list: "A hand-maintained invariant is a promise nobody can keep,
# so it is now mechanical" (M262, after that list drifted to 25 of 53). This lint
# applies the same treatment to its neighbour.
#
# Two directions, because either alone misses half:
#   * a key the parser READS but the table lacks -> doctor calls a feature a typo
#   * a key the table lists but no parser READS -> a dead key accepted silently
#     (the M285 rule: a declared-but-dead name is worse than an absent one)
#
# Plus the M372 tripwire shape: the parser->kind->table MAP lives here, and every
# mapped kind must appear in the enum, own a *_KEYS table, and own a `case` in
# jc_assetval_check's switch -- because that switch's `default:` silently maps an
# unknown kind to CMD_KEYS, so a new kind without a case would be validated
# against the wrong table with nothing said.
#
# STATED SCOPE (the M305 rule): the map below is the registry. A NEW asset parser
# that nobody adds to it is invisible to this lint -- the same limit keys_lint
# states for escape sequences. What the lint does guarantee is that every kind in
# the map, and every kind in the enum, is completely and exclusively described by
# its table.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)

AV_H="$SMOKE_ROOT/include/jc_assetval.h"
AV_C="$SMOKE_ROOT/src/util/jc_assetval.c"

# --- the map: kind | loader | table ------------------------------------------
# One line per frontmatter-bearing .jichi/ asset. Four exist: agents/, skills/,
# commands/ and output-styles/ (the first three are scaffolded by `init`; output
# styles are user-written but equally loadable -- docs/OUTPUT_STYLES.md).
cat > "$tmp/map" <<'EOF'
AGENT src/command/jc_agentdef.c AGENT_KEYS
SKILL src/skill/jc_skill.c SKILL_KEYS
COMMAND src/command/jc_command.c CMD_KEYS
STYLE src/command/jc_output_style.c STYLE_KEYS
EOF

kinds=$(sed -n '/enum jc_asset_kind {/,/}/p' "$AV_H" \
        | grep -oE 'JC_ASSET_[A-Z]+' | sort -u)
nkinds=$(printf '%s\n' "$kinds" | grep -c .)
if [ "$nkinds" -ge 3 ]; then
    t_ok "extracted $nkinds asset kinds from the enum (floor 3)"
else
    t_fail "only $nkinds asset kinds extracted -- the enum's shape moved; fix the extraction, not the floor"
fi

# --- 2: every mapped kind is a real enum member ------------------------------
bad=""
while read -r kind loader table; do
    [ -n "$kind" ] || continue
    printf '%s\n' "$kinds" | grep -q "^JC_ASSET_$kind$" || bad="$bad JC_ASSET_$kind"
done < "$tmp/map"
if [ -z "$bad" ]; then
    t_ok "every mapped asset kind exists in the enum"
else
    t_fail "mapped kind(s) missing from enum jc_asset_kind:$bad"
fi

# --- 3: every mapped kind owns a table AND a switch case ---------------------
# The case is the tripwire: without it, jc_assetval_check's `default:` validates
# the kind against CMD_KEYS and says nothing.
bad=""
while read -r kind loader table; do
    [ -n "$kind" ] || continue
    grep -q "const $table\[\] = {" "$AV_C" || bad="$bad $table(no-table)"
    grep -q "case JC_ASSET_$kind:" "$AV_C" || bad="$bad JC_ASSET_$kind(no-case)"
done < "$tmp/map"
if [ -z "$bad" ]; then
    t_ok "every mapped kind owns a *_KEYS table and a switch case"
else
    t_fail "missing table/case:$bad"
fi

# --- 4: table keys == loader keys, both directions ---------------------------
bad=""
while read -r kind loader table; do
    [ -n "$kind" ] || continue
    [ -f "$SMOKE_ROOT/$loader" ] || { bad="$bad $loader(missing-file)"; continue; }
    # Comment lines are dropped BEFORE extracting: these tables carry prose that
    # quotes key names ("M302 added \"style\""), and the first teeth run proved
    # the lint blind because of it -- deleting `style` from the array left the
    # word in the comment and the lint stayed green. Anchoring on code, not
    # prose, is the M369/M295 rule; `grep -v '\*'` drops the `/*`, ` *` and `*/`
    # lines (no key line in this file carries a `*`). A key smuggled onto a
    # commented line now fails LOUDLY as untabled rather than silently passing.
    sed -n "/const $table\[\] = {/,/};/p" "$AV_C" | grep -v '\*' \
        | grep -oE '"[a-z][a-z-]*"' | tr -d '"' | sort -u > "$tmp/tab.$kind"
    grep -oE 'jc_yaml_get[a-z_]*\([^,()]*, *"[a-z][a-z-]*"' "$SMOKE_ROOT/$loader" \
        | grep -oE '"[a-z][a-z-]*"' | tr -d '"' | sort -u > "$tmp/par.$kind"
    miss=$(comm -13 "$tmp/tab.$kind" "$tmp/par.$kind" | tr '\n' ' ')
    dead=$(comm -23 "$tmp/tab.$kind" "$tmp/par.$kind" | tr '\n' ' ')
    [ -z "$miss" ] || bad="$bad [$kind reads-but-untabled: $miss]"
    [ -z "$dead" ] || bad="$bad [$kind tabled-but-unread: $dead]"
done < "$tmp/map"
if [ -z "$bad" ]; then
    t_ok "every kind's table matches the keys its loader reads"
else
    t_fail "asset key drift:$bad"
fi

# --- 5: per-loader floor -- a reformat that hides the reads fails loudly -----
bad=""
while read -r kind loader table; do
    [ -n "$kind" ] || continue
    [ -f "$tmp/par.$kind" ] || continue
    n=$(grep -c . "$tmp/par.$kind")
    [ "$n" -ge 1 ] || bad="$bad $loader"
done < "$tmp/map"
if [ -z "$bad" ]; then
    t_ok "every loader yields at least one extracted key"
else
    t_fail "no keys extracted from:$bad (multi-line call? fix the extraction)"
fi

# --- 6: the matcher can miss (two-sided; the config_keys_lint rule) ----------
if grep -q '"zzz-invented-key"' "$AV_C"; then
    t_fail "an invented key is 'known' -- the table search is broken"
else
    t_ok "the matcher can miss: an invented key is not in any table"
fi

t_done
