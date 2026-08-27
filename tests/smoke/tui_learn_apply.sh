#!/bin/sh
# smoke: `/learn apply` and `/learn corrections` in the TUI, on a real PTY
# (M293, M294).
#
# `learn apply` was CLI-only and jichi has no shell escape, so committing a
# reviewed draft meant leaving the session. That was not merely inconvenient:
# jc_memory_add does NOT refresh app->memory (the `remember` tool calls
# jc_memory_refresh itself), and a `learn apply` in a SECOND process cannot
# refresh this one at all. So the session went on serving notes a `## Corrections`
# section had already superseded, until it was restarted.
#
# The load-bearing assertions here are therefore the LIVE ones: after /learn
# apply, this same session's /memory must show the new note and must no longer
# show the corrected-away one. Checking only the file on disk would pass even with
# the refresh deleted, which is the whole defect.
#
# PHASES A AND B ARE NOT REDUNDANT, and the split was forced by watching this
# driver stay green with jc_learn_apply's refresh deleted. jc_memory_correct
# refreshes app->memory ITSELF, so a draft containing corrections reloads the file
# -- new memory notes included -- no matter what jc_learn_apply does. Only a
# memory-notes-ONLY draft (phase A) exercises the refresh this milestone added,
# because jc_memory_add is the one that does not do it. Phase B then covers the
# correction path and the rest of the draft; phase C (M294) the narrow
# `/learn corrections` command over the same machinery.
#
# Two PTY rules the tier learned the hard way: the expect after a send must be a
# string that is NOT already in the startup banner, and sends need human-scale
# gaps or the M156 burst-paste window merges them into one logical line.
. "$(dirname "$0")/_smoke.sh"

t_plan 13
smoke_home

# --- phase A: memory notes only -- isolates jc_learn_apply's own refresh -------
a=$(smoke_tmp)
write_config "$a/config.json" 9
mkdir -p "$a/.jichi"
cat > "$a/.jichi/memory.md" <<'EOF'
- a note that was here before the apply
EOF
cat > "$a/.jichi/lessons.draft.md" <<'EOF'
## Memory notes
- always build with WERROR=1 before committing [evidence: 3 build breaks]
EOF
cat > "$a/apply.pd" <<EOF
expect "] " 20
delay 400
send "/learn apply\r"
expect "Applied" 25
delay 600
send "/memory\r"
expect "WERROR=1" 25
delay 600
send "/exit\r"
waitexit 15
EOF
if (cd "$a" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --log "$a/pty.log" \
        "$a/apply.pd" -- "$BIN" --config "$a/config.json" --no-lite) \
        > "$a/drive.out" 2>&1; then
    t_ok "a note added by /learn apply is live in the SAME session"
else
    t_fail "a note added by /learn apply is live in the SAME session"
    sed 's/^/    | /' "$a/drive.out"
fi

# The file must be right too -- if it were not, the live check above would be
# proving nothing about the refresh.
if grep -q "always build with WERROR=1" "$a/.jichi/memory.md" &&
   grep -q "a note that was here before" "$a/.jichi/memory.md"; then
    t_ok "the note was appended to memory.md without losing the existing one"
else
    t_fail "the note was appended to memory.md without losing the existing one"
fi

# --- phase B: corrections, skills, and the human's section --------------------
tmp=$(smoke_tmp)

write_config "$tmp/config.json" 9

mkdir -p "$tmp/.jichi"

# A note that has since become false -- the mentor's `## Corrections` section
# exists for exactly this, and it is what the operator wants when memory.md has
# outgrown the injection budget: retract, not append.
cat > "$tmp/.jichi/memory.md" <<'EOF'
- ResourceCache uses AutoHasher for its key type
- unrelated note that must survive
EOF

cat > "$tmp/.jichi/lessons.draft.md" <<'EOF'
# Lessons draft

## Memory notes
- always build with WERROR=1 before committing [evidence: 3 build breaks]

## Skills
### draft triage: curate a mentor draft into the strict format
1. keep the lessons worth keeping
2. delete the rest

## Corrections
- remove: ResourceCache uses AutoHasher

## Suggested (manual)
- raise timeouts.stall to 60
EOF

# /memory first (proving the stale note IS loaded at startup), then apply, then
# /memory again -- the second one is the milestone's claim.
cat > "$tmp/apply.pd" <<EOF
expect "] " 20
delay 400
send "/memory\r"
expect "AutoHasher" 25
delay 600
send "/learn apply\r"
expect "Applied" 25
delay 600
send "/memory\r"
expect "WERROR=1" 25
delay 600
send "/skills\r"
expect "curate a mentor draft" 25
delay 600
send "/exit\r"
waitexit 15
EOF

if (cd "$tmp" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --log "$tmp/pty.log" \
        "$tmp/apply.pd" -- "$BIN" --config "$tmp/config.json" --no-lite) \
        > "$tmp/drive.out" 2>&1; then
    t_ok "TUI /learn apply runs and the applied note is live in /memory"
else
    t_fail "TUI /learn apply runs and the applied note is live in /memory"
    sed 's/^/    | /' "$tmp/drive.out"
fi

# The shared summary renderer's line, so the TUI reports the same numbers the CLI
# does rather than counting for itself.
if grep -aq "Applied 1 memory note(s), 1 skill(s), 1 correction(s), and 0 " \
    "$tmp/pty.log"; then
    t_ok "the TUI renders the shared summary line with the real counts"
else
    t_fail "the TUI renders the shared summary line with the real counts"
    grep -a "Applied" "$tmp/pty.log" | sed 's/^/    | /'
fi

