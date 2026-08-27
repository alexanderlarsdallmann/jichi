#!/bin/sh
# smoke: the learning loop's offline halves (M70) -- `learn analyze`
# surfaces a low-ok-rate tool + a model stall from synthesized telemetry;
# `learn apply` commits a draft's memory note + skill (never the
# "Suggested (manual)" section) and dedups on re-apply.
# (Port of tests/e2e/learn.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 17
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

tdir="$HOME/.jichi.d/telemetry"
mkdir -p "$tdir"
cat > "$tdir/run.jsonl" <<'EOF'
{"v":1,"event":"tool_call","name":"run_terminal_command","ok":false}
{"v":1,"event":"tool_call","name":"run_terminal_command","ok":false}
{"v":1,"event":"tool_call","name":"run_terminal_command","ok":false}
{"v":1,"event":"tool_call","name":"run_terminal_command","ok":true}
{"v":1,"event":"model_call","model":"M","ok":false,"result":"timeout"}
{"v":1,"event":"model_call","model":"M","ok":false,"result":"timeout"}
EOF

(cd "$ws" && with_deadline 30 "$BIN" learn analyze "$tdir/run.jsonl" \
    < /dev/null > "$tmp/an" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "run_terminal_command" "$tmp/an"; then
    t_ok "analyze names the failing tool"
else
    t_fail "analyze rc=$rc or tool not named"
fi
if grep -q "stalled" "$tmp/an" || grep -i -q "timeout" "$tmp/an"; then
    t_ok "analyze reports the model stall"
else
    t_fail "no stall report in analyze output"
fi

# M600: with a memory file in the workspace, analyze states the injection budget
# as a fraction, the pinned share, and any cited path that no longer resolves.
# The three notes: one pinned to a real driver, one naming a path that does not
# exist here, one plain prose. Measured motive: a project at 8,092 of 8,192
# bytes, one note from silently losing its oldest lesson, and nothing said so.
mkdir -p "$ws/.jichi" "$ws/tests/smoke"
: > "$ws/tests/smoke/known.sh"
cat > "$ws/.jichi/memory.md" <<'EOF'
- floor the extraction [pins: tests/smoke/known.sh]
- the allocator in src/gone/alloc.c leaks
- prefer small commits
EOF
(cd "$ws" && with_deadline 30 "$BIN" learn analyze "$tdir/run.jsonl" \
    --workspace "$ws" < /dev/null > "$tmp/an2" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q "Memory: .* of 8192 bytes injected" "$tmp/an2"; then
    t_ok "analyze states the memory injection budget as a fraction"
else
    t_fail "no memory budget line (rc=$rc): $(head_bytes 300 "$tmp/an2")"
fi
if grep -q "1 of 3 remembered note(s) are pinned" "$tmp/an2"; then
    t_ok "analyze states the pinned share (1 of 3)"
else
    t_fail "no pinned-share line: $(head_bytes 300 "$tmp/an2")"
fi
if grep -q "src/gone/alloc.c" "$tmp/an2" && ! grep -q "known.sh.*no longer" "$tmp/an2"; then
    t_ok "analyze names the cited path that does not resolve, and only that one"
else
    t_fail "unresolved-path finding wrong: $(head_bytes 300 "$tmp/an2")"
fi

mkdir -p "$ws/.jichi"
cat > "$ws/.jichi/lessons.draft.md" <<'EOF'
## Memory notes
- prefer apply_patch over many edits [evidence: redo]
## Skills
### build check: verify before commit
1. make WERROR=1 test
## Suggested (manual)
- raise timeouts.stall
EOF

(cd "$ws" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/ap1" 2>&1); rc=$?
mem="$ws/.jichi/memory.md"
if [ $rc -eq 0 ] && [ -f "$mem" ] && grep -q "prefer apply_patch" "$mem"; then
    t_ok "apply commits the memory note"
else
    t_fail "apply rc=$rc; memory note missing"
fi

skill="$ws/.jichi/skills/build-check/SKILL.md"
if [ -f "$skill" ] && grep -q "make WERROR=1 test" "$skill"; then
    t_ok "apply commits the skill body"
else
    t_fail "skill missing or empty"
fi
if [ -f "$skill" ] && ! grep -q "raise timeouts.stall" "$skill"; then
    t_ok "the 'Suggested (manual)' section did not leak into the skill"
else
    t_fail "suggested section leaked into the skill"
fi

(cd "$ws" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/ap2" 2>&1)
if [ "$(grep -c "prefer apply_patch" "$mem")" -eq 1 ]; then
    t_ok "memory note deduped on re-apply"
else
    t_fail "memory note duplicated on re-apply"
fi
if grep -q "Applied 0 memory note(s), 0 skill(s), 0 correction(s), and 0 " \
    "$tmp/ap2"; then
    t_ok "re-apply is a clean no-op"
else
    t_fail "re-apply not a no-op: $(head_bytes 200 "$tmp/ap2")"
fi


# --- `learn corrections` (M294) ------------------------------------------------
#
# Two shipped warnings told users to run this command for months before it
# existed (M292 retired the wrong advice; M294 makes the named operation real).
# It applies ONLY the draft's `## Corrections` section, which is what the
# situation printing those warnings calls for: memory.md has outgrown the 8 KB
# injection budget, so the need is to RETRACT stale notes, not to add more.
cws=$(smoke_tmp)
mkdir -p "$cws/.jichi"
cat > "$cws/.jichi/memory.md" <<'EOF'
- ResourceCache uses AutoHasher for its key type
- a note that must survive
EOF
cat > "$cws/.jichi/lessons.draft.md" <<'EOF'
## Memory notes
- a brand new note that must NOT be committed by `learn corrections`
## Skills
### unwanted skill: must not be written by a corrections-only run
1. nope
## Corrections
- remove: ResourceCache uses AutoHasher
EOF

(cd "$cws" && with_deadline 30 "$BIN" learn corrections \
    < /dev/null > "$tmp/co" 2>&1); rc=$?

if [ $rc -eq 0 ] && grep -q "Applied 1 correction(s) from" "$tmp/co"; then
    t_ok "learn corrections applies the correction and reports only that"
else
    t_fail "learn corrections rc=$rc: $(head_bytes 200 "$tmp/co")"
fi

# The mask must actually mask. A corrections-only run that quietly committed the
# memory notes and skills too would be indistinguishable from `learn apply`.
if ! grep -q "brand new note" "$cws/.jichi/memory.md" &&
   [ ! -e "$cws/.jichi/skills/unwanted-skill" ]; then
    t_ok "the memory notes and skills were NOT committed"
else
    t_fail "corrections-only run committed sections outside its mask"
fi

if ! grep -q "AutoHasher" "$cws/.jichi/memory.md" &&
   grep -q "a note that must survive" "$cws/.jichi/memory.md"; then
    t_ok "the stale note was retracted and the unrelated one kept"
else
    t_fail "correction did not retract exactly one note"
fi

# A partial apply must say what it left, or the user reads "Applied 1
# correction(s)" as the whole draft having been committed. Two items are pending
# here: one memory note and one skill.
if grep -q "2 other draft item(s) not applied" "$tmp/co" &&
   grep -q "learn apply" "$tmp/co"; then
    t_ok "the pending rest of the draft is reported, naming learn apply"
else
    t_fail "pending draft items not reported: $(head_bytes 200 "$tmp/co")"
fi

# Re-running is safe: the note is already gone, so the directive no longer
# matches. That is expected on a second pass, so the message must not read as a
# broken draft -- it names the likely cause.
(cd "$cws" && with_deadline 30 "$BIN" learn corrections \
    < /dev/null > "$tmp/co2" 2>&1)
if grep -q "already applied" "$tmp/co2"; then
    t_ok "an already-applied correction says so rather than looking broken"
else
    t_fail "re-run message unclear: $(head_bytes 200 "$tmp/co2")"
fi

# A full `learn apply` still commits everything, so `corrections` narrowed the
# run without disabling anything.
(cd "$cws" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/co3" 2>&1)
if grep -q "brand new note" "$cws/.jichi/memory.md" &&
   [ -s "$cws/.jichi/skills/unwanted-skill/SKILL.md" ]; then
    t_ok "a later learn apply still commits the rest of the draft"
else
    t_fail "learn apply did not commit the remaining sections"
fi

# And a full apply leaves nothing pending, so it must not print the hint.
if ! grep -q "not applied by this command" "$tmp/co3"; then
    t_ok "a full apply does not print the pending-items hint"
else
    t_fail "full apply printed a pending hint: $(head_bytes 200 "$tmp/co3")"
fi

t_done
