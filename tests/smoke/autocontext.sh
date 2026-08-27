#!/bin/sh
# smoke: automatic context injection (M61, auto-RAG). ONE mockmodel
# serves both endpoints, routed by body content (a chat request carries
# "messages"; an embeddings request carries "input" instead): the
# embeddings rule keys cosine on a marker word so the marker file ranks
# first. With --auto-context the captured chat body must carry the
# "automatically retrieved context" block naming target.c; with
# --no-auto-context it must not.
# (Port of tests/e2e/autocontext.py, M213 -- and the driver that closes
# the M212 orphan: autocontext.py had been dropped from the e2e run list
# without being ported, so it silently stopped running.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
MARKER=zorklefrobnicator

cat > "$ws/target.c" <<EOF
/* $MARKER module */
int $MARKER(void){ return ${MARKER}_impl(); }
EOF
cat > "$ws/other.c" <<'EOF'
/* banana */
int banana(void){ return 0; }
EOF

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"messages\""
  text ok
rule
  embed zorklefrobnicator banana
EOF

cfg() { # cfg PATH PORT
    cat > "$1" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["chat"]},
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$2/v1","apiKey":"x","roles":["embed"]}],
 "autoContext":true,"autoContextSources":"codebase",
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
}

# --- 1) auto-context ON: the chat body carries the retrieved block ----------
mkdir -p "$tmp/cap1"
mm_start "$tmp/replies.mm" "$tmp/cap1"
cfg "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto-context -p "how does $MARKER work" \
    < /dev/null > /dev/null 2>"$tmp/err"); rc=$?
mm_stop
chat_req=$(grep -l '"messages"' "$tmp/cap1"/req.* 2>/dev/null | tail -1)
if [ $rc -eq 0 ] && [ -n "$chat_req" ]; then
    t_ok "the chat endpoint received a request (rc=0)"
else
    t_fail "rc=$rc, no chat request: $(tail -c 200 "$tmp/err")"
fi
if grep -q "automatically retrieved context" "$chat_req" 2>/dev/null; then
    t_ok "the retrieved-context block is in the chat body"
else
    t_fail "no retrieved-context block"
fi
if grep -q "target.c" "$chat_req" 2>/dev/null; then
    t_ok "the block names the marker file (target.c)"
else
    t_fail "target.c missing from the block"
fi

# --- 2) auto-context OFF: no block -------------------------------------------
HOME=$(smoke_tmp)
export HOME
mkdir -p "$tmp/cap2"
mm_start "$tmp/replies.mm" "$tmp/cap2"
cfg "$tmp/config2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    -q --no-session --no-auto-context -p "how does $MARKER work" \
    < /dev/null > /dev/null 2>&1); rc=$?
mm_stop
chat_req=$(grep -l '"messages"' "$tmp/cap2"/req.* 2>/dev/null | tail -1)
if [ $rc -eq 0 ] && [ -n "$chat_req" ] \
   && ! grep -q "automatically retrieved context" "$chat_req"; then
    t_ok "--no-auto-context suppressed the block"
else
    t_fail "block present (or run failed, rc=$rc) despite --no-auto-context"
fi

t_done
