#!/bin/sh
# smoke lint: a fenced block in docs/reading/ that claims to come from a file
# must still be in that file, line for line (M508).
#
# WHY THIS EXISTS. reading_refs_lint.sh holds the guides' ANCHORS to the tree
# (`file.c:function` must resolve; line numbers are banned outright) and
# deliberately skips fenced blocks: "excerpts ... may legitimately show elided
# or historical code". That exemption was right while the guides quoted almost
# nothing -- 3 C blocks across 22 chapters. The trace chapters (Tsuiseki)
# quote for a living: source, and the artifacts of a recorded run. An
# unchecked quote is the worst kind of rot, because it is the most convincing
# thing on the page.
#
# THE CONVENTION. Tag the opening fence with the file the block came from:
#
#     ```c src/chat/jc_message.c:jc_history_add_tool_result
#     ```jsonl docs/reading/traces/tool-round/expected/stdout.jsonl
#
# The tag is the language, then the path, optionally `:function`. Untagged
# blocks (pseudo code, shell sessions, invented examples) are ignored -- this
# lint is opt-in per block, because "every block must be verbatim" would ban
# the pseudo code the beginner guide is built on.
#
# WHAT IS CHECKED: the path exists; a named function is defined-or-used there;
# every line of the block that is not blank and not an elision marker (a line
# whose only content is `...`, `/* ... */` or `# ...`) appears VERBATIM as a
# line of that file.
#
# ALSO CHECKED, since M509: that a `:function` tag names the function the block
# is actually IN -- not merely a function that exists in the file. This check
# was written because an audit found three tags that named the wrong one (the
# callee instead of the caller, `path_cmp` for a `qsort` call inside
# `render_listing`), all of which the name-exists rule passed. A tag is a
# navigation instruction; one that sends the reader to the wrong function is
# worse than none, because they will believe it.
#
# WHAT IS NOT CHECKED, stated rather than implied (the M305 rule): order,
# adjacency, and context. Ten lines that each exist somewhere in the file pass
# even if the code was reordered between them, and a quoted `}` matches
# trivially. This catches the rot that actually happens -- a renamed
# identifier, a reworded comment, a re-taken trace artifact -- and it cannot
# tell you the excerpt still MEANS what the prose says it means. A reader has
# to do that (docs/TEST_INTEGRITY.md).
#
# Runs no jichi and compiles nothing (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
tmp=$(smoke_tmp)

# The count at M509 (all four Tsuiseki chapters). A floor exists so a broken
# extraction fails loudly instead of reporting OK over zero blocks -- two
# empty sets agree perfectly (the M295 lesson).
FLOOR=41

guides=$(ls "$SMOKE_ROOT/docs/reading/"*.md 2>/dev/null)
nfiles=$(printf '%s\n' "$guides" | grep -c .)
if [ "$nfiles" -ge 2 ]; then
    t_ok "scanning $nfiles reading-guide files for tagged quotes"
else
    t_fail "no reading guides found under docs/reading/ -- layout moved?"
fi

