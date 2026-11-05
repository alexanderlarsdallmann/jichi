#!/bin/sh
# smoke: D-18 (M1104) -- jichi's OWN workspace files are rewritten IN PLACE via
# jc_write_file (fopen "wb"), so a failed write TRUNCATES the pre-existing
# content to 0 bytes while the command exits 0. The four in-place rewrites of
# pre-existing content:
#   jc_memory.c:325 (jc_memory_add)        .jichi/memory.md
#   jc_memory.c:454 (jc_memory_correct)    .jichi/memory.md
#   jc_learn.c:760  (learn_rules_apply)    AGENTS.md / CLAUDE.md
#   jc_learn.c:850  (learn_apply_rules)    AGENTS.md / CLAUDE.md
# (PLAN.md at jc_tool_plan.c:120 is already atomic from M990 -- out of scope.)
#
# The fix replaces each in-place write with a MODE-PRESERVING atomic replace
# (jc_write_file_atomic_mode): write a same-directory temp, fchmod it to the
# target's CURRENT mode (0644 when the target is genuinely absent), rename over
# the target. A failed write then leaves the pre-existing bytes untouched, and
# a SUCCESSFUL write keeps the file's original permission bits (the old in-place
# code happened to preserve mode only because fopen "wb" on an EXISTING file
# does not chmod -- the fix makes that guarantee explicit and extends it to the
# absent-target creation case).
#
# A real `jichi learn apply` (offline, no model, < /dev/null) is the vehicle:
# with a draft carrying BOTH a new memory note and a new project rule, the
# FIRST jc_write_file call in the flow is the memory add (jc_memory_add), and
# the LATER call is the rules write (learn_apply_rules). The one-shot knob
# JICHI_FAULT_WRITE_ONCE_AT=0 (src/util/jc_fault.c) faults exactly that first
# call -- a genuine failed-PRIMARY, not a blanket "every write fails", because
# the later rules write still lands.
#
#   FAULT ARM (a fresh workspace, pre-seeded non-empty memory.md + AGENTS.md):
#     run `learn apply` with JICHI_FAULT_WRITE_ONCE_AT=0.
#       check 1 (THE RED ONE): memory.md STILL contains its pre-existing
#               sentinel line -- under the old in-place code the memory write
#               truncates memory.md to 0 bytes, so this fails on the unfixed
#               tree and passes on the fixed one.
#       check 2: the rules file GOT its new rule -- proving the LATER write
#               landed (failed-primary-not-recovery, not every-write-fails).
#       check 3: the file the failed write targeted (memory.md) is UNCHANGED
#               -- byte-for-byte identical to the pre-seed (nothing appended).
#       check 4: no *.tmp<pid> leftover in the workspace -- the atomic write
#               cleaned up its temp on the failed path (guards the fix, which
#               is the first to create temps in this flow).
#   NO-FAULT ARM (a fresh workspace, same pre-seed + draft):
#     run `learn apply` WITHOUT the fault env.
#       check 5: BOTH the memory note and the rule land (append, not replace).
#       check 6: memory.md keeps its ORIGINAL permission bits (0644 pre-seed).
#       check 7: AGENTS.md keeps its ORIGINAL permission bits (0640 pre-seed).
#
# t_plan 7 (fault arm is 4, no-fault arm is 3). RED-FIRST on the unfixed tree:
# checks 1 and 3 go red (memory.md truncated to 0 bytes); checks 2, 4 (fault arm)
# and 5, 6, 7 (no-fault arm) stay green.

. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- the reliable probe
# (the write_fail_restores.sh idiom). Must precede t_plan (M672).
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

# Portable mode read (the state_dir_private / parallel_symlink_merge idiom):
# `ls -l | cut -c1-10` yields the 10-char mode string (stat -c is banned by
# posix_utils_lint). 0644 = -rw-r--r-- ; 0640 = -rw-r-----.
# Pre-seed both files NON-EMPTY at DISTINCTIVE modes so the no-fault arm pins
# mode-preservation on each file (0640 on AGENTS.md is not the atomic 0600, so
# a naive mode-less atomic replace would be caught).
seed_ws() {
    _iw_ws=$(smoke_tmp)
    mkdir -p "$_iw_ws/.jichi"
    printf -- '- KEEP-MEMORY sentinel\n' > "$_iw_ws/.jichi/memory.md"
    chmod 644 "$_iw_ws/.jichi/memory.md"
    printf '# Rules\n- KEEP-RULE sentinel\n' > "$_iw_ws/AGENTS.md"
    chmod 640 "$_iw_ws/AGENTS.md"
    cat > "$_iw_ws/.jichi/lessons.draft.md" <<'EOF'
## Memory notes
- a new memory note [warrant: judgement]
## Project rules
- a new rule note
EOF
    printf '%s\n' "$_iw_ws"
}

