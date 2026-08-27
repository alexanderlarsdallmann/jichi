#!/bin/sh
# smoke: session export to Markdown / HTML (M34b). Seeds a session JSON in
# the private HOME, exports by id prefix (Markdown to stdout, HTML to a
# file), asserts the transcript structure and that HTML is self-contained
# with escaped metacharacters. (Port of tests/e2e/export.py, M210.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

sdir="$HOME/.jichi.d/sessions"
mkdir -p "$sdir"
cat > "$sdir/deadbeef-0000-0000-0000-000000000000.json" <<'EOF'
{"sessionId":"deadbeef-0000-0000-0000-000000000000",
 "title":"Refactor the <parser>",
 "workspaceDirectory":"/home/me/proj","mode":"auto",
 "history":[
  {"role":"user","content":"refactor a < b & c"},
  {"role":"assistant","content":"reading the file",
   "toolCalls":[{"id":"c1","name":"read_file",
                 "arguments":"{\"path\": \"x.c\"}"}]},
  {"role":"tool","toolCallId":"c1","content":"int main(){}"},
  {"role":"assistant","content":"Done."}]}
EOF

with_deadline 30 "$BIN" export deadbeef < /dev/null > "$tmp/t.md" 2>&1; rc=$?
if [ $rc -eq 0 ]; then
    t_ok "export by id prefix exits 0"
else
    t_fail "export rc=$rc: $(head_bytes 150 "$tmp/t.md")"
fi

md_ok=1
for needle in "# Refactor the <parser>" "## User" "## Assistant" \
              "**Tool call:** \`read_file\`" "## Tool result" \
              "**Mode:** auto"; do
    grep -F -q "$needle" "$tmp/t.md" || { md_ok=0; break; }
done
if [ $md_ok -eq 1 ]; then
    t_ok "markdown transcript carries title/roles/tool call/mode"
else
    t_fail "markdown export missing '$needle'"
fi

with_deadline 30 "$BIN" export deadbeef --html -o "$tmp/t.html" \
    < /dev/null > /dev/null 2>&1
if [ -s "$tmp/t.html" ] && grep -F -q "<!doctype html>" "$tmp/t.html" \
   && grep -F -q "</html>" "$tmp/t.html"; then
    t_ok "HTML export is a self-contained document"
else
    t_fail "HTML export missing or not a full document"
fi

if grep -F -q "&lt;parser&gt;" "$tmp/t.html" \
   && grep -F -q "a &lt; b &amp; c" "$tmp/t.html"; then
    t_ok "HTML export escapes metacharacters"
else
    t_fail "HTML export did not escape < & >"
fi

t_done
