#!/bin/sh
# smoke lint: every top-level config key jichi parses must be documented (M305).
#
# THE COVERAGE AUDIT, MECHANISED. The docs had been audited for numbers (M260) and
# for what the code comments claim (M261), but never for coverage -- which shipped
# capabilities have no page at all. Reading 286 pages to find out is the audit that
# "finds what it knew to look for"; comparing two lists is not.
#
# It found two undocumented keys, and one of them mattered: `systemPrompt` appends
# free text to the system prompt AND to every subagent's -- a shipped, session-wide
# way to shape what jichi is -- documented nowhere. A user who went looking for "how
# do I configure the system prompt" would have concluded they could not. (The other
# was `numberFormat`.)
#
# This is the fourth namespace in the chain: docs_flags.sh (flags),
# tool_names_lint.sh (tool names), builtin_cmds_lint.sh + slash_commands_lint.sh
# (slash commands), subcommands_lint.sh (subcommands), and now config keys.
#
# SCOPE: TOP-LEVEL keys read straight off `root`. Nested objects' INNER keys
# (`routing.*`, `sound.*`, `retrieval.*`, per-model keys) are read from
# sub-objects; check 6 covers those separately, with its own floor.
#
# M510: the extraction was HALF ITS OWN SCOPE. It matched only the scalar
# readers (`jc_json_get_bool|int|long|num|str|double`, optionally `_lenient`,
# and `jc_json_dup_str`), so
# the 23 keys read with `jc_json_get_obj(root, ...)` -- `models`, `hooks`,
# `permissions`, `routing`, `tools`, `sound`, `mcpServers`, `editScope` and the
# rest -- were outside the universe this check claimed to cover. 71 of 94. They
# are the container keys, i.e. the ones a config example is most likely to
# name, and an undocumented one would have passed in silence. All 94 were
# documented when the extraction was widened, so this cost nothing to fix and
# would have cost a shipped-but-undiscoverable key to leave. Found by auditing
# the OTHER direction (do documented config examples name keys that exist)
# and noticing the universe was too small to answer with.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
root="$SMOKE_ROOT"

# --- keys jichi actually parses ------------------------------------------------
grep -ohE 'jc_json_get_(bool|int|long|num|str|double)(_lenient)?\(root, "[A-Za-z][A-Za-z0-9]*"' \
    "$root/src/config/jc_config.c" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' | sort -u > "$tmp/parsed"
# jc_json_dup_str(root, "key", a) is a second shape (M263's formatCommand uses it).
grep -ohE 'jc_json_dup_str\(root, "[A-Za-z][A-Za-z0-9]*"' \
    "$root/src/config/jc_config.c" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' >> "$tmp/parsed"
# M510: and the CONTAINER shape -- an object- or array-valued top-level key is
# fetched whole and parsed by a helper. Same scope (read straight off root),
# same claim, and 23 keys that were missing from it.
grep -ohE '(jc_json_get_obj|cJSON_GetObjectItem)\(root, "[A-Za-z][A-Za-z0-9]*"' \
    "$root/src/config/jc_config.c" \
    | grep -oE '"[A-Za-z][A-Za-z0-9]*"' | tr -d '"' >> "$tmp/parsed"
sort -u "$tmp/parsed" > "$tmp/keys"
nk=$(grep -c . "$tmp/keys" || true)

# A shrinking extraction must fail LOUDLY: if a parse shape changes, the universe
# empties and this lint passes while checking nothing (the M285 discipline, and the
# empty-universe bug M302's own driver caught one milestone ago).
if [ "$nk" -ge 70 ]; then
    t_ok "extracted $nk top-level config keys from jc_config.c"
else
    t_fail "only $nk config keys extracted -- did a parse shape change? Fix the
 extraction; do not relax the floor"
fi

# --- keys the docs mention -----------------------------------------------------
: > "$tmp/undoc"
# M461: this used --exclude=/--exclude-dir=, which are GNU-only. BSD grep
# reads them as filenames, drops the filter, and -- inside this `!` -- turns
# its own error into "undocumented" for every key. The exclusions are the
# point of the check (a key mentioned once in a 12,000-line roadmap is not
# documented), so they move into find, where they are portable and visible.
find "$root/docs" -type f -name '*.md' \
     ! -name ROADMAP.md ! -name ANECDOTES.md ! -name DECISIONS.md \
     ! -name DEFERRED.md \
     ! -path "*/analysis/*" ! -path "*/proposals/*" \
     -exec cat {} + > "$tmp/reader_docs" 2>/dev/null
cat "$root/README.md" >> "$tmp/reader_docs" 2>/dev/null

