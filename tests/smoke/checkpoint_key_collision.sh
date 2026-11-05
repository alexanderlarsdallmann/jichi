#!/bin/sh
# smoke: two workspaces that USED to share one checkpoint store can no longer
# cross-restore (M1097, D-09).
#
# THE DEFECT. The per-workspace stores (the checkpoint shadow repo, the run
# lease, the telemetry log) were named by djb2 of the workspace path -- and
# djb2 collides on names differing by (+1,-33) in adjacent bytes: /ws/Ez and
# /ws/FY produced the SAME 20-digit decimal key. One run in Ez left a shadow
# repo that `jichi checkpoints` in FY -- which had never run a model -- then
# listed, and `undo --dry-run` in FY offered to restore Ez's file and remove
# FY's own. The fix (M1097) keys every store on the 64-hex SHA-256 of the raw
# path (distinct for Ez and FY) and pins the work tree in the shadow repo's
# config as jichi.worktree (the belt): a store positively pinned to a
# different tree is disabled, "snapshots unavailable here".
#
# WHAT THIS ASSERTS, one check at a time. (1) the OLD derivation really did
# collide on this pair, so the driver measures the defect and not an adjacent
# one; (2) the NEW one separates them (64 lowercase hex, distinct); (3) a mock
# run in Ez exits 0 and `checkpoints` lists its checkpoint; (4) FY, which never
# ran a model, lists no checkpoints and its undo fails without naming Ez's
# file -- pre-fix this check is red: FY cross-read Ez's store; (5) the on-disk
# store holds exactly two 64-hex dirs, one per workspace; (6) the belt: pinning
# FY's store to a foreign tree disables it.
#
# keyprobe (tests/tools/keyprobe.c) computes both keys, linked against the
# in-tree SHA-256 core so it cannot drift from the implementation.
. "$(dirname "$0")/_smoke.sh"

# The config carries snapshots:true -- written here rather than via write_config
# because that helper's base template pins "snapshots":false and an override
# appends a duplicate key the parser takes the first of (the baseline_checkpoint
# comment, which this driver inherits).
mk_cfg() {  # $1 = path, $2 = port
    cat > "$1" <<EOC
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOC
}

command -v git >/dev/null 2>&1 || t_skip "needs git (snapshots back the checkpoint)"
t_plan 6
smoke_home
tmp=$(smoke_tmp)

# The collision pair: adjacent bytes differ by (+1,-33) -- 'E'/'F' and
# 'z'/'Y' -- which djb2 (h*33+c) folds into one value for any prefix/suffix.
ws=$(smoke_tmp)
ez="$ws/Ez"
fy="$ws/FY"
mkdir -p "$ez" "$fy"

KP="$SMOKE_TOOLS/keyprobe"

# 1. The OLD derivation collides on this pair.
old_ez=$("$KP" djb2 "$ez")
old_fy=$("$KP" djb2 "$fy")
if [ -n "$old_ez" ] && [ "$old_ez" = "$old_fy" ]; then
    t_ok "the old djb2 key collides: Ez and FY both derive $old_ez"
else
    t_fail "the pair no longer collides under djb2 (Ez=$old_ez FY=$old_fy); the driver measures a different structure than D-09"
fi

