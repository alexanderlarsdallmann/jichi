#!/bin/sh
# smoke: the codebase-search index cache is a FOURTH per-workspace store, and
# like the checkpoint/lease/telemetry stores it must be keyed by the 64-hex
# SHA-256 of the workspace path (M1097-b, D-09).
#
# THE DEFECT. Pre-M1097-b the index cache dir was `~/.jichi.d/index/<djb2(root)>`
# -- the index's OWN djb2 (h*33+c), not the shared store key. djb2 collides on
# names that differ by (+1,-33) in adjacent bytes: /ws/Ez and /ws/FY produced the
# SAME 20-digit decimal key, so two workspaces shared ONE index directory. Indexing
# one workspace overwrote the other's manifest/vectors (a clobber, and a
# cross-workspace interference: a re-index after a sibling index lost its cache),
# and the manifest's "root" was never compared on load (only "model" + "endian"),
# so a foreign-rooted cache was trusted rather than rebuilt.
#
# The fix reuses the ONE derivation (jc_workspace_key, 64-hex SHA-256 of the raw
# path) and adds the belt: the load path compares the manifest's "root" to the
# root being indexed; a mismatch means the cache belongs to a different tree and
# is rebuilt. This is the index analogue of M1097's checkpoint key + jichi.worktree
# belt.
#
# WHAT THIS ASSERTS. (1) the OLD derivation really did collide on this pair;
# (2) the NEW one separates them (64 lowercase hex, distinct); (3) THE FINDING --
# indexing both colliding workspaces leaves TWO 64-hex index dirs on disk, not
# one shared numeric dir (pre-fix this is red: a single djb2 dir); (4) the
# clobber -- re-indexing Ez after FY reuses Ez's own cache (embedded 0) because
# the stores are separate (pre-fix red: FY overwrote the shared dir, so Ez
# re-embeds); (5) the search-level guarantee -- a codebase_search in FY for FY's
# own marker does not surface a chunk whose marker only exists in Ez; (6) the
# belt -- a hand-planted manifest whose "root" is a foreign path is honoured by
# rebuilding (embedded > 0), not reused.
#
# keyprobe (tests/tools/keyprobe.c) computes both keys, linked against the
# in-tree SHA-256 core so it cannot drift from the implementation.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git (keyprobe build + sibling pattern)"
t_plan 6
smoke_home
tmp=$(smoke_tmp)

# The collision pair: adjacent bytes differ by (+1,-33) -- 'E'/'F' and
# 'z'/'Y' -- which djb2 (h*33+c) folds into one value for any prefix/suffix.
ws=$(smoke_tmp)
ez="$ws/Ez"
fy="$ws/FY"
mkdir -p "$ez" "$fy"
EZMARK=ezmarkx
FYMARK=fymarky
printf 'int %s(void){return 1;}\n' "$EZMARK" > "$ez/note.txt"
printf 'int %s(void){return 2;}\n' "$FYMARK" > "$fy/note.txt"

KP="$SMOKE_TOOLS/keyprobe"

# 1. The OLD derivation collides on this pair (so the driver measures D-09, not
# an adjacent structure).
old_ez=$("$KP" djb2 "$ez")
old_fy=$("$KP" djb2 "$fy")
if [ -n "$old_ez" ] && [ "$old_ez" = "$old_fy" ]; then
    t_ok "the old djb2 key collides: Ez and FY both derive $old_ez"
else
    t_fail "the pair no longer collides under djb2 (Ez=$old_ez FY=$old_fy)"
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
    t_ok "the new sha256 keys are 64-hex and distinct"
else
    t_fail "the new sha256 keys are not 64-hex/distinct (Ez=$new_ez FY=$new_fy)"
fi

# A config with BOTH a chat model (for the check-5 search) and an embed model
# (for `jichi index` + the search's query embed), routed to one mockmodel.
cfg() { # cfg PATH PORT
    cat > "$1" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]},
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["embed"]}],
 "snapshots":false,"repoMap":false,"references":false,
 "toolProfile":"full","maxRetries":0}
EOF
}

# The search rule makes the model call codebase_search once for FY's marker,
# then finish. The embed rule answers every /v1/embeddings request (it must,
# or the index builds fail). Rules are selected by CONTENT (index_coverage.sh's
# lesson): a chat request carries "messages"; the codebase_search round is the
# one carrying "role":"tool".
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "\\"messages\\""
  nomatch "\\"role\\":\\"tool\\""
  tool codebase_search {"query":"$FYMARK"}
rule
  match "\\"messages\\""
  text DONE
rule
  embed $EZMARK $FYMARK marker
EOF

# --- index both workspaces, then assert on-disk separation -------------------
mm_start "$tmp/replies.mm" "$tmp/cap" 30
cfg "$tmp/config.json" "$MM_PORT"

# Index Ez, then FY (the order that clobbers pre-fix).
(cd "$ez" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    index < /dev/null > "$tmp/ez.out" 2>"$tmp/ez.err"); ez_rc=$?
(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    index < /dev/null > "$tmp/fy.out" 2>"$tmp/fy.err"); fy_rc=$?