# --- FAULT ARM ----------------------------------------------------------------
wsF=$(seed_ws)
cp "$wsF/.jichi/memory.md" "$tmp/memF.orig"
( cd "$wsF" && with_deadline 30 env JICHI_FAULT_WRITE_ONCE_AT=0 "$BIN" learn apply \
    < /dev/null > "$tmp/outF" 2> "$tmp/errF" )

# CHECK 1 (THE RED ONE): memory.md still holds its pre-existing sentinel.
# Unfixed: the first in-place jc_write_file truncates memory.md to 0 bytes.
if grep -q 'KEEP-MEMORY sentinel' "$wsF/.jichi/memory.md" 2>/dev/null; then
    t_ok "fault arm: memory.md still contains its pre-existing sentinel (D-18 core property)"
else
    t_fail "fault arm: memory.md LOST its pre-existing sentinel (truncated; content: $(head_bytes 200 "$wsF/.jichi/memory.md" 2>/dev/null); out: $(head_bytes 200 "$tmp/outF" 2>/dev/null))"
fi

# CHECK 2: the rules file GOT its new rule -- the LATER write landed, so this is
# a true failed-PRIMARY (the one-shot knob), not a blanket every-write-fails.
if grep -q 'a new rule note' "$wsF/AGENTS.md" 2>/dev/null; then
    t_ok "fault arm: AGENTS.md got its new rule (later write landed -- failed primary, not blanket fault)"
else
    t_fail "fault arm: AGENTS.md did NOT get its new rule (the one-shot knob missed the memory write? out: $(head_bytes 200 "$tmp/outF" 2>/dev/null))"
fi

# CHECK 3: the file the failed write targeted (memory.md) is UNCHANGED --
# byte-for-byte identical to the pre-seed (the failed append left nothing).
if cmp -s "$wsF/.jichi/memory.md" "$tmp/memF.orig" 2>/dev/null; then
    t_ok "fault arm: memory.md is byte-for-byte unchanged after the failed write"
else
    t_fail "fault arm: memory.md was CHANGED by the failed write (size $(wc -c < "$wsF/.jichi/memory.md" 2>/dev/null | tr -d ' ') vs pre-seed $(wc -c < "$tmp/memF.orig" | tr -d ' '))"
fi

# CHECK 4: no *.tmp<pid> leftover -- the atomic write cleaned up its temp on the
# failed path (guards the fix; the unfixed in-place writer never creates temps).
leftover=$(find "$wsF" -type f -name '*.tmp*' 2>/dev/null)
if [ -z "$leftover" ]; then
    t_ok "fault arm: no *.tmp<pid> leftover in the workspace (atomic write cleaned up)"
else
    t_fail "fault arm: leftover temp file(s) after the run: $leftover"
fi

# --- NO-FAULT ARM ---------------------------------------------------------------
wsN=$(seed_ws)
( cd "$wsN" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/outN" 2> "$tmp/errN" )

# CHECK 5: BOTH the memory note and the rule land (append, not replace).
if grep -q 'a new memory note' "$wsN/.jichi/memory.md" 2>/dev/null \
   && grep -q 'KEEP-MEMORY sentinel' "$wsN/.jichi/memory.md" 2>/dev/null \
   && grep -q 'a new rule note' "$wsN/AGENTS.md" 2>/dev/null \
   && grep -q 'KEEP-RULE sentinel' "$wsN/AGENTS.md" 2>/dev/null; then
    t_ok "no-fault arm: both the memory note and the rule landed (pre-existing content preserved)"
else
    t_fail "no-fault arm: apply did not land both: mem=$(grep -c 'a new memory note' "$wsN/.jichi/memory.md" 2>/dev/null) rule=$(grep -c 'a new rule note' "$wsN/AGENTS.md" 2>/dev/null) (out: $(head_bytes 200 "$tmp/outN" 2>/dev/null))"
fi

# CHECK 6: memory.md keeps its ORIGINAL mode (0644 -> -rw-r--r--).
memmode=$(ls -l "$wsN/.jichi/memory.md" 2>/dev/null | cut -c1-10)
if [ "$memmode" = "-rw-r--r--" ]; then
    t_ok "no-fault arm: memory.md keeps its original 0644 mode ($memmode)"
else
    t_fail "no-fault arm: memory.md mode changed ($memmode, want -rw-r--r--) -- the atomic replace must carry the target's mode"
fi

# CHECK 7: AGENTS.md keeps its ORIGINAL mode (0640 -> -rw-r-----). Distinct from
# the atomic default 0600, so a naive mode-less replace would go red here.
agmode=$(ls -l "$wsN/AGENTS.md" 2>/dev/null | cut -c1-10)
if [ "$agmode" = "-rw-r-----" ]; then
    t_ok "no-fault arm: AGENTS.md keeps its original 0640 mode ($agmode)"
else
    t_fail "no-fault arm: AGENTS.md mode changed ($agmode, want -rw-r-----) -- the atomic replace must carry the target's mode"
fi

t_done
