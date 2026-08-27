#!/bin/sh
# smoke: a `## Checks` lesson becomes an AUTHORED constraint on `learn apply`, and
# the next run is refused by it (M602).
#
# THE SEAM THIS CLOSES. Everything the learning loop could produce was prose -- a
# memory bullet, a skill, a rule -- the tier this project's own record ranks as
# "necessary, demonstrably insufficient" (docs/analysis/2026-08-22-learning-from-
# errors.md). jichi has exactly one bridge from a sentence to a fence:
# jc_constraint_scan, which turns "do not run the build" into a refusal at the
# tool gate, re-injected every turn. The mentor may now propose into it under
# `## Checks`, and `learn apply` -- a human's action, so propose-only stays --
# commits the phrase as an AUTHORED constraint, the same call `/constraints add`
# makes.
#
# Check 3 reads the EFFECT, not the file: a later run whose model calls `make`
# gets "blocked by an active constraint" in the request that carries the tool
# result. A phrase the scanner cannot read and a `hook:` bullet are each COUNTED
# and named (check 2), never guessed at -- a fence that "roughly" matches is the
# M167d/M168c misparse class.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

mkdir -p "$ws/.jichi"
cat > "$ws/.jichi/lessons.draft.md" <<'DRAFT'
## Memory notes
- the build takes minutes; never trigger it from the agent [pins: constraint]
## Checks
- constraint: do not run the build
- constraint: please be careful with the database
- hook: PreToolUse make -> exit 2
DRAFT

(cd "$ws" && with_deadline 30 "$BIN" learn apply < /dev/null > "$tmp/ap" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "1 check(s) from" "$tmp/ap"; then
    t_ok "apply committed the one readable check"
else
    t_fail "apply rc=$rc: $(head_bytes 300 "$tmp/ap")"
fi
if grep -q "1 check(s) skipped: the constraint scanner" "$tmp/ap" && \
   grep -q "1 check(s) of a kind apply cannot commit" "$tmp/ap"; then
    t_ok "the unreadable phrase and the hook kind are each named, not dropped"
else
    t_fail "skip lines missing: $(head_bytes 300 "$tmp/ap")"
fi

# --- 3: the EFFECT -- a later run's build command is refused by the gate ------
cat > "$tmp/r.mm" <<'MM'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"make"}
rule
  text DONE
MM
mm_start "$tmp/r.mm" "$tmp/cap"
write_config "$tmp/c.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c.json" --no-session --auto \
    -p "build it" < /dev/null > "$tmp/out" 2>"$tmp/err")
mm_stop
if [ -f "$tmp/cap/req.2" ] && grep -q "blocked by an active constraint" "$tmp/cap/req.2"; then
    t_ok "the next run's build command was refused by the authored constraint"
else
    t_fail "no refusal reached the model: $(head_bytes 300 "$tmp/err")"
fi
# The store holds directives, not the prose that produced them (CONSTRAINTS.md
# §"The store"): `deny-cmd build` is what "do not run the build" became.
if [ -f "$ws/.jichi/constraints.md" ] && grep -q "deny-cmd build" "$ws/.jichi/constraints.md"; then
    t_ok "the constraint persisted as AUTHORED in .jichi/constraints.md (deny-cmd build)"
else
    t_fail "constraints.md missing or without deny-cmd build: $(head_bytes 200 "$ws/.jichi/constraints.md" 2>/dev/null)"
fi

t_done