# THE assertion. The stale BULLET was printed once by the first /memory; if the
# correction reached this session's app->memory, the second /memory cannot print
# it again. Without jc_memory_refresh the count is 2 -- the file on disk is right
# and the live session is wrong, precisely the bug a second process cannot fix.
#
# Match the note's tail ("for its key type"), not the bare substring: the
# correction's own detail line quotes the MATCH ("ResourceCache uses AutoHasher"),
# so counting that would find two hits with the refresh working perfectly. This
# fixture was wrong that way on its first run.
n=$(grep -ac "AutoHasher for its key type" "$tmp/pty.log")
if [ "$n" -eq 1 ]; then
    t_ok "the correction took effect in the LIVE session (no restart)"
else
    t_fail "stale note still served after apply: AutoHasher seen $n times"
    sed 's/^/    | /' "$tmp/pty.log"
fi

# A correction must not be a blunt instrument: the unrelated note stays.
if grep -q "unrelated note that must survive" "$tmp/.jichi/memory.md"; then
    t_ok "an unrelated memory note survived the correction"
else
    t_fail "an unrelated memory note survived the correction"
fi

if [ -s "$tmp/.jichi/skills/draft-triage/SKILL.md" ] &&
   grep -q "keep the lessons worth keeping" \
        "$tmp/.jichi/skills/draft-triage/SKILL.md"; then
    t_ok "the skill was written to .jichi/skills/<slug>/SKILL.md"
else
    t_fail "the skill was written to .jichi/skills/<slug>/SKILL.md"
fi

# The other half of "reload in place": a freshly written SKILL.md is invisible to
# the catalog (and so to the model) until jc_skill_load runs again. The /skills
# expect above already gates the run, so this is the explicit statement of why.
if grep -aq "curate a mentor draft" "$tmp/pty.log"; then
    t_ok "the new skill is in the LIVE catalog (/skills), not just on disk"
else
    t_fail "the new skill is in the LIVE catalog (/skills), not just on disk"
fi

# The 'Suggested (manual)' section is the human's; it must never be committed.
if ! grep -rq "timeouts.stall" "$tmp/.jichi/memory.md" \
        "$tmp/.jichi/skills" 2>/dev/null; then
    t_ok "the 'Suggested (manual)' section was not committed"
else
    t_fail "the 'Suggested (manual)' section was committed"
fi

# --- phase C: /learn corrections in the TUI (M294) -----------------------------
#
# The narrow command, on the surface where the situation that calls for it shows
# up: memory.md has outgrown the 8 KB injection budget, so the need is to retract
# stale notes now and leave the additions for later. The live check is the same as
# phase B's -- and here it is jc_memory_correct's own refresh doing the work, which
# is exactly why phase A exists separately.
c=$(smoke_tmp)
write_config "$c/config.json" 9
mkdir -p "$c/.jichi"
cat > "$c/.jichi/memory.md" <<'EOF'
- ResourceCache uses AutoHasher for its key type
EOF
cat > "$c/.jichi/lessons.draft.md" <<'EOF'
## Memory notes
- an addition that must wait for a full apply
## Corrections
- remove: ResourceCache uses AutoHasher
EOF
cat > "$c/corr.pd" <<EOF
expect "] " 20
delay 400
send "/learn corrections\r"
expect "Applied 1 correction(s)" 25
delay 600
send "/memory\r"
expect "] " 25
delay 600
send "/exit\r"
waitexit 15
EOF
if (cd "$c" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --log "$c/pty.log" \
        "$c/corr.pd" -- "$BIN" --config "$c/config.json" --no-lite) \
        > "$c/drive.out" 2>&1; then
    t_ok "TUI /learn corrections applies only the corrections"
else
    t_fail "TUI /learn corrections applies only the corrections"
    sed 's/^/    | /' "$c/drive.out"
fi

# The pending memory note must be reported, not silently deferred, and must not
# have been written.
if grep -aq "1 other draft item(s) not applied" "$c/pty.log" &&
   ! grep -q "an addition that must wait" "$c/.jichi/memory.md"; then
    t_ok "the deferred memory note is reported and not committed"
else
    t_fail "the deferred memory note is reported and not committed"
fi

# --force is meaningless for a corrections-only run, so it is refused rather than
# silently accepted -- accepting it would teach the wrong model of the command.
cat > "$c/badopt.pd" <<EOF
expect "] " 20
delay 400
send "/learn corrections --force\r"
expect "no options" 25
delay 600
send "/exit\r"
waitexit 15
EOF
if (cd "$c" && "$SMOKE_TOOLS/ptydrive" --deadline 90 --log "$c/pty3.log" \
        "$c/badopt.pd" -- "$BIN" --config "$c/config.json" --no-lite) \
        > "$c/drive3.out" 2>&1; then
    t_ok "/learn corrections --force is refused, not silently ignored"
else
    t_fail "/learn corrections --force is refused, not silently ignored"
    sed 's/^/    | /' "$c/drive3.out"
fi

# No draft is an actionable message naming the path, not a silent no-op. Run in a
# fresh workspace so there is nothing to apply.
nows=$(smoke_tmp)
cat > "$tmp/nodraft.pd" <<EOF
expect "] " 20
delay 400
send "/learn apply\r"
expect "lessons.draft.md" 25
delay 600
send "/exit\r"
waitexit 15
EOF
if (cd "$nows" && "$SMOKE_TOOLS/ptydrive" --deadline 90 \
        --log "$tmp/pty2.log" "$tmp/nodraft.pd" -- \
        "$BIN" --config "$tmp/config.json" --no-lite) \
        > "$tmp/drive2.out" 2>&1 &&
   grep -aq "draft lessons first with /learn" "$tmp/pty2.log"; then
    t_ok "no draft names the path and points at /learn"
else
    t_fail "no draft names the path and points at /learn"
    sed 's/^/    | /' "$tmp/drive2.out"
fi

t_done
