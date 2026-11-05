#!/bin/sh
# smoke: M1104-c (the SV:0257 side effect of M1104 / D-18) -- jichi's OWN
# workspace files that are HARD-LINKED to a second name.
#
# M1104 replaced the four in-place rewrites of pre-existing content with a
# MODE-PRESERVING ATOMIC replace (jc_write_file_atomic_mode: write a
# same-directory temp, fchmod to the target's mode, rename over the target).
# That closes the D-18 truncation hole, but a temp+rename REPLACES the inode.
# A hard link is how a user shares one rules file between jichi (AGENTS.md)
# and another tool (CLAUDE.md) now that D-15 refuses a symlink there -- and
# rename over the first name silently ORPHANS the second: after `learn apply`,
# AGENTS.md and CLAUDE.md are on DIFFERENT inodes, the new rule is in AGENTS.md
# ONLY, and CLAUDE.md keeps the old rules (link count 2 -> 1+1). M141's original
# reason for in-place writes was exactly this, which is why the pre-M1104 code
# happened to keep both names in lock-step.
#
# The fix (M1104-c): jc_write_file_preserve_link -- lstat the target; when it is
# a regular file with st_nlink > 1, write IN PLACE (the M141/vim
# backupcopy=auto convention: keep the inode, both names see the new bytes),
# else take the M1104 atomic path. The in-place write's failure mode -- a short
# write truncates the shared inode -- is the M141-accepted trade for link
# integrity.
#
# A real `jichi learn apply` (offline, no model, < /dev/null) is the vehicle.
# Portable inode read: `ls -li <f> | cut -d' ' -f1`. Portable mode read (the
# state_dir_private / parallel_symlink_merge idiom; stat -c is banned by
# posix_utils_lint): `ls -l <f> | cut -c1-10` -> the 10-char mode string
# (0644 = -rw-r--r-- ; 0640 = -rw-r-----).
#
#   CHECK 1 (THE RED ONE): AGENTS.md + CLAUDE.md hard-linked (ln AGENTS.md
#           CLAUDE.md), pre-seeded identical at 0644, a draft carrying a new
#           project rule, `learn apply` -> the two names still share ONE inode
#           AND both carry the new rule AND the old sentinel AND the link count
#           is 2. RED on the unfixed tree: different inodes, CLAUDE.md missing
#           the new rule, link counts 1+1.
#   CHECK 2 (the memory twin): .jichi/memory.md hard-linked to a second name,
#           pre-seeded non-empty, a draft carrying a new memory note,
#           `learn apply` -> both names contain the note and share an inode.
#   CHECK 3 (NO over-refusal): a SINGLE-link AGENTS.md (no hard link) + a new
#           rule -> the rule lands (the write is not refused because the
#           helper changed) AND the file's MODE is preserved (0644 in, 0644
#           out -- the atomic path is still taken for a single-link target).
#   CHECK 4 (mode on the link case): a hard-linked AGENTS.md/CLAUDE.md pre-
#           seeded at 0640 (NOT the default) -> after, BOTH names are still
#           0640 (the in-place fopen path keeps the existing mode).
#
# t_plan 4.

. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

# Portable inode of FILE (the M1104-c red is a DIVERGENT-inode reading).
inode_of() {
    ls -li "$1" 2>/dev/null | cut -d' ' -f1
}
# Portable link count of FILE (field 3 of `ls -li`, the one after the inode).
links_of() {
    ls -li "$1" 2>/dev/null | cut -d' ' -f3
}
# Portable 10-char mode string of FILE (state_dir_private idiom).
mode_of() {
    ls -l "$1" 2>/dev/null | cut -c1-10
}