id="$HOME/.jichi.d/index"
# 3. THE FINDING: two 64-hex index dirs, one per workspace -- not one numeric.
store_ok=1
n_idx=$(ls "$id" 2>/dev/null | grep -c .)
[ "$n_idx" -eq 2 ] || store_ok=0
for d in "$id"/*; do
    [ -e "$d" ] || continue
    b=${d##*/}
    case $b in *[!0-9a-f]*|'') store_ok=0 ;; esac
    [ ${#b} -eq 64 ] || store_ok=0
done
if [ "$store_ok" -eq 1 ] && [ "$ez_rc" -eq 0 ] && [ "$fy_rc" -eq 0 ]; then
    t_ok "the index store holds exactly two 64-hex dirs (one per workspace), not one shared numeric dir"
else
    t_fail "index dirs wrong (want two 64-hex, got: $(ls "$id" 2>/dev/null | tr '\n' ' '); ez_rc=$ez_rc fy_rc=$fy_rc)"
fi

# 4. THE CLOBBER: re-index Ez. Post-fix Ez's own cache is intact (FY wrote to a
# different dir), so Ez fully reuses -- "0 embedded". Pre-fix FY overwrote the
# shared dir, so Ez's file is no longer in the manifest and re-index re-embeds
# -- "1 embedded" (the red leg).
(cd "$ez" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    index < /dev/null > "$tmp/ez2.out" 2>"$tmp/ez2.err"); ez2_rc=$?
if [ "$ez2_rc" -eq 0 ] && grep -q '0 embedded' "$tmp/ez2.out"; then
    t_ok "re-indexing Ez after FY reuses Ez's own cache (no clobber: 0 embedded)"
else
    t_fail "Ez's cache was clobbered by FY (re-index must re-embed): rc=$ez2_rc out=[$(head_bytes 120 "$tmp/ez2.out")] err=[$(head_bytes 120 "$tmp/ez2.err")]"
fi

# 5. The search-level guarantee: a codebase_search in FY for FY's marker returns
# FY's chunk and does NOT surface a chunk whose marker only exists in Ez.
#
# We read ONLY the tool-result message ("role":"tool" content) -- NOT the whole
# capture -- because the earlier indexing checks' embed requests carry BOTH
# markers in their input array (the mock's embed vocab), so a bare grep would
# false-positive on the embed body rather than the search result. The
# tool-result is what the model actually saw from the codebase_search.
(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -p "find $FYMARK" --no-session --auto < /dev/null \
    > "$tmp/s.out" 2>"$tmp/s.err"); s_rc=$?
# Collect the codebase_search tool-result content only. The request that carries
# the tool result is the one with a "role":"tool" message; we pull the
# "content":"..." of THAT message. The tool result is the LAST message in the
# array, so its content runs to the end of the request body -- a safe slice.
# (A bare whole-capture grep would false-positive: the earlier indexing checks'
# embed requests carry BOTH markers in their input array, the mock's vocab.)
tool_content=""
for _f in "$tmp/cap"/req.*; do
    [ -f "$_f" ] || continue
    if grep -q '"role":"tool"' "$_f"; then
        tool_content=$(grep -o '"role":"tool"[^]]*' "$_f" | head -1)
        [ -n "$tool_content" ] && break
    fi
done
fy_seen=0; ez_leak=0
printf '%s' "$tool_content" | grep -q "$FYMARK" && fy_seen=1
printf '%s' "$tool_content" | grep -q "$EZMARK" && ez_leak=1
if [ "$s_rc" -eq 0 ] && [ "$fy_seen" -eq 1 ] && [ "$ez_leak" -eq 0 ]; then
    t_ok "a codebase_search in FY returns FY's marker and never leaks Ez's marker"
else
    printf '%s' "$tool_content" > "$tmp/tool.content"
    t_fail "FY search isolated wrongly: rc=$s_rc fy_seen=$fy_seen ez_leak=$ez_leak tool_result=[$(head_bytes 300 "$tmp/tool.content")]"
fi
# 6. THE BELT: a manifest whose recorded "root" is a foreign path must be
# honoured by REBUILDING (embedded > 0), not reusing (embedded 0). To isolate
# the root as the ONLY mismatch, let the real `index` command write FY's valid
# cache (correct endian, dim, model, matching file), then sed the "root" field
# to a foreign path and re-index. Pre-fix the loader checks model+endian only,
# so the foreign-rooted cache is trusted (embedded 0); post-fix the root
# mismatch drops it (embedded > 0). The rebuild EMBEDS, so the mock must be
# alive here (it stops only after this check). The 64-hex key is from keyprobe.
fykey_dir="$id/$new_fy"
if [ ! -d "$fykey_dir" ]; then
    # FY's cache was written by its index run above; find it if the key naming
    # differs (defensive -- should not happen).
    fykey_dir=""
    for d in "$id"/*; do
        [ -e "$d/manifest.json" ] || continue
        case ${d##*/} in *[!0-9a-f]*|'') continue ;; esac
        fykey_dir=$d
    done
fi
# A clean re-index first, so the on-disk manifest is FY's OWN valid cache.
(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    index < /dev/null > /dev/null 2>&1)
# Flip ONLY the root in FY's own manifest to a foreign path. The smoke temp
# root never contains '|', so it is a safe sed delimiter.
sed "s#\"root\":\"$fy\"#\"root\":\"$ws/foreign\"#" \
    "$fykey_dir/manifest.json" > "$tmp/manifest.belt" &&
    cat "$tmp/manifest.belt" > "$fykey_dir/manifest.json"
(cd "$fy" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    index < /dev/null > "$tmp/fy2.out" 2>"$tmp/fy2.err"); fy2_rc=$?
if [ "$fy2_rc" -eq 0 ] && grep -Eq '[1-9][0-9]* embedded' "$tmp/fy2.out"; then
    t_ok "a foreign-rooted manifest is honoured: FY rebuilds (embedded > 0), not reuses"
else
    t_fail "the belt did not rebuild on a root mismatch: rc=$fy2_rc out=[$(head_bytes 120 "$tmp/fy2.out")] err=[$(head_bytes 120 "$tmp/fy2.err")] manifest_root=[$(head_bytes 200 "$fykey_dir/manifest.json" 2>/dev/null)]"
fi

mm_stop
t_done
