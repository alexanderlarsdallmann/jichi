#!/bin/sh
# smoke lint: every SIDE (non-model) HTTP call carries the connect + dead-stream
# fences (M1093, finding C-09).
#
# The model call (src/chat/jc_agent.c) already resolves its connect, stall,
# request and dead-stream timeouts from config (jc_config_resolve_timeouts) and
# sets them inline -- that is the correct shape. The ~21 SIDE calls (embeddings/
# rerank, model listing, image/audio/transcribe, MCP-over-HTTP, compaction,
# one-shot, web search, feed/doc fetch, the two helper completions, the TUI's
# inline suggestion) set only a fixed TOTAL cap (timeout_secs) and none of the
# FENCES, so with no connect bound libcurl waits up to 300 s on a dead endpoint
# and a silent stream after connecting is never cut.
#
# The rule this lint holds (the house principle "a rule about timeouts must
# hold at every line that sets one, not only the line that was measured"):
# for EVERY side site, the enclosing function must reference the connect fence
# -- either `connect_timeout_secs` directly or through the shared helper
# `jc_net_side_fences` (which sets connect + dead_stream, never the total).
#
# UNIVERSE: every `<name>.timeout_secs =` assignment in src/ (all .c files),
# EXCLUDING src/chat/jc_agent.c (the model call sets its own fences inline).
# Every site today is a local `req`; the pattern matches any object member
# `.timeout_secs =` (the `req.` prefix is not required, so a renamed request
# variable is still found). The floor is the exact site count today (21): a new
# side call must get the fence (or the count and this lint change together), and
# a deleted call must lower the floor on purpose. Both are "read the site".
#
# ENCLOSING FUNCTION: a char-level awk pass strips comments and strings and
# reports each top-level function body as a "start<TAB>end" range. A top-level
# `{` is a function body iff the last code char before it was `)` (covers the
# K&R signature-above-brace and the same-line `void f(void){` forms); struct /
# enum / namespace bodies do not end their declarator in `)` and are skipped.
# This is what lets the lint find the right function in files that open a brace
# at column 0 for a struct closer (jc_scaffold.c has 235 of them).
#
# Red-first, recorded: run against the UNFIXED tree this lint reports all 21
# sites missing the fence (the 13 files then carry no connect_timeout_secs and
# no jc_net_side_fences reference). After the fix it is green at the floor 21.
# The lint is the permanent instrument; a functional "delayed reply" test would
# need a 300 s libcurl connect wait and is out of scope for the smoke tier.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
tmp=$(smoke_tmp)

# The char-level function-range finder (see header for why the `)` test).
cat > "$tmp/funcref.awk" <<'AWK'
{
  line = $0; ln = NR; n = length(line); i = 1
  while (i <= n) {
    c = substr(line, i, 1)
    d = (i < n) ? substr(line, i + 1, 1) : ""
    if (mode == "bcomment") {
      if (c == "*" && d == "/") { mode = ""; i += 2; continue }
      i++; continue
    }
    if (mode == "string") {
      if (c == "\\") { i += 2; continue }
      if (c == "\"") mode = ""
      i++; continue
    }
    if (mode == "char") {
      if (c == "\\") { i += 2; continue }
      if (c == "'") mode = ""
      i++; continue
    }
    if (c == "/" && d == "*") { mode = "bcomment"; i += 2; continue }
    if (c == "/" && d == "/") { break }
    if (c == "\"") { mode = "string"; i++; continue }
    if (c == "'") { mode = "char"; i++; continue }
    if (c == "{") {
      if (depth == 0) { isf = (lastchar == ")"); funcstart = isf ? ln : 0 }
      depth++; fflag[depth] = (depth == 1) ? isf : 0
      lastchar = "{"
    } else if (c == "}") {
      depth--
      if (depth == 0) { if (fflag[1] == 1) print funcstart "\t" ln; fflag[1] = 0 }
      lastchar = "}"
    } else if (c != " " && c != "\t" && c != "\r") { lastchar = c }
    i++
  }
}
AWK

