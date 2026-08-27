#!/bin/sh
# smoke: a `## Corrections` directive retracts a learned convention from the rules
# file (M601), and only from the "## Learned conventions" section.
#
# THE STORE THAT COULD ONLY GROW. `## Project rules` has appended to AGENTS.md
# under "## Learned conventions" since M106; nothing could take one back --
# Corrections acted on memory.md only. The 2026-08-27 analysis named it as the
# loop's one append-only store, and 守破離 asks the student to leave the form,
# which a store that cannot forget does not permit. M533 is what one careless
# write to that file did to this repository's own rules.
#
# The checks read the FILE back: the convention is gone, the hand-written rule
# above the heading is untouched, and a directive that matched a rule but no
# memory note is not reported as "unmatched".
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

mkdir -p "$ws/.jichi"
printf '# Project rules\n- Hand-written: never sprintf; this line mentions whole-file writes too.\n' \
    > "$ws/AGENTS.md"
cat > "$ws/.jichi/lessons.draft.md" <<'DRAFT'
## Project rules
- Prefer whole-file writes over apply_patch in this repo.
DRAFT
(cd "$ws" && with_deadline 30 "$BIN" learn apply < /dev/null > "$tmp/ap" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "## Learned conventions" "$ws/AGENTS.md" && \
   grep -q "Prefer whole-file writes" "$ws/AGENTS.md"; then
    t_ok "apply appended the convention under '## Learned conventions'"
else
    t_fail "apply rc=$rc; AGENTS.md: $(head_bytes 200 "$ws/AGENTS.md")"
fi

cat > "$ws/.jichi/lessons.draft.md" <<'DRAFT'
## Corrections
- remove: whole-file writes
DRAFT
(cd "$ws" && with_deadline 30 "$BIN" learn corrections < /dev/null > "$tmp/co" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "1 learned convention(s) retracted" "$tmp/co"; then
    t_ok "learn corrections reports the retracted convention"
else
    t_fail "rc=$rc: $(head_bytes 300 "$tmp/co")"
fi
if ! grep -q "Prefer whole-file writes" "$ws/AGENTS.md" && \
   grep -q "Hand-written: never sprintf; this line mentions whole-file writes too." "$ws/AGENTS.md"; then
    t_ok "the convention is gone; the hand-written rule ABOVE the heading (same substring) is untouched"
else
    t_fail "AGENTS.md after retraction: $(head_bytes 300 "$ws/AGENTS.md")"
fi
if ! grep -q "no note matches" "$tmp/co"; then
    t_ok "a rule-only match is not reported as an unmatched correction"
else
    t_fail "reported unmatched despite retracting a convention: $(head_bytes 200 "$tmp/co")"
fi

t_done
