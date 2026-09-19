#!/bin/sh
# smoke lint: HTML reduction is OPT-IN, and every caller says which it wants (M667).
#
# THE DECISION THIS PINS. `jc_index_build`'s `with_html` reduces .html/.htm to
# prose before chunking. A DOCS index wants that: indexing Racket's Scribble
# pages as source embedded `<span class="RktSym">` and href URLs along with the
# sentences. A CODEBASE index wants the opposite — in a web project the markup
# IS the code, and stripping it would make a file unsearchable by the very thing
# its author was looking for. The flag exists so the caller states which, exactly
# as `pdf_cmd` already distinguishes the two for PDFs.
#
# WHY A LINT AND NOT A DRIVER. The docs half is driven for real by
# `docs_html.sh`, which indexes an HTML fixture through a mock embedder and
# asserts the chunk carries prose and no tags. The CODEBASE half has no such
# cheap observation — asserting "the markup survived" needs a second live index
# over a second corpus for a property that is decided by one argument. So the
# argument is what gets checked, which is also the thing a future refactor would
# get wrong: adding a call site and copying the wrong neighbour.
#
# THE UNIVERSE is every call to jc_index_build in first-party C. Calls are
# joined across lines first, because this tree wraps them and a line-based
# extraction would see an argument list that is not there — the same mistake
# that made two other lints in this tier vacuous in the same week.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
G=/usr/bin/grep
[ -x "$G" ] || G=grep
cd "$SMOKE_ROOT" || exit 1

# arg 4 (pdf_cmd) classifies the call; arg 5 (with_html) is what is asserted.
_calls=$(find src -name '*.c' -type f 2>/dev/null | sort | while read -r f; do
    awk -v F="$f" '
        { buf = buf $0 " " }
        END {
            n = split(buf, _dummy, "")   # force buf to exist
            s = buf
            while ((i = index(s, "jc_index_build(")) > 0) {
                s = substr(s, i + length("jc_index_build("))
                depth = 1; arg = ""; argn = 1; a4 = ""; a5 = ""
                for (j = 1; j <= length(s); j++) {
                    c = substr(s, j, 1)
                    if (c == "(") { depth++ }
                    else if (c == ")") { depth--; if (depth == 0) { break } }
                    if (depth == 1 && c == ",") {
                        if (argn == 4) { a4 = arg }
                        if (argn == 5) { a5 = arg }
                        argn++; arg = ""
                        continue
                    }
                    arg = arg c
                }
                if (argn == 5) { a5 = arg }
                gsub(/^[ \t]+|[ \t]+$/, "", a4)
                gsub(/^[ \t]+|[ \t]+$/, "", a5)
                printf "%s|%s|%s\n", F, a4, a5
            }
        }' "$f"
done)

_n=$(printf '%s' "$_calls" | $G -c . | tr -d '[:space:]')
[ -n "$_calls" ] || _n=0

# ---- 1: the universe is the one measured ----------------------------------
# Floored at today's count: an extraction that finds nothing would make checks
# 2 and 3 pass by having nothing to judge, which is this tier's most-repeated
# way of being green and wrong.
if [ "${_n:-0}" -ge 6 ]; then
    t_ok "$_n jc_index_build call site(s) parsed (floor 6)"
else
    t_fail "only ${_n:-0} jc_index_build call site(s) parsed (want >= 6) -- the \
extraction broke, so the two checks below judge nothing:
$_calls"
fi

# ---- 2: a CODEBASE index never strips HTML --------------------------------
_bad_code=$(printf '%s\n' "$_calls" | awk -F'|' '$2 == "NULL" && $3 != "0"')
if [ -z "$_bad_code" ]; then
    t_ok "every codebase index (pdf_cmd = NULL) passes with_html = 0"
else
    t_fail "a codebase index asks for HTML reduction. In a web project the markup
IS the code, and stripping it makes the file unsearchable by what its author was
looking for:
$_bad_code"
fi

# ---- 3: a DOCS index always does --------------------------------------------
_bad_docs=$(printf '%s\n' "$_calls" | awk -F'|' '$2 ~ /jc_pdf_command/ && $3 != "1"')
if [ -z "$_bad_docs" ]; then
    t_ok "every docs index (pdf_cmd = jc_pdf_command(...)) passes with_html = 1"
else
    t_fail "a docs index indexes HTML as source. Whole projects ship their
documentation as HTML and publish no text archive, so this is the difference
between a usable corpus and a pile of markup:
$_bad_docs"
fi

t_done