# --- CHECK 1 (THE RED ONE): a hard-linked rules file keeps ONE inode ---------
ws1=$(smoke_tmp)
mkdir -p "$ws1/.jichi"
printf '# Rules\n- KEEP-RULE sentinel\n' > "$ws1/AGENTS.md"
chmod 644 "$ws1/AGENTS.md"
ln "$ws1/AGENTS.md" "$ws1/CLAUDE.md"
cat > "$ws1/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- a new rule note
EOF
( cd "$ws1" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/out1" 2> "$tmp/err1" )
i_a=$(inode_of "$ws1/AGENTS.md")
i_c=$(inode_of "$ws1/CLAUDE.md")
l_a=$(links_of "$ws1/AGENTS.md")
if [ -n "$i_a" ] && [ "$i_a" = "$i_c" ] \
   && grep -q 'a new rule note' "$ws1/AGENTS.md" 2>/dev/null \
   && grep -q 'a new rule note' "$ws1/CLAUDE.md" 2>/dev/null \
   && grep -q 'KEEP-RULE sentinel' "$ws1/AGENTS.md" 2>/dev/null \
   && grep -q 'KEEP-RULE sentinel' "$ws1/CLAUDE.md" 2>/dev/null \
   && [ "$l_a" = "2" ]; then
    t_ok "check 1: hard-linked AGENTS.md/CLAUDE.md share one inode ($i_a) after learn apply; both carry the new rule + sentinel; link count 2"
else
    t_fail "check 1: hard link BROKE (AGENTS inode=$i_a CLAUDE inode=$i_c; rule AG=$(grep -c 'a new rule note' "$ws1/AGENTS.md" 2>/dev/null) CL=$(grep -c 'a new rule note' "$ws1/CLAUDE.md" 2>/dev/null); sentinel CL=$(grep -c 'KEEP-RULE sentinel' "$ws1/CLAUDE.md" 2>/dev/null); AG links=$l_a) -- the atomic rename orphaned CLAUDE.md (M1104 defect)"
fi

# --- CHECK 2 (the memory twin): a hard-linked .jichi/memory.md ---------------
ws2=$(smoke_tmp)
mkdir -p "$ws2/.jichi"
printf -- '- KEEP-MEMORY sentinel\n' > "$ws2/.jichi/memory.md"
chmod 644 "$ws2/.jichi/memory.md"
ln "$ws2/.jichi/memory.md" "$ws2/.jichi/memory.md.2nd"
printf '# Rules\n- KEEP-RULE sentinel\n' > "$ws2/AGENTS.md"
chmod 644 "$ws2/AGENTS.md"
cat > "$ws2/.jichi/lessons.draft.md" <<'EOF'
## Memory notes
- a new memory note [warrant: judgement]
EOF
( cd "$ws2" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/out2" 2> "$tmp/err2" )
m1=$(inode_of "$ws2/.jichi/memory.md")
m2=$(inode_of "$ws2/.jichi/memory.md.2nd")
if [ -n "$m1" ] && [ "$m1" = "$m2" ] \
   && grep -q 'a new memory note' "$ws2/.jichi/memory.md" 2>/dev/null \
   && grep -q 'a new memory note' "$ws2/.jichi/memory.md.2nd" 2>/dev/null; then
    t_ok "check 2: hard-linked .jichi/memory.md keeps one inode ($m1); both names carry the new memory note"
else
    t_fail "check 2: hard-linked memory.md BROKE (memory.md inode=$m1 memory.md.2nd inode=$m2; note primary=$(grep -c 'a new memory note' "$ws2/.jichi/memory.md" 2>/dev/null) second=$(grep -c 'a new memory note' "$ws2/.jichi/memory.md.2nd" 2>/dev/null))"
fi

# --- CHECK 3 (NO over-refusal): a single-link AGENTS.md still lands + mode ----
ws3=$(smoke_tmp)
mkdir -p "$ws3/.jichi"
printf '# Rules\n- KEEP-RULE sentinel\n' > "$ws3/AGENTS.md"
chmod 644 "$ws3/AGENTS.md"
cat > "$ws3/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- a new rule note
EOF
( cd "$ws3" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/out3" 2> "$tmp/err3" )
m3=$(mode_of "$ws3/AGENTS.md")
if grep -q 'a new rule note' "$ws3/AGENTS.md" 2>/dev/null \
   && grep -q 'KEEP-RULE sentinel' "$ws3/AGENTS.md" 2>/dev/null \
   && [ "$m3" = "-rw-r--r--" ]; then
    t_ok "check 3: single-link AGENTS.md got the rule (not over-refused) and kept its 0644 mode ($m3) -- the atomic path still runs"
else
    t_fail "check 3: single-link AGENTS.md wrong (rule=$(grep -c 'a new rule note' "$ws3/AGENTS.md" 2>/dev/null) sentinel=$(grep -c 'KEEP-RULE sentinel' "$ws3/AGENTS.md" 2>/dev/null) mode=$m3 want -rw-r--r--) -- preserve_link must not refuse a single-link target"
fi

# --- CHECK 4 (mode on the link case): a hard link at 0640 stays 0640 ---------
ws4=$(smoke_tmp)
mkdir -p "$ws4/.jichi"
printf '# Rules\n- KEEP-RULE sentinel\n' > "$ws4/AGENTS.md"
chmod 640 "$ws4/AGENTS.md"
ln "$ws4/AGENTS.md" "$ws4/CLAUDE.md"
cat > "$ws4/.jichi/lessons.draft.md" <<'EOF'
## Project rules
- a new rule note
EOF
( cd "$ws4" && with_deadline 30 "$BIN" learn apply \
    < /dev/null > "$tmp/out4" 2> "$tmp/err4" )
i4a=$(inode_of "$ws4/AGENTS.md")
i4c=$(inode_of "$ws4/CLAUDE.md")
mm4a=$(mode_of "$ws4/AGENTS.md")
mm4c=$(mode_of "$ws4/CLAUDE.md")
if [ "$i4a" = "$i4c" ] \
   && [ "$mm4a" = "-rw-r-----" ] \
   && [ "$mm4c" = "-rw-r-----" ]; then
    t_ok "check 4: hard-linked rules file at 0640 stays 0640 on BOTH names (inode $i4a; AG=$mm4a CL=$mm4c) -- the in-place write keeps the existing mode"
else
    t_fail "check 4: 0640 hard link wrong (AG inode=$i4a CL inode=$i4c; AG mode=$mm4a CL mode=$mm4c want -rw-r-----)"
fi

t_done
