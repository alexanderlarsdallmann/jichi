#!/bin/sh
# smoke: several design docs, from the config and the CLI together (M462).
#
# WHAT THIS PINS. v1 of --design took ONE document from ONE place. The three
# claims added here are each a decision that could reasonably have gone the
# other way, so each gets a check rather than a comment:
#
#   1. the CLI flag repeats;
#   2. a config `design: [...]` list is read;
#   3. the CLI APPENDS to that list rather than replacing it.
#
# (3) is the load-bearing one. Replacement is the conventional precedence and
# it fails silently here: a project that pins its architecture doc in config
# would lose it the moment anyone passed a task spec, while the prompt still
# carried a section labelled "authoritative for this task". A check that only
# proved the CLI doc was present would pass under either design -- so the check
# below requires BOTH, which is the only way to tell them apart.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

printf 'ARCHDOC_MARKER: the standing architecture.\n' > "$tmp/arch.md"
printf 'SPECDOC_MARKER: this task only.\n'            > "$tmp/spec.md"
printf 'THIRDDOC_MARKER: another one.\n'              > "$tmp/third.md"

cfg() {   # cfg FILE [design-json]
    {
        printf '{"models":[{"name":"m","provider":"openai","model":"x",'
        printf '"apiBase":"http://127.0.0.1:9/v1","apiKey":"k","roles":["chat"]}],'
        printf '"lowResource":false'
        [ -n "${2:-}" ] && printf ',"design":%s' "$2"
        printf '}\n'
    } > "$1"
}

# --- 1: one CLI doc still works, unchanged from v1 ---------------------------
cfg "$tmp/c.json"
out=$("$BIN" --config "$tmp/c.json" --design "$tmp/spec.md" sysmsg < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'SPECDOC_MARKER' &&
   printf '%s' "$out" | grep -q 'Design specification'; then
    t_ok "a single --design still lands in the prompt"
else
    t_fail "the v1 single-doc path regressed"
fi

# --- 2: a single doc is NOT given a filename heading -------------------------
# With one document a heading is noise. With several it is the only way the
# model can tell which plan a paragraph belongs to, so it appears only then.
if printf '%s' "$out" | grep -q '^## spec.md'; then
    t_fail "a lone design doc was given a redundant '## spec.md' heading"
else
    t_ok "a lone design doc carries no filename heading"
fi

# --- 3: the flag repeats, and each doc is named ------------------------------
out=$("$BIN" --config "$tmp/c.json" --design "$tmp/spec.md" \
      --design "$tmp/third.md" sysmsg < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'SPECDOC_MARKER' &&
   printf '%s' "$out" | grep -q 'THIRDDOC_MARKER' &&
   printf '%s' "$out" | grep -q '^## spec.md' &&
   printf '%s' "$out" | grep -q '^## third.md'; then
    t_ok "--design repeats, and each document is named by its basename"
else
    t_fail "a repeated --design lost a document or its heading"
fi

# --- 4: a config `design: [...]` list is read --------------------------------
cfg "$tmp/c2.json" "[\"$tmp/arch.md\"]"
out=$("$BIN" --config "$tmp/c2.json" sysmsg < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'ARCHDOC_MARKER'; then
    t_ok "a config design list is read with no CLI flag at all"
else
    t_fail "the config \`design\` key was ignored"
fi

# --- 5: THE ONE THAT MATTERS -- the CLI adds to the config, never replaces ---
out=$("$BIN" --config "$tmp/c2.json" --design "$tmp/spec.md" sysmsg \
      < /dev/null 2>&1)
if printf '%s' "$out" | grep -q 'ARCHDOC_MARKER' &&
   printf '%s' "$out" | grep -q 'SPECDOC_MARKER'; then
    t_ok "a CLI --design ADDS to the config list (the pinned doc survives)"
else
    t_fail "the CLI doc replaced the config doc -- a project's pinned \
architecture doc would vanish silently while the section still claimed to be \
authoritative. arch=$(printf '%s' "$out" | grep -c ARCHDOC_MARKER) \
spec=$(printf '%s' "$out" | grep -c SPECDOC_MARKER)"
fi

# --- 6: config-then-CLI order, so the task spec sits last --------------------
# Not cosmetic: the end of a section is the position a model weights most, and
# the task-specific document is the one that should hold it.
_a=$(printf '%s' "$out" | grep -n 'ARCHDOC_MARKER' | head -1 | cut -d: -f1)
_s=$(printf '%s' "$out" | grep -n 'SPECDOC_MARKER' | head -1 | cut -d: -f1)
if [ -n "$_a" ] && [ -n "$_s" ] && [ "$_a" -lt "$_s" ]; then
    t_ok "config docs come first, the CLI task spec last (line $_a < $_s)"
else
    t_fail "ordering wrong: config at $_a, CLI at $_s"
fi

# --- 7: the same doc named twice is paid for once ----------------------------
# Deduped by RESOLVED path, so a relative spelling of the same file collapses
# too -- the always-sent prefix re-bills on every call of the run.
out=$("$BIN" --config "$tmp/c2.json" --design "$tmp/arch.md" sysmsg \
      < /dev/null 2>&1)
_n=$(printf '%s' "$out" | grep -c 'ARCHDOC_MARKER')
if [ "$_n" -eq 1 ]; then
    t_ok "a document named in both config and CLI is included once"
else
    t_fail "the same design doc was included $_n times -- it re-bills on \
every model call of the run"
fi

t_done
