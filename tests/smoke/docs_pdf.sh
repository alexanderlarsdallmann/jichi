#!/bin/sh
# smoke: PDFs in a docs source are extracted + indexed (M44/M45). A docs
# dir holds hooks.pdf (built by smoke_make_pdf; its EXTRACTED text
# carries the hooks marker) and a plain-text state.md decoy; the mock
# embeddings endpoint keys cosine on those words, so `docs search` must
# retrieve the PDF and rank it first. Skips without pdftotext.
# (Port of tests/e2e/docs_pdf.py, M213.)
. "$(dirname "$0")/_smoke.sh"

command -v pdftotext >/dev/null 2>&1 || t_skip "pdftotext not installed"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
docs=$(smoke_tmp)

smoke_make_pdf "$docs/hooks.pdf" "hooks hooks hooks lifecycle"
cat > "$docs/state.md" <<'EOF'
# State

State state state holds the component data.
EOF

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  embed hooks state
EOF

mm_start "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"chat","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"emb","provider":"openai","model":"mock-embed",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["embed"]}],
 "docs":[{"name":"docs","path":"$docs"}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

(cd "$docs" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    docs search docs "how do hooks work" < /dev/null \
    > "$tmp/out" 2>"$tmp/err"); rc=$?
mm_stop

if [ -f "$tmp/req.1" ]; then
    t_ok "the embeddings endpoint was queried"
else
    t_fail "no embeddings request (rc=$rc): $(tail -c 200 "$tmp/err")"
fi
if grep -q "hooks.pdf" "$tmp/out"; then
    t_ok "the PDF was extracted, indexed, and retrieved"
else
    t_fail "hooks.pdf missing from results: $(head_bytes 200 "$tmp/out")"
fi
pdf_line=$(grep -n "hooks.pdf" "$tmp/out" | head -1 | cut -d: -f1)
state_line=$(grep -n "state.md" "$tmp/out" | head -1 | cut -d: -f1)
if [ -z "$state_line" ] || [ "${pdf_line:-9999}" -lt "$state_line" ]; then
    t_ok "the PDF ranks ahead of the decoy"
else
    t_fail "state.md ranked ahead of hooks.pdf"
fi

t_done
