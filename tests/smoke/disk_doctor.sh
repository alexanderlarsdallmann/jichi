#!/bin/sh
# M814 (P4b): `doctor` reports the disk fence honestly. Its "disk space" line is now
# measured with jc_disk_space (M805/M813), not the old jc_disk_free_mb that read 0 on
# a sub-1024 fragment size (ZFS = 512) -- the rig T11 bug, where doctor told an
# illumos/FreeBSD user "0 MB free" on a disk that was not. Here, on the local (non-ZFS)
# filesystem, we assert the line reports POSITIVE free space and names the floor; the
# ZFS-non-zero property itself is the rig T11 check. And doctor now NAMES
# kept-but-unpreserved worktrees -- orphan par-<run> bases a dead run left behind.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
cat > "$tmp/cfg.json" <<'EOF'
{"toolProfile":"full","lowResource":false,
 "models":[{"name":"m","provider":"openai","model":"mock","apiKey":"x","roles":["chat"]}]}
EOF

# --- the disk-space line: positive free + the floor named ----------------------
"$BIN" --config "$tmp/cfg.json" doctor > "$tmp/doc.txt" 2>&1
disk=$(grep -iA1 'disk space' "$tmp/doc.txt" | grep -iE 'MiB free' | head -1)
freemb=$(printf '%s\n' "$disk" | grep -oE '^[[:space:]]*[0-9]+' | tr -d ' ')
echo "# disk line: $disk"
[ -n "$disk" ] && t_ok "doctor reports a disk-space line" \
    || t_fail "doctor has no disk-space line -- $(head_bytes 200 "$tmp/doc.txt")"
{ [ -n "$freemb" ] && [ "$freemb" -gt 0 ]; } \
    && t_ok "the line reports POSITIVE free space (${freemb} MiB) -- not the ZFS-fragment 0 (T11)" \
    || t_fail "free space reads '$freemb' -- 0 or unpar?  the jc_disk_free_mb bug"
printf '%s\n' "$disk" | grep -q 'diskFloorMb' \
    && t_ok "the line names the disk-fence floor (diskFloorMb)" \
    || t_fail "the disk line does not name diskFloorMb -- pre-M814 wording"

# --- kept-but-unpreserved worktrees: doctor names an orphan base ---------------
mkdir -p "$HOME/.jichi.d/worktrees/par-deadrun999/wt-0"
"$BIN" --config "$tmp/cfg.json" doctor > "$tmp/doc2.txt" 2>&1
grep -qi 'kept-but-unpreserved worktrees' "$tmp/doc2.txt" \
    && t_ok "doctor names an orphaned worktree base (kept-but-unpreserved)" \
    || t_fail "doctor did not name the seeded orphan base -- $(grep -i worktree "$tmp/doc2.txt" | head -1)"
t_done
