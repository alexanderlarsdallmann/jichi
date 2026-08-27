#!/bin/sh
# smoke: `prune` trims the codebase-index cache, and never the current
# workspace's own index (M612).
#
# THE SEAM. The index cache (~/.jichi.d/index/<key>/) is keyed by workspace root
# and had NO retention: one directory per distinct workspace, forever. `prune`
# scoped sessions (M219) and dreams (M611); M612 adds the index, by the same
# --keep/--older-than selectors -- an index is a REBUILDABLE cache, so this is
# the safest of the three to sweep -- while PROTECTING the current workspace's
# own index (deleting the one you are about to use is pure waste). Born red:
# before M612 `prune` ignored the index cache and deleted zero.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
idx="$HOME/.jichi.d/index"

# Seed three FAKE workspace indexes (opaque keys), oldest -> newest by mtime.
seed_fake() { # key ts
    mkdir -p "$idx/$1"
    printf '{"version":1,"root":"/gone/%s"}\n' "$1" > "$idx/$1/manifest.json"
    printf 'VEC' > "$idx/$1/vectors.f32"
    touch -t "$2" "$idx/$1/manifest.json" "$idx/$1/vectors.f32" "$idx/$1"
}
seed_fake 111 202601010101
seed_fake 222 202601020101
seed_fake 333 202601030101

ndirs() { ls -d "$idx"/*/ 2>/dev/null | grep -c . ; }

# --- 1: dry-run reports indexes and deletes none ---------------------------------
out=$(with_deadline 20 "$BIN" prune --keep 1 --dry-run < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'index' && [ "$(ndirs)" -eq 3 ]; then
    t_ok "dry-run reports indexes and deletes none (still 3)"
else
    t_fail "dry-run rc=$rc dirs=$(ndirs): $(printf '%s' "$out" | head_bytes 160)"
fi

# --- 2: --keep 1 leaves exactly the newest fake ----------------------------------
out=$(with_deadline 20 "$BIN" prune --keep 1 < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && [ -d "$idx/333" ] && [ ! -d "$idx/111" ] && [ ! -d "$idx/222" ]; then
    t_ok "prune --keep 1 kept the newest index and removed the two older ones"
else
    t_fail "prune rc=$rc left: $(ls "$idx" 2>/dev/null | tr '\n' ' ')"
fi

# --- 3: the report counts indexes distinctly -------------------------------------
if printf '%s' "$out" | grep -q 'index(es)'; then
    t_ok "the report counts indexes distinctly"
else
    t_fail "no index count in the report: $(printf '%s' "$out" | head_bytes 160)"
fi

# --- 4/5: the CURRENT workspace's index is protected, even when old ---------------
# Build a REAL index for $ws via the mock embeddings endpoint, then age it and
# re-seed an old fake; prune by age must delete the fake and KEEP the real one.
printf 'int marker_fn(void){return 7;}\n' > "$ws/a.c"
cat > "$tmp/emb.mm" <<'MM'
wire openai
rule
  embed marker fn code int
MM
mm_start "$tmp/emb.mm" "$tmp/cap"
cat > "$tmp/config.json" <<CFG
{"lowResource":false,"models":[
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["embed"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
CFG
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" index --reindex \
    < /dev/null > /dev/null 2>"$tmp/ix.err"); ixrc=$?
mm_stop

# The real index is the one dir that is not a fake; age its manifest so --older-than
# would select it if it were not protected.
real=""
for d in "$idx"/*/; do
    case "$d" in *"/333/"|*"/111/"|*"/222/") continue ;; esac
    real="$d"
done
if [ "$ixrc" -eq 0 ] && [ -n "$real" ] && [ -f "$real/manifest.json" ]; then
    t_ok "a real index was built for the current workspace ($(basename "$real"))"
else
    t_fail "index build rc=$ixrc real='$real': $(head_bytes 120 "$tmp/ix.err")"
fi
touch -t 202601010101 "$real/manifest.json" "$real"      # make it OLD
seed_fake 444 202601010101                                # an old fake to delete
out=$(cd "$ws" && with_deadline 20 "$BIN" prune --older-than 1d < /dev/null 2>&1)
if [ -d "$real" ] && [ ! -d "$idx/444" ]; then
    t_ok "prune --older-than kept the current workspace's own (old) index and removed the old fake"
else
    t_fail "protection failed: real=$([ -d "$real" ] && echo kept || echo GONE) \
444=$([ -d "$idx/444" ] && echo LEFT || echo gone); $(printf '%s' "$out" | head_bytes 120)"
fi

t_done
