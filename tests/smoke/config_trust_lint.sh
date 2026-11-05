#!/bin/sh
# smoke lint: every config key the loader reads is classified for the project-
# config trust step, and every classified key is one the loader reads (M851,
# CFG-1; the operator's decision of 2026-10-01: "a lint holds the
# classification, so a new key cannot slip through unclassified").
#
# WHY BOTH DIRECTIONS. A key the loader reads but the table does not know is
# ignored from an untrusted file (the filter fails closed) -- safe, but a SAFE key
# would silently stop applying from project files, and nobody would know why.
# A table entry the loader no longer reads is dead weight that reads as a
# decision. Either drift is a review someone skipped; this makes it a build.
#
# UNIVERSE: the TOP-LEVEL keys read straight off `root` in src/config/jc_config.c,
# by the same extraction config_keys_lint.sh uses (its M510 lesson: all the
# reader families, not just the scalar ones); and the keys of the CLASSES table
# in src/config/jc_config_trust.c. Both floored at today's exact counts, so an
# extraction that silently reads nothing cannot pass.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
CFG="$SMOKE_ROOT/src/config/jc_config.c"
TRUST="$SMOKE_ROOT/src/config/jc_config_trust.c"

grep -ohE 'jc_json_get_(bool|int|long|num|str|double)(_lenient)?\(root, "[A-Za-z][A-Za-z0-9]*"' "$CFG" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' > "$tmp/read"
grep -ohE 'jc_json_dup_str\(root, "[A-Za-z][A-Za-z0-9]*"' "$CFG" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' >> "$tmp/read"
grep -ohE '(jc_json_get_obj|cJSON_GetObjectItem)\(root, "[A-Za-z][A-Za-z0-9]*"' "$CFG" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' >> "$tmp/read"
sort -u "$tmp/read" > "$tmp/read.u"
# the table: lines like `    { "key", JC_CFGTRUST_...`
grep -oE '^ *\{ "[A-Za-z][A-Za-z0-9]*", *JC_CFGTRUST_' "$TRUST" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' | sort -u > "$tmp/classified"

n_read=$(wc -l < "$tmp/read.u" | tr -d ' ')
n_class=$(wc -l < "$tmp/classified" | tr -d ' ')

# 1-2: the extractions read something (floors at today's counts: 100 and 100)
if [ "$n_read" -ge 100 ]; then
    t_ok "the loader's top-level keys extract ($n_read; floor 100)"
else
    t_fail "only $n_read top-level keys extracted from jc_config.c -- fix the
 extraction, not the floor"
fi
if [ "$n_class" -ge 100 ]; then
    t_ok "the trust table's keys extract ($n_class; floor 100)"
else
    t_fail "only $n_class keys extracted from the CLASSES table -- fix the
 extraction, not the floor"
fi

# 3: every key the loader reads is classified
unclassified=$(comm -23 "$tmp/read.u" "$tmp/classified" | tr '\n' ' ')
if [ -z "$unclassified" ]; then
    t_ok "every config key the loader reads is classified for the trust step"
else
    t_fail "config key(s) the loader reads but the trust table does not classify
 (an untrusted project file would silently drop them): $unclassified
 -- add each to CLASSES in src/config/jc_config_trust.c, SAFE, NARROW or GATED"
fi

# 4: every classified key is one the loader reads
stale=$(comm -13 "$tmp/read.u" "$tmp/classified" | tr '\n' ' ')
if [ -z "$stale" ]; then
    t_ok "every classified key is one the loader reads"
else
    t_fail "trust-table entr(y|ies) the loader no longer reads: $stale"
fi

t_done
