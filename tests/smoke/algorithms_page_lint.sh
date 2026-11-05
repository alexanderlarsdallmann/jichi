#!/bin/sh
# smoke lint: the measurements docs/ALGORITHMS.md publishes still come out of this tree
# (M755).
#
# WHY. The page teaches four of jichi's algorithms by having the reader compile a
# small program against the real source file and read what it prints, and it
# pastes what those programs printed on the day it was written. A page like that
# rots in two ways nothing else would catch: the program stops compiling (a
# function renamed, a header moved), or the claim it makes stops being true (the
# SSE parser learns bare CR, and the page still says it does not). CLAUDE.md's rule
# is "run every command you publish, in the form you publish it", so this does.
#
# HOW. Each lesson is a `Save this as `/tmp/NAME.c`` sentence, the ```c block
# after it, and the ```sh block after that. This extracts all three, rewrites the
# page's /tmp/ to its own scratch directory, and runs the commands from the root of
# the checkout, exactly as the page tells a reader to.
#
# WHAT IS CHECKED is only what does not depend on how large the tree has grown:
# the four programs build, the edit-distance suggestions the page prints, the SSE
# parser's chunk-size property and its bare-CR behaviour, and BM25's saturation.
# The hash and ranking FIGURES (2,112 paths, 7.02 for DATA_STRUCTURES.md) are
# dated on the page and are not held here: they are meant to change.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
root=$SMOKE_ROOT
page="$root/docs/ALGORITHMS.md"

# Extract: NAME.c from the ```c block after "Save ... `/tmp/NAME.c`", and NAME.sh
# from the ```sh block after it, with /tmp/ pointing at $tmp.
awk -v out="$tmp" '
  /Save (this )?as `\/tmp\/[a-z0-9_]+\.c`/ {
      n = $0; sub(/.*`\/tmp\//, "", n); sub(/\.c`.*/, "", n); name = n; want = "c"; next }
  want == "c"  && /^```c$/  { f = out "/" name ".c"; incode = 1; next }
  want == "sh" && /^```sh$/ { f = out "/" name ".sh"; incode = 1; next }
  incode && /^```$/ { close(f); incode = 0; want = (want == "c") ? "sh" : ""; next }
  incode { line = $0; if (want == "sh") gsub(/\/tmp\//, out "/", line); print line > f }
' "$page"
progs=$(cd "$tmp" && ls *.c 2>/dev/null | sed 's/\.c$//' | sort | tr '\n' ' ')
nsh=$(ls "$tmp"/*.sh 2>/dev/null | wc -l | tr -d ' ')

# --- 1: the page still has its four programs, each with its commands ----------
if [ "$progs" = "bm ed hc sse " ] && [ "$nsh" -eq 4 ]; then
    t_ok "extracted the page's four programs and their commands ($progs)"
else
    t_fail "extracted programs '$progs' and $nsh command blocks; the page's lessons or their 'Save as' sentences changed shape"
fi

# --- 2: all four build with the page's own command line ------------------------
bad=""
for p in bm ed hc sse; do
    cc_line=$(grep '^cc ' "$tmp/$p.sh" 2>/dev/null | head -1)
    if [ -z "$cc_line" ] || ! (cd "$root" && sh -c "$cc_line") > "$tmp/$p.build" 2>&1; then
        bad="$bad $p"
    fi
done
if [ -z "$bad" ]; then
    t_ok "the four programs build with the commands the page prints"
else
    t_fail "did not build:$bad -- $(head_bytes 200 "$tmp/$(echo $bad | cut -d' ' -f1).build")"
fi

# run NAME: every non-cc line of NAME.sh, from the root, output to NAME.out
run() {
    grep -v '^cc ' "$tmp/$1.sh" > "$tmp/$1.run"
    (cd "$root" && sh "$tmp/$1.run") > "$tmp/$1.out" 2>&1
}

# --- 3: the edit-distance lesson prints what the page says ---------------------
run ed
if grep -qx 'modle   -> mode' "$tmp/ed.out" && grep -qx 'h       -> (silence)' "$tmp/ed.out" \
   && grep -qx 'hlep    -> help' "$tmp/ed.out"; then
    t_ok "edit distance: modle -> mode, hlep -> help, and h stays silent, as the page says"
else
    t_fail "the edit-distance program printed something else: $(tr '\n' ';' < "$tmp/ed.out")"
fi

# --- 4: the SSE lesson: chunk sizes agree, and a bare CR ends a line (M775) -----
run sse
if grep -q 'that disagree with one read: 0$' "$tmp/sse.out" \
   && grep -q 'bare CR: 1 event(s)$' "$tmp/sse.out"; then
    t_ok "SSE: every chunk size agrees with one read, and a bare CR ends a line (M775)"
else
    t_fail "the SSE program printed something else -- the every-chunk-size property must hold (0 disagreements) and a bare CR must end a line (1 event, since M775); if it reverted to 0 events that is a regression: $(tr '\n' ';' < "$tmp/sse.out")"
fi

# --- 5: the ranking lesson: repetition saturates --------------------------------
run bm
scores=$(sed -n 's/^  r= *[0-9]*  score *\([0-9.]*\).*/\1/p' "$tmp/bm.out" | tr '\n' ' ')
if printf '%s\n' "$scores" | awk '{ if (NF < 5) exit 1
        for (i = 2; i <= NF; i++) if ($i + 0 < $(i-1) + 0) exit 1
        if ($NF - $(NF-1) >= 0.1 * $(NF-1)) exit 1 }'; then
    t_ok "BM25: the repeated term's score never falls and saturates ($scores)"
else
    t_fail "the saturation table does not saturate: '$scores'"
fi

# --- 6: the hashing lesson runs on the tracked paths ----------------------------
if ! (cd "$root" && git rev-parse --git-dir) > /dev/null 2>&1; then
    t_skip_one "not a git checkout: the hashing lesson pipes git ls-files, and there is nothing to list"
else
    run hc
    if grep -q '^[0-9][0-9]* strings$' "$tmp/hc.out" \
       && [ "$(grep -c 'buckets: longest chain' "$tmp/hc.out")" -eq 6 ]; then
        t_ok "hashing: the program reads the tracked paths and reports all six tables ($(head -1 "$tmp/hc.out"))"
    else
        t_fail "the hashing program printed something else: $(head_bytes 200 "$tmp/hc.out")"
    fi
fi

t_done