# SV:0559 R5: a comment that MENTIONS jc_net_side_fences used to satisfy the
# per-function match (the loop grepped the raw lines, comments included). This
# second char-level pass strips comments and string/char literals to spaces
# (one line per input line, positions preserved) so the match only sees CODE.
cat > "$tmp/strip.awk" <<'AWK'
{
  # A line comment cannot span a newline (a block comment can, and its state
  # must persist); reset linecomment at the top of every record so the next
  # line is not swallowed whole.
  if (mode == "linecomment") mode = ""
  line = $0; n = length(line); i = 1; out = ""
  while (i <= n) {
    c = substr(line, i, 1)
    d = (i < n) ? substr(line, i + 1, 1) : ""
    # A UTF-8 multi-byte sequence is ONE token to the C lexer, not a run of
    # quote bytes: the project header carries "Gießen" and a byte-level scan
    # of its 0xC3/0xA4 reads the bytes as string delimiters, unbalancing the
    # quote mode and blanking every later line of code (SV:0559 R5 probe
    # caught this -- 21 MISS instead of 1). Skip a whole sequence (0xC2-0xDF
    # lead -> 2 bytes, 0xE0-0xEF -> 3, 0xF0-0xF4 -> 4) as a single space.
    if (c >= "\200" && c < "\240" && i + 1 <= n) { out = out " "; i += 2; continue }
    if (c >= "\240" && c < "\340" && i + 2 <= n) { out = out " "; i += 3; continue }
    if (c >= "\340" && c <= "\365" && i + 3 <= n) { out = out " "; i += 4; continue }
    if (mode == "bcomment") {
      if (c == "*" && d == "/") { out = out "  "; i += 2; mode = ""; continue }
      out = out " "; i++; continue
    }
    if (mode == "linecomment") { break }
    if (mode == "string") {
      if (c == "\\") { out = out "  "; i += 2; continue }
      if (c == "\"") mode = ""
      out = out " "; i++; continue
    }
    if (mode == "char") {
      if (c == "\\") { out = out "  "; i += 2; continue }
      if (c == "'") mode = ""
      out = out " "; i++; continue
    }
    if (c == "/" && d == "*") { mode = "bcomment"; out = out "  "; i += 2; continue }
    if (c == "/" && d == "/") { mode = "linecomment"; break }
    if (c == "\"") { mode = "string"; out = out "\" "; i++; continue }
    if (c == "'") { mode = "char"; out = out "' "; i++; continue }
    out = out c; i++
  }
  print out
}
AWK

# The universe: every .timeout_secs assignment in src/, minus the model call.
# /dev/null as a fixed operand: grep prints file names only when given 2+
# files, so if xargs ever batches a lone source file the line would lose its
# file name and the parse below would skip the site silently (SV:2138; the
# sibling lints' idiom).
smoke_srcfiles "$SMOKE_ROOT/src" c | xargs grep -nE '\.timeout_secs[[:space:]]*=' /dev/null 2>/dev/null \
    | grep -v '/jc_agent\.c:' > "$tmp/sites" || : > "$tmp/sites"
nsites=$(grep -c . "$tmp/sites")

# Floor at today's exact count (CLAUDE.md "audit the universe" tripwire). A new
# side call must bump this (or get the fence AND the count); a vanished one must
# lower it deliberately.
if [ "$nsites" -eq 21 ]; then
    t_ok "scanning $nsites side HTTP call sites (universe = every src .timeout_secs; floor 21)"
else
    t_fail "found $nsites side sites, floor is 21 -- a side call was added or deleted; read each and update the floor + this header"
fi

# Per site: compute its file's function ranges once, find the range containing
# the site, and check that range names the connect fence.
: > "$tmp/res"
lastfile=""
while IFS=: read -r file line _; do
    [ -f "$file" ] || continue
    if [ "$file" != "$lastfile" ]; then
        awk -f "$tmp/funcref.awk" "$file" > "$tmp/ranges"
        awk -f "$tmp/strip.awk" "$file" > "$tmp/stripped"
        lastfile=$file
    fi
    range=$(awk -F'\t' -v L="$line" '$1 <= L && L <= $2 { print $1 "-" $2; exit }' "$tmp/ranges")
    if [ -n "$range" ]; then
        if sed -n "${range%-*},${range#*-}p" "$tmp/stripped" \
                | grep -qE 'connect_timeout_secs|jc_net_side_fences'; then
            echo "ok ${file}:${line} (func ${range})" >> "$tmp/res"
        else
            echo "MISS ${file}:${line} (func ${range})" >> "$tmp/res"
        fi
    else
        echo "MISS ${file}:${line} (no enclosing function found)" >> "$tmp/res"
    fi
done < "$tmp/sites"

nmiss=$(grep '^MISS' "$tmp/res" 2>/dev/null | wc -l)
if [ "$nmiss" -eq 0 ]; then
    t_ok "every side call site sets the connect + dead-stream fence (directly or via jc_net_side_fences)"
else
    t_fail "$nmiss side call site(s) lack the connect fence -- add jc_net_side_fences(&req, cfg) (or set connect_timeout_secs directly):"
    grep '^MISS' "$tmp/res" | sed 's/^/# /' | head -25
fi

t_done
