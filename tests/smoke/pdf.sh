#!/bin/sh
# smoke: PDF ingestion (M42). Builds a tiny REAL PDF carrying a marker,
# then drives a mock turn: the model reads doc.pdf; the mock answers
# PDF_OK only if the marker text (extracted by pdftotext, not the raw
# bytes) is in the tool result it receives. Skips without pdftotext.
# The PDF fixture comes from _smoke.sh's smoke_make_pdf (exact xref
# offsets by construction; shared with docs_pdf.sh since M213).
# (Port of tests/e2e/pdf.py, M212.)
. "$(dirname "$0")/_smoke.sh"

command -v pdftotext >/dev/null 2>&1 || t_skip "pdftotext not installed"

t_plan 2
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
MARKER=JICHIPDFMARKER42

smoke_make_pdf "$ws/doc.pdf" "$MARKER"

# sanity: the fixture itself must extract (else the driver, not jichi,
# is broken -- prefer a loud fixture failure to a misattributed one)
if pdftotext "$ws/doc.pdf" - 2>/dev/null | grep -q "$MARKER"; then
    t_ok "the sh-built PDF fixture extracts with pdftotext"
else
    t_fail "the fixture PDF does not extract -- driver bug, not jichi"
fi

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool read_file {"path":"doc.pdf"}
rule
  match "$MARKER"
  text PDF_OK
rule
  text PDF_MISSING
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "read doc.pdf" < /dev/null); rc=$?
mm_stop

case "$out" in
    *PDF_OK*) t_ok "read_file fed pdftotext-extracted text to the model" ;;
    *PDF_MISSING*) t_fail "tool result lacked the extracted marker" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

t_done
