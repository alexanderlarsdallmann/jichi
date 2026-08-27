#!/bin/sh
# smoke lint: docs/README.md lists every top-level doc, and lists nothing else
# (M487).
#
# WHAT THIS EXISTS FOR. 138 pages sat in docs/ with no index at all -- a stranger
# opening the directory met an alphabetical wall from ACCESSIBILITY.md to
# ZIG_REWRITE_ANALYSIS.md, with ROBOTICS_BRINGLIST.md at the same visual level as
# INSTALL.md. docs/DOCS.md is a name trap besides: it documents the search_docs
# feature, not the doc tree.
#
# But an index is worse than no index once it is stale, because it looks
# authoritative: a page missing from it is a page nobody finds, which is M297's
# sentence ("a feature nobody can discover was not shipped") applied to
# documentation. The orphan check in config_keys_lint answers a DIFFERENT
# question -- whether anything links to a page -- and a page linked only from
# one milestone's ROADMAP entry passes it while remaining unfindable.
#
# So this is a two-directional set comparison, the tool_names_lint shape:
#   every docs/*.md must appear in the index   (nothing unfindable)
#   every path the index names must exist      (no dead entry)
# with a floor under the extraction, because two empty sets agree perfectly.
. "$(dirname "$0")/_smoke.sh"

t_plan 4

IDX="$SMOKE_ROOT/docs/README.md"

if [ ! -f "$IDX" ]; then
    t_fail "docs/README.md is missing -- 138 pages with no map"
    t_fail "-"; t_fail "-"; t_fail "-"
    t_done
fi

tmp=$(smoke_tmp)

# Ground truth: the top-level pages, README itself excluded.
ls "$SMOKE_ROOT"/docs/*.md 2>/dev/null \
  | sed 's|.*/||; s|\.md$||' | grep -v '^README$' | sort > "$tmp/have"
# Claimed: every `FOO.md` link target in the index that is not a subdirectory.
grep -oE '\]\([A-Z0-9_]+\.md\)' "$IDX" \
  | sed 's|^](||; s|\.md)$||' | sort -u > "$tmp/listed"

_nhave=$(grep -c . < "$tmp/have")
_nlisted=$(grep -c . < "$tmp/listed")

# --- 1: the floor -- both extractions saw something -------------------------
if [ "$_nhave" -ge 50 ] && [ "$_nlisted" -ge 50 ]; then
    t_ok "comparing $_nlisted indexed entries against $_nhave pages on disk"
else
    t_fail "extraction broke (on disk $_nhave, indexed $_nlisted) -- two small sets agree for the wrong reason"
    t_fail "-"; t_fail "-"; t_fail "-"
    t_done
fi

# --- 2: nothing on disk is missing from the index ---------------------------
_missing=$(comm -23 "$tmp/have" "$tmp/listed" | tr '\n' ' ')
if [ -z "$_missing" ]; then
    t_ok "every top-level doc is in the index"
else
    t_fail "page(s) nobody can find from the index: $_missing -- add them to docs/README.md"
fi

# --- 3: nothing in the index is missing from disk ---------------------------
_dead=$(comm -13 "$tmp/have" "$tmp/listed" | tr '\n' ' ')
if [ -z "$_dead" ]; then
    t_ok "every entry in the index resolves to a page"
else
    t_fail "index names page(s) that do not exist: $_dead"
fi

# --- 4: the index states that the project record ships on purpose -----------
# The decision (2026-07-28) lived only in CLAUDE.md -- i.e. it was told to the
# agent and never to the reader, who then meets docs/analysis/ and reads a
# recorded mis-diagnosis as leaked internal residue rather than as the point.
if grep -qiE 'on purpose|deliberately' "$IDX" && grep -q 'analysis/' "$IDX"; then
    t_ok "the index tells the reader the project record ships deliberately"
else
    t_fail "docs/README.md does not say the analysis/plans/anecdotes record is published on purpose"
fi

t_done