# 2. The NEW derivation separates them: 64 lowercase hex, distinct.
new_ez=$("$KP" sha256 "$ez")
new_fy=$("$KP" sha256 "$fy")
new_ok=1
case $new_ez in *[!0-9a-f]*|'') new_ok=0 ;; esac
case $new_fy in *[!0-9a-f]*|'') new_ok=0 ;; esac
[ ${#new_ez} -eq 64 ] || new_ok=0
[ ${#new_fy} -eq 64 ] || new_ok=0
[ "$new_ez" != "$new_fy" ] || new_ok=0
if [ "$new_ok" -eq 1 ]; then
    t_ok "the new sha256 keys are 64-hex and distinct (Ez=${new_ez%????}..., FY=${new_fy%????}...)"
else
    t_fail "the new sha256 keys are not 64-hex/distinct (Ez=$new_ez FY=$new_fy)"
fi

# 3. A mock run in Ez exits 0 and `checkpoints` lists its checkpoint.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"note.txt","content":"from Ez\n"}
rule
  text done
EOF
mm_start "$tmp/replies.mm" "$tmp/cap" 8
mk_cfg "$tmp/config.json" "$MM_PORT"
(cd "$ez" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session --auto -p 'do it' < /dev/null > "$tmp/out.txt" 2>&1)
ez_rc=$?
mm_stop
ez_cp=$(cd "$ez" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session checkpoints < /dev/null 2>&1)
ez_cprc=$?
if [ "$ez_rc" -eq 0 ] && [ "$ez_cprc" -eq 0 ] && \
   printf '%s\n' "$ez_cp" | grep -q '  1  do it'; then
    t_ok "the run in Ez exits 0 and its checkpoint is listed ('  1  do it')"
else
    t_fail "Ez's own store is broken: run rc=$ez_rc, checkpoints rc=$ez_cprc out=[$ez_cp], tail=[$(head_bytes 200 "$tmp/out.txt")]"
fi

# 4. THE FINDING: FY, which never ran a model, has an empty store of its own.
fy_cp=$(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session checkpoints < /dev/null 2>&1)
fy_rc=$?
fy_undo=$(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    --no-session undo --dry-run 1 < /dev/null 2>&1)
fy_undorc=$?
if [ "$fy_rc" -eq 0 ] && \
   [ "$fy_cp" = "(no checkpoints for this workspace)" ] && \
   [ "$fy_undorc" -ne 0 ] && \
   ! printf '%s\n' "$fy_undo" | grep -q 'note.txt'; then
    t_ok "FY lists no checkpoints and its undo fails without naming Ez's file"
else
    t_fail "FY cross-reads Ez's store: checkpoints rc=$fy_rc out=[$fy_cp], undo rc=$fy_undorc out=[$fy_undo]"
fi

# 5. Store separation on disk: exactly two 64-hex dirs, one per workspace.
lsd="$HOME/.jichi.d/checkpoints"
store_ok=1
n_store=$(ls "$lsd" 2>/dev/null | grep -c .)
[ "$n_store" -eq 2 ] || store_ok=0
for d in "$lsd"/*; do
    b=${d##*/}
    case $b in *[!0-9a-f]*|'') store_ok=0 ;; esac
    [ ${#b} -eq 64 ] || store_ok=0
done
if [ "$store_ok" -eq 1 ]; then
    t_ok "the checkpoint store holds exactly two 64-hex dirs (one per workspace)"
else
    t_fail "checkpoint store dirs wrong (want two 64-hex, got: $(ls "$lsd" 2>/dev/null | tr '\n' ' '))"
fi

# 6. THE BELT: pinning FY's store to a foreign tree disables it.
# FY's run in check 4 created its own store (the belt written at init);
# find it by the key keyprobe computed for FY.
fykey_dir=""
for d in "$lsd"/*; do
    b=${d##*/}
    if [ "$b" = "$new_fy" ]; then
        fykey_dir=$d
    fi
done
belt_rc=0
belt_out="(store not found)"
if [ -n "$fykey_dir" ]; then
    git --git-dir="$fykey_dir" config jichi.worktree "$ws/foreign"
    belt_out=$(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
        --no-session checkpoints < /dev/null 2>&1)
    belt_rc=$?
fi
if [ "$belt_rc" -eq 1 ] && \
   printf '%s\n' "$belt_out" | grep -q 'snapshots unavailable here'; then
    t_ok "a store pinned to a foreign tree is disabled ('snapshots unavailable here')"
else
    t_fail "the belt did not disable the foreign-pinned store: rc=$belt_rc out=[$belt_out]"
fi

t_done
