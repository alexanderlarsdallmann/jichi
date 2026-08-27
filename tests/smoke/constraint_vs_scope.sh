#!/bin/sh
# smoke: an INFERRED constraint must not override an EXPLICIT --edit-scope.
#
# The defect (DEFERRED.md, reproduced as probe P3 at M424): a run given
# `--edit-scope docs/PROBE.md` -- an operator declaring on the command line
# exactly which file is writable -- inferred `read-only` from the prose of the
# same request and then blocked `write_file` on that very file. The measured
# cost was 250,288 tokens and 11 model calls to produce 1 tool call, ending
# `budget_exhausted` with `no_changes: true` and `starved: true`. The journal
# recorded `edit_scope: 1` beside the constraint: both declarations in one
# file, disagreeing, with the guess winning.
#
# The shape is not exotic, it is the commonest useful one: "work read-only and
# write your findings to <file>". A read-only ANALYSIS whose only deliverable
# is a written report. jc_constraint.h already draws the distinction this needs
# -- AUTHORED is a policy the operator wrote, INFERRED is "a guess" scanned out
# of prose, and the header caps its blast radius for exactly that reason -- so
# a guess outranking a typed flag inverts the module's own principle.
#
# This driver holds BOTH halves: the inferred read-only still binds everywhere
# else (an out-of-scope path stays refused), and the operator's declared path
# goes through. Check 2 is the one that keeps the rest honest: if the scanner
# ever stops inferring here, the write would succeed for the wrong reason and
# every other check would pass vacuously.
. "$(dirname "$0")/_smoke.sh"

t_plan 9
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/docs"
: > "$ws/docs/PROBE.md"

# Two writes: the declared one, then one OUTSIDE the scope. The second is what
# proves the exemption is narrow rather than a blanket disarm.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"docs/PROBE.md","content":"FINDINGS_WRITTEN\n"}
rule
  count 2
  tool write_file {"path":"docs/OTHER.md","content":"SHOULD_NOT_LAND\n"}
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,
"maxRetries":0}
EOF

# The prose is an INSTRUCTION form the scanner recognises ("work read-only"),
# not the adjectival mention M168c deliberately ignores.
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session \
    --auto --edit-scope 'docs/PROBE.md' \
    -p "work read-only and summarise the tree, then write your findings to docs/PROBE.md" \
    < /dev/null > "$tmp/out" 2> "$tmp/err") || true
mm_stop

# --- 1: the run reached the model at all -------------------------------------
if [ -s "$tmp/out" ] || [ -s "$tmp/err" ]; then
    t_ok "the run produced output"
else
    t_fail "no output at all -- the run never started"
fi

# --- 2: the case is actually exercised ---------------------------------------
# Without this, a scanner change would make checks 3-5 pass for the wrong
# reason. The refusal we care about only exists if a read-only was inferred.
if grep -q 'constraint' "$tmp/err"; then
    t_ok "a constraint was inferred from the prose (the case is live)"
else
    t_fail "no constraint inferred -- this driver would be testing nothing"
fi

# --- 3: the operator's declared path was written -----------------------------
if grep -q 'FINDINGS_WRITTEN' "$ws/docs/PROBE.md" 2>/dev/null; then
    t_ok "the explicitly scoped file was written (the flag outranks the guess)"
else
    t_fail "--edit-scope named docs/PROBE.md and an INFERRED read-only still \
blocked it -- the guess outranked the operator's flag"
fi

# --- 4: ...and the exemption is NARROW ---------------------------------------
if [ ! -f "$ws/docs/OTHER.md" ]; then
    t_ok "an out-of-scope write is still refused (not a blanket disarm)"
else
    t_fail "docs/OTHER.md was written -- the exemption disarmed the constraint \
everywhere, not just on the declared path"
fi

# --- 5: the override is LOUD -------------------------------------------------
# Silently ignoring a constraint is the same class of defect as silently
# enforcing one: either way the operator cannot see which rule decided.
if grep -qi 'edit-scope' "$tmp/err"; then
    t_ok "the override names --edit-scope, so the decision is auditable"
else
    t_fail "the constraint was overridden with no mention of why: \
$(tail -c 200 "$tmp/err")"
fi

# --- 6: the run completed rather than thrashing ------------------------------
# The measured symptom was not only the refusal, it was 11 model calls spent
# hunting for another way through.
if grep -q 'DONE' "$tmp/out" 2>/dev/null; then
    t_ok "the run completed instead of hunting for a way around the refusal"
else
    t_fail "the run never reached its final answer: $(tail -c 200 "$tmp/out")"
fi

# ---- APPLY_PATCH: the same defect, in the tool a model actually reaches for ---
# M501. M459's exemption tested `write_file`/`edit_file` BY NAME, and
# `apply_patch` carries its paths in `edits[]` -- so a run told exactly which
# file to change still refused to change it through the multi-edit tool. Three
# checks: the in-scope patch lands, a patch with ANY out-of-scope path is
# refused whole (it is atomic, so partial exemption would be wrong), and the
# override is announced.
tmp2=$(smoke_tmp)
ws2=$(smoke_tmp)
mkdir -p "$ws2/docs"
printf 'ORIGINAL\n' > "$ws2/docs/PROBE.md"
printf 'ORIGINAL\n' > "$ws2/docs/OTHER.md"
cat > "$tmp2/replies.mm" <<'EOS'
wire openai
rule
  count 1
  tool read_file {"path":"docs/PROBE.md"}
rule
  count 2
  tool apply_patch {"edits":[{"path":"docs/PROBE.md","old_string":"ORIGINAL","new_string":"PATCHED_IN_SCOPE"}]}
rule
  count 3
  tool apply_patch {"edits":[{"path":"docs/PROBE.md","old_string":"PATCHED_IN_SCOPE","new_string":"X"},{"path":"docs/OTHER.md","old_string":"ORIGINAL","new_string":"SHOULD_NOT_LAND"}]}
rule
  text DONE
EOS
mm_start "$tmp2/replies.mm" "$tmp2"
cat > "$tmp2/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
"roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,
"maxRetries":0}
EOF
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp2/config.json" --no-session \
    --auto --edit-scope 'docs/PROBE.md' \
    -p "work read-only and summarise the tree, then write your findings to docs/PROBE.md" \
    < /dev/null > "$tmp2/out" 2> "$tmp2/err") || true
mm_stop

# --- 7: the in-scope multi-edit call lands -----------------------------------
if grep -q 'PATCHED_IN_SCOPE' "$ws2/docs/PROBE.md" 2>/dev/null; then
    t_ok "apply_patch on the explicitly scoped path is exempt too (M501)"
else
    t_fail "an INFERRED read-only blocked apply_patch on the very file \
--edit-scope named -- M459 fixed write_file/edit_file by NAME and this tool \
carries its paths in edits[]: $(head_bytes 120 "$ws2/docs/PROBE.md")"
fi

# --- 8: one out-of-scope path refuses the WHOLE atomic call ------------------
if ! grep -q 'SHOULD_NOT_LAND' "$ws2/docs/OTHER.md" 2>/dev/null; then
    t_ok "a patch touching any out-of-scope path is refused whole (atomic)"
else
    t_fail "an out-of-scope edit landed because a sibling edit was in scope -- \
the exemption must be all-or-nothing for an atomic tool"
fi

# --- 9: and the decision is auditable ---------------------------------------
if grep -q 'edit-scope' "$tmp2/err"; then
    t_ok "the apply_patch override names --edit-scope in the record"
else
    t_fail "the exemption fired silently for apply_patch: \
$(head_bytes 160 "$tmp2/err")"
fi

t_done