while read -r k; do
    [ -z "$k" ] && continue
    # Word-boundaried, not a bare substring: `grep -F systemPrompt` also matches
    # `XsystemPromptX`, so a substring match would call a key documented because its
    # NAME appears inside a longer word. Found by trying to break this check and
    # watching it stay green.
    # M325: the HISTORY does not count. This searched all of docs/ including
    # ROADMAP.md, so a key mentioned once in a 12,000-line changelog passed as
    # "documented" -- which is not what the check claims ("a capability nobody can
    # find is a capability that was not shipped"). Nobody looks up a setting in a
    # roadmap, an anecdote or a post-hoc analysis. Found by hunting for
    # parallelTaskTimeout's documentation and discovering the lint had already
    # blessed its absence. The orphan-page check below always exempted
    # ROADMAP.md; this half did not, which is the inconsistency.
    if ! grep -qE "(^|[^A-Za-z0-9])$k([^A-Za-z0-9]|\$)" "$tmp/reader_docs" 2>/dev/null; then
        printf '%s\n' "$k" >> "$tmp/undoc"
    fi
done < "$tmp/keys"
nu=$(grep -c . "$tmp/undoc" 2>/dev/null || true)
[ -z "$nu" ] && nu=0

if [ "$nu" -eq 0 ]; then
    t_ok "every parsed config key appears somewhere in docs/ or README.md"
else
    t_fail "$nu config key(s) jichi parses are documented NOWHERE -- a shipped
 capability nobody can find is a capability that was not shipped:"
    sed 's/^/    | /' "$tmp/undoc"
fi

# --- the lint's own teeth ------------------------------------------------------
# A substring match is deliberately generous (a key may appear in prose, a table or
# an example), so prove it can still miss something: a name that is certainly not in
# the docs must be reported.
if ! grep -rqE "(^|[^A-Za-z0-9])definitelyNotAConfigKeyXyz([^A-Za-z0-9]|\$)" \
        "$root/docs" "$root/README.md" 2>/dev/null; then
    t_ok "the matcher can miss: an invented key is not found in the docs"
else
    t_fail "the doc matcher matches anything -- it would pass on any key"
fi

# And prove the OTHER direction is not vacuous: a key that IS documented must be
# found, or the check above would pass because the search is broken rather than
# because the docs are complete.
if grep -rqE "(^|[^A-Za-z0-9])autoCompact([^A-Za-z0-9]|\$)" "$root/docs" \
        2>/dev/null; then
    t_ok "the matcher finds a key that is genuinely documented (autoCompact)"
else
    t_fail "the doc search cannot even find autoCompact -- it is broken, so the
 coverage result above means nothing"
fi

# --- no orphaned documentation pages ------------------------------------------
# A page nothing links to is the same failure as an undocumented key one level up:
# the writing exists and nobody can find it. Measured before being made a check --
# exactly ONE orphan existed (the plain-language page added by this milestone, before
# it was linked), so the lint is quiet by construction rather than crying wolf about
# a pre-existing mess (the M295 rule). ROADMAP.md is exempt: it is the spine every
# other page points AT, not one that needs pointing to.
orph=""
for f in "$root"/docs/*.md; do
    b=$(basename "$f")
    [ "$b" = "ROADMAP.md" ] && continue
    if ! grep -rlF "$b" "$root"/docs/*.md "$root"/docs/*/*.md "$root/README.md" \
            "$root/CLAUDE.md" 2>/dev/null | grep -qv "^$f$"; then
        orph="$orph $b"
    fi
done
if [ -z "$orph" ]; then
    t_ok "every docs page is linked from somewhere a reader might start"
else
    t_fail "orphaned page(s) -- written, unfindable:$orph"
fi

# --- 6 (M371): NESTED key coverage -- closing this lint's own stated
# exclusion ("top-level keys only, nested objects excluded explicitly").
# Model entries, routing, mcpServers, aliases, timeouts, the posture blocks:
# all hand-edited by users, all parsed via jc_json_get_*(obj, "key"), none
# coverage-checked until now. Each parsed key must appear in docs/*.md as a
# backticked or quoted token -- anchored, because common-word keys ("value",
# "name") must never be satisfied by prose. The failure this prevents is a
# hidden knob: privilegedAudit -- the OFF-SWITCH of the always-on privileged
# audit log -- shipped undocumented, found by this check's first run.
grep -ohE 'jc_json_get_[a-z_]+\([^,]+, *"[a-zA-Z]+"' \
    "$SMOKE_ROOT/src/config/jc_config.c" \
    | grep -oE '"[a-zA-Z]+"' | tr -d '"' | sort -u > "$tmp/nested"
nn=$(grep -c . "$tmp/nested")
smoke_md_corpus "$tmp/nested_corpus" "$SMOKE_ROOT/docs"   # M461
undoc=""
while IFS= read -r k; do
    grep -q -e '`'"$k"'`' -e '"'"$k"'"' "$tmp/nested_corpus" 2>/dev/null \
        || undoc="$undoc $k"
done < "$tmp/nested"
if [ "$nn" -ge 110 ] && [ -z "$undoc" ]; then
    t_ok "all $nn parsed nested config keys are documented (floor 110)"
else
    t_fail "nested keys ($nn parsed) documented nowhere:$undoc"
fi

t_done
