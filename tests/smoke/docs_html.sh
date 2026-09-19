#!/bin/sh
# smoke: HTML in a docs source is reduced to TEXT before indexing (M667).
#
# THE DEFECT, measured on a real corpus rather than imagined. docs/DOCS.md says a
# `url` docs source is fetched and "reduced to plain text (tags dropped,
# `<script>`/`<style>` skipped …)" before indexing. A **`path`** source was not,
# and whole projects ship their documentation as HTML -- Racket's Guide is 151
# Scribble pages, and docs.racket-lang.org publishes no text archive, so a learner
# following docs/LANGUAGE_COURSE.md indexes HTML or nothing.
#
# What that cost, measured 2026-09-19 on /usr/racket/doc/guide:
#
#     Python text corpus (17 files)   first `docs search`    4 s
#     Racket HTML corpus (151 files)  first `docs search`   72 s
#
# and a retrieved passage came back wrapped in `<span class="RktSym">`, href URLs
# and `&ldquo;`. Retrieval still found the right section, so this is **cost and
# readability, not correctness** -- which is why it is worth fixing calmly and
# why this driver asserts the chunk's CONTENT rather than a timing.
#
# WHAT IS AND IS NOT COVERED:
#   checked -- the embeddings endpoint really ran (so nothing below is vacuous);
#              the HTML page is retrievable at all; the returned chunk carries
#              the PROSE and **no raw tags**; and markup-only tokens -- a class
#              name, a `<script>` body, a `<style>` body -- are ABSENT, which is
#              what distinguishes "reduced" from "printed without highlighting".
#   not checked -- entity decoding beyond what the reducer already does, and the
#              indexing TIME. Time is the reason the fix is wanted but it is a
#              bad assertion: it varies with the machine and with the embedder,
#              and a driver that fails on a slow bench teaches nothing.
#   not checked -- that a CODEBASE index leaves .html alone. That is the other
#              half of the decision and it is asserted by
#              `docs_html_scope_lint.sh` instead, without a model.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
docs=$(smoke_tmp)

# The prose is what a reader wants; every other token here exists ONLY inside
# markup, so finding one in a chunk proves the markup was indexed.
cat > "$docs/page.html" <<'EOF'
<!DOCTYPE html>
<html><head><title>Hooks</title>
<style>.zzstyleonly { color: red }</style>
<script>var zzscriptonly = 1;</script>
</head>
<body>
<div class="zzclassonly">
<p>hooks hooks hooks lifecycle: the prose a reader actually wants.</p>
<p>A <span class="RktSym">lambda</span> form can take optional arguments.</p>
<p>It accepts the &ldquo;rest&rdquo; of the arguments &mdash; see &sect;4.4.2&hellip;</p>
</div>
</body></html>
EOF

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

# --- 1: the run happened at all ---------------------------------------------
if [ -f "$tmp/req.1" ]; then
    t_ok "the embeddings endpoint was queried"
else
    t_fail "no request reached the mock (rc=$rc): $(head_bytes 200 < "$tmp/err")"
fi

# --- 2: the HTML page is retrievable ----------------------------------------
# Without this every check below could pass by retrieving the .md decoy.
if grep -q 'page\.html' "$tmp/out"; then
    t_ok "the HTML page was indexed and retrieved"
else
    t_fail "page.html was not retrieved at all: $(head_bytes 300 < "$tmp/out")"
fi

# --- 3: the prose survived ---------------------------------------------------
# A reducer that ate the content would pass check 4 perfectly.
if grep -q 'the prose a reader actually wants' "$tmp/out"; then
    t_ok "the prose survived the reduction"
else
    t_fail "the prose is gone -- the reduction removed the content, not the \
markup: $(head_bytes 300 < "$tmp/out")"
fi

# --- 4: no raw tags in what was indexed -------------------------------------
# THE CHECK THIS DRIVER EXISTS FOR, and the one that is red before the fix.
_tags=$(grep -cE '<span|<div|<p>|<html' "$tmp/out" 2>/dev/null | tr -d ' ')
[ -n "$_tags" ] || _tags=0
if [ "$_tags" -eq 0 ]; then
    t_ok "the indexed text carries no raw HTML tags"
else
    t_fail "$_tags line(s) of the retrieved chunk still carry raw HTML tags -- \
the markup is being embedded along with the prose, which is what costs the \
context and the time:
$(grep -nE '<span|<div|<p>|<html' "$tmp/out" | head -3)"
fi

# --- 5: markup-only tokens are absent ---------------------------------------
# Distinguishes "reduced" from "shown without the angle brackets": a class name
# and the bodies of <script>/<style> have no business in a documentation chunk.
_only=$(grep -cE 'zzclassonly|zzscriptonly|zzstyleonly' "$tmp/out" 2>/dev/null | tr -d ' ')
[ -n "$_only" ] || _only=0
if [ "$_only" -eq 0 ]; then
    t_ok "class names and <script>/<style> bodies are not in the indexed text"
else
    t_fail "markup-only token(s) reached the index ($_only line(s)): a class \
name or a script/style body is in the chunk, so the page was indexed as source \
rather than as documentation:
$(grep -nE 'zzclassonly|zzscriptonly|zzstyleonly' "$tmp/out" | head -3)"
fi

# --- 6: typographic entities are decoded ------------------------------------
# A documentation corpus is full of them: Racket's Guide writes its quotation
# marks as `&ldquo;`/`&rdquo;` and its dashes as `&mdash;`. Leaving them raw
# means a learner reads `&ldquo;rest&rdquo;` in a passage, and it means any
# future exact-quote check compares text the page does not contain. The numeric
# forms (`&#167;`) were already handled -- M524 added them after a reader got
# `&#167;3.6.2` out of the C standard -- and the NAMED typographic ones are the
# same defect with a different spelling.
_ents=$(grep -cE '&ldquo;|&rdquo;|&mdash;|&hellip;|&sect;' "$tmp/out" 2>/dev/null | tr -d ' ')
[ -n "$_ents" ] || _ents=0
if [ "$_ents" -eq 0 ]; then
    t_ok "named typographic entities are decoded, not left raw"
else
    t_fail "$_ents line(s) still carry raw named entities -- a reader sees
\`&ldquo;rest&rdquo;\` where the page shows quotation marks:
$(grep -nE '&ldquo;|&rdquo;|&mdash;|&hellip;|&sect;' "$tmp/out" | head -3)"
fi

t_done