# --- extraction --------------------------------------------------------------
# One line per tagged block in $tmp/index (doc, doc line, tag, body file), the
# block's lines in $tmp/b<N>.
: > "$tmp/index"
nb=0
for g in $guides; do
    nb=$(awk -v OUT="$tmp" -v F="$g" -v NB="$nb" '
        function is_repo_path(p) {
            return p ~ /^(src|include|tests|docs|examples|man|editors)\//
        }
        /^```/ {
            if (state != 0) { state = 0; next }
            info = substr($0, 4)
            n = split(info, a, /[ \t]+/)
            if (n >= 2 && is_repo_path(a[2])) {
                NB++
                state = 1
                body = OUT "/b" NB
                printf "%s\t%d\t%s\t%s\n", F, FNR, a[2], body >> (OUT "/index")
            } else {
                state = 2
            }
            next
        }
        state == 1 { print >> body }
        END { print NB }
    ' "$g")
done
if [ "$nb" -ge "$FLOOR" ]; then
    t_ok "found $nb tagged quote block(s) (floor $FLOOR)"
else
    t_fail "found only $nb tagged quote blocks (floor $FLOOR) -- the extraction broke; fix it rather than the floor"
fi

# --- 3: the tags resolve -----------------------------------------------------
: > "$tmp/badtags"
while IFS="$(printf '\t')" read -r df dl tag bf; do
    path=${tag%%:*}
    fn=${tag#*:}
    [ "$fn" = "$tag" ] && fn=""
    if [ ! -f "$SMOKE_ROOT/$path" ]; then
        printf '%s:%s: tagged path does not exist: %s\n' "$df" "$dl" "$path" >> "$tmp/badtags"
        continue
    fi
    if [ -n "$fn" ] && ! grep -q "$fn(" "$SMOKE_ROOT/$path"; then
        printf '%s:%s: %s not found in %s (renamed?)\n' "$df" "$dl" "$fn" "$path" >> "$tmp/badtags"
    fi
done < "$tmp/index"
if [ ! -s "$tmp/badtags" ]; then
    t_ok "every quote tag names a file that exists"
else
    t_fail "unresolvable quote tag(s) ($(grep -c . "$tmp/badtags")):"
    sed 's/^/# /' "$tmp/badtags" | head -10
fi

# --- 4: the quoted lines are still there -------------------------------------
: > "$tmp/stale"
while IFS="$(printf '\t')" read -r df dl tag bf; do
    path=${tag%%:*}
    [ -f "$SMOKE_ROOT/$path" ] || continue
    [ -f "$bf" ] || continue
    while IFS= read -r line; do
        bare=$(printf '%s' "$line" | tr -d ' 	')
        case "$bare" in
            ''|'...'|'/*...*/'|'#...') continue ;;
        esac
        if ! grep -Fqx -- "$line" "$SMOKE_ROOT/$path"; then
            printf '%s:%s: not in %s: %s\n' "$df" "$dl" "$path" \
                "$(printf '%s' "$line" | head_bytes 90)" >> "$tmp/stale"
        fi
    done < "$bf"
done < "$tmp/index"
if [ ! -s "$tmp/stale" ]; then
    t_ok "every tagged quote is still verbatim in its file"
else
    t_fail "stale quote line(s) ($(grep -c . "$tmp/stale")):"
    sed 's/^/# /' "$tmp/stale" | head -12
fi

# --- 5: a :function tag names the function the block is IN ------------------
# For the block's first quotable line, every place that line occurs in the file
# is walked back to the nearest column-0 definition; the tag must match one of
# them. Any-occurrence rather than first-occurrence deliberately: a line like
# `    if (path == NULL) {` occurs in several functions and the block may quote
# any of them, so demanding the first would report a defect that is not there.
: > "$tmp/misplaced"
while IFS="$(printf '\t')" read -r df dl tag bf; do
    path=${tag%%:*}
    fn=${tag#*:}
    [ "$fn" = "$tag" ] && continue
    [ -f "$SMOKE_ROOT/$path" ] || continue
    [ -f "$bf" ] || continue
    target=""
    while IFS= read -r line; do
        bare=$(printf '%s' "$line" | tr -d ' 	')
        case "$bare" in
            ''|'...'|'/*...*/'|'#...') continue ;;
        esac
        target=$line
        break
    done < "$bf"
    [ -n "$target" ] || continue
    # Every enclosing definition line, one per occurrence of $target.
    if awk -v target="$target" '
        /^[A-Za-z_][A-Za-z0-9_ \t*]*\(/ { def = $0 }
        $0 == target { print def }
    ' "$SMOKE_ROOT/$path" \
      | sed 's/(.*//; s/[^A-Za-z0-9_]*$//; s/.*[^A-Za-z0-9_]//' \
      | grep -qx "$fn"
    then
        :
    else
        printf '%s:%s: %s is not the function this block sits in\n' \
            "$df" "$dl" "$tag" >> "$tmp/misplaced"
    fi
done < "$tmp/index"
if [ ! -s "$tmp/misplaced" ]; then
    t_ok "every :function tag names the function its block is in"
else
    t_fail "misplaced tag(s) ($(grep -c . "$tmp/misplaced")):"
    sed 's/^/# /' "$tmp/misplaced" | head -10
fi

t_done
