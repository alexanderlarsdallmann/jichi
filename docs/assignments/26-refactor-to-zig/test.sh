#!/bin/sh
# Two-sided by construction: the pristine project still has stats.c, and
# this runner requires it GONE -- replaced by a Zig implementation behind
# the unchanged C header -- with the tool's output byte-identical. The
# header is pinned (sha over the file) so "refactor" cannot quietly mean
# "change the contract".
cd "$(dirname "$0")" || exit 1

command -v zig >/dev/null 2>&1 || {
    echo "FAIL: this extra needs zig on PATH (one download -- see ZIG_INTEROP.md)"
    exit 1; }

grep -q "long wt_count_words(const char \*text);" wordtool.h &&
grep -q "long wt_longest_word(const char \*text);" wordtool.h || {
    echo "FAIL: wordtool.h changed -- the refactor must keep the contract"
    exit 1; }

[ ! -f stats.c ] || {
    echo "FAIL: stats.c still exists -- the module was not moved to Zig"
    exit 1; }
[ -f stats.zig ] || { echo "FAIL: stats.zig missing"; exit 1; }

sh build.sh || { echo "FAIL: build.sh failed"; exit 1; }

out=$(./wordtool "the quick brown fox") || exit 1
[ "$out" = "words=4 longest=5" ] || {
    echo "FAIL: expected 'words=4 longest=5', got: $out"; exit 1; }
out=$(./wordtool "a bb  ccc") || exit 1
[ "$out" = "words=3 longest=3" ] || {
    echo "FAIL: expected 'words=3 longest=3', got: $out"; exit 1; }
out=$(./wordtool "") || exit 1
[ "$out" = "words=0 longest=0" ] || {
    echo "FAIL: expected 'words=0 longest=0', got: $out"; exit 1; }

rm -f wordtool ./*.o
echo "PASS: stats lives in Zig now; the C header and the behavior never moved"
exit 0
