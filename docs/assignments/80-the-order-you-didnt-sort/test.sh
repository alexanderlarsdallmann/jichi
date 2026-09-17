#!/bin/sh
# Grader for task 80: an ordered map built twice -- a sorted array kept sorted
# on insert, and a binary search tree -- both correct to the byte under ASan
# and LeakSanitizer, and a MEASUREMENT of insert-heavy against query-heavy on
# the learner's machine, recorded with a machine, a method and a finding.
#
# WHAT IS AND IS NOT CHECKED. Both implementations run the same probe: 3,000
# random keys (with duplicates that must overwrite), every key found, absent
# keys absent, min and max, seven range walks compared against a reference
# computed by brute force (ascending, inclusive bounds, each key once, the
# empty range), and then the SAME probe on 3,000 SORTED keys -- because an
# unbalanced tree is still correct on sorted input, only slow, and a tree that
# breaks on it has a different bug. The measurement is checked for shape:
# machine, method, results rows, a finding that mentions sorted input, and a
# stated position on balancing and on deletion. Never for direction. The
# grader compiles bench.c against both files and does not run it: the numbers
# that matter are the learner's, from the learner's machine.
cd "$(dirname "$0")" || exit 1
trap 'rm -rf saprobe bstprobe bench _probe.c _err.txt' EXIT
cc --version >/dev/null 2>&1 || { echo "CANNOT RUN: a C compiler (cc) is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 77; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

# One probe, two builds: -DPFX=sa or -DPFX=bst renames the calls.
cat > _probe.c <<'PRB'
#include "ordered.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAT_(a, b) a##_##b
#define CAT(a, b) CAT_(a, b)
#define F(name) CAT(PFX, name)
#define MAP struct PFX

#define N 3000

struct seen { long keys[N + 8]; long vals[N + 8]; size_t n; int broken; long last; };

static void collect(long k, long v, void *ctx)
{
    struct seen *s = (struct seen *)ctx;
    if (s->n > 0 && k <= s->last) s->broken = 1;   /* not ascending, or a repeat */
    s->last = k;
    if (s->n < N + 8) { s->keys[s->n] = k; s->vals[s->n] = v; }
    s->n++;
}

static unsigned long lcg(unsigned long *s)
{
    *s = *s * 1103515245UL + 12345UL;
    return (*s >> 8) & 0xffffUL;   /* 65,536 possible keys: 3,000 draws WILL repeat */
}

/* Brute-force reference over the distinct keys we inserted. */
static long ref_keys[N]; static long ref_vals[N]; static size_t ref_n;
static void ref_put(long k, long v)
{
    size_t i;
    for (i = 0; i < ref_n; i++) if (ref_keys[i] == k) { ref_vals[i] = v; return; }
    ref_keys[ref_n] = k; ref_vals[ref_n] = v; ref_n++;
}
static size_t ref_range_count(long lo, long hi)
{
    size_t i, c = 0;
    for (i = 0; i < ref_n; i++) if (ref_keys[i] >= lo && ref_keys[i] <= hi) c++;
    return c;
}
static int ref_has(long k, long *v)
{
    size_t i;
    for (i = 0; i < ref_n; i++) if (ref_keys[i] == k) { *v = ref_vals[i]; return 1; }
    return 0;
}

static int check_range(MAP *m, long lo, long hi, struct seen *s)
{
    size_t want = ref_range_count(lo, hi);
    size_t got, j;
    memset(s, 0, sizeof *s);
    got = F(range)(m, lo, hi, collect, s);
    if (got != want || s->n != want) return 27;
    if (s->broken) return 28;
    for (j = 0; j < s->n && j < N + 8; j++) {
        long rv;
        if (s->keys[j] < lo || s->keys[j] > hi) return 29;
        if (!ref_has(s->keys[j], &rv) || rv != s->vals[j]) return 30;
    }
    return 0;
}

static int run(int sorted_input)
{
    MAP *m = F(new)();
    unsigned long seed = 4242UL;
    long i, v, mn, mx, refmn = 0, refmx = 0;
    size_t k;
    struct seen s;
    static const long RANGES[][2] = { {0, 65535}, {1000, 2000}, {30000, 30000},
                                      {70000, 80000}, {-5, 5}, {40000, 39999}, {65000, 70000} };

    ref_n = 0;
    if (m == NULL) return 21;
    for (i = 0; i < N; i++) {
        long key = sorted_input ? i * 3 : (long)lcg(&seed);
        if (F(put)(m, key, i) != 0) { F(free)(m); return 22; }
        ref_put(key, i);
    }
    if (F(count)(m) != ref_n) { F(free)(m); return 23; }   /* duplicates overwrote */
    for (k = 0; k < ref_n; k++) {
        if (!F(get)(m, ref_keys[k], &v) || v != ref_vals[k]) { F(free)(m); return 24; }
        if (ref_keys[k] < refmn || k == 0) refmn = ref_keys[k];
        if (ref_keys[k] > refmx || k == 0) refmx = ref_keys[k];
    }
    if (F(get)(m, 1000000L, &v)) { F(free)(m); return 25; }
    if (F(get)(m, -1L, &v)) { F(free)(m); return 25; }
    if (!F(min)(m, &mn) || mn != refmn) { F(free)(m); return 26; }
    if (!F(max)(m, &mx) || mx != refmx) { F(free)(m); return 26; }
    for (k = 0; k < sizeof RANGES / sizeof RANGES[0]; k++) {
        int rc = check_range(m, RANGES[k][0], RANGES[k][1], &s);
        if (rc) { F(free)(m); return rc; }
    }
    /* Bounds that ARE keys, so an off-by-one at either end cannot pass on the
       luck of the draw: the whole span, one key exactly, a span between two
       keys, and the span just inside them. */
    {
        long a = ref_keys[3] < ref_keys[9] ? ref_keys[3] : ref_keys[9];
        long b = ref_keys[3] < ref_keys[9] ? ref_keys[9] : ref_keys[3];
        int rc;
        if ((rc = check_range(m, refmn, refmx, &s)) != 0)         { F(free)(m); return rc; }
        if ((rc = check_range(m, ref_keys[0], ref_keys[0], &s)) != 0) { F(free)(m); return rc; }
        if ((rc = check_range(m, a, b, &s)) != 0)                 { F(free)(m); return rc; }
        if ((rc = check_range(m, a + 1, b - 1, &s)) != 0)         { F(free)(m); return rc; }
        if ((rc = check_range(m, refmx, refmx + 10, &s)) != 0)    { F(free)(m); return rc; }
    }
    F(free)(m);   /* LeakSanitizer judges this */
    return 0;
}

int main(void)
{
    int rc = run(0);
    if (rc != 0) return rc;
    rc = run(1);
    if (rc != 0) return rc + 20;   /* the sorted-input run's codes are +20 */
    {
        MAP *m = F(new)();
        long v;
        if (m == NULL) return 21;
        if (F(count)(m) != 0 || F(min)(m, &v) || F(max)(m, &v)) { F(free)(m); return 61; }
        if (F(range)(m, 0, 100, collect, NULL) != 0) { F(free)(m); return 61; }
        F(free)(m);
    }
    return 0;
}
PRB

explain() { # $1 which, $2 code
    c=$2; where=""
    if [ "$c" -gt 40 ] && [ "$c" -lt 61 ]; then c=$((c - 20)); where=" (on SORTED input -- the tree is allowed to be slow there, never wrong)"; fi
    case $c in
      21) echo "FAIL ($1): new() returned NULL" ;;
      22) echo "FAIL ($1): put returned -1 on a machine that has memory -- the stub, still" ;;
      23) echo "FAIL ($1): count is wrong -- a repeated key must OVERWRITE, not insert (the probe's 3,000 draws come from 65,536 keys and repeat)$where" ;;
      24) echo "FAIL ($1): a key that was put is not found, or has a stale value$where" ;;
      25) echo "FAIL ($1): an absent key was reported found$where" ;;
      26) echo "FAIL ($1): min or max is wrong$where" ;;
      27) echo "FAIL ($1): a range walk visited the wrong NUMBER of keys -- both bounds are inclusive, an empty range (lo > hi, or nothing inside) visits nothing$where" ;;
      28) echo "FAIL ($1): a range walk was not strictly ascending, or visited a key twice$where" ;;
      29) echo "FAIL ($1): a range walk visited a key outside [lo, hi]$where" ;;
      30) echo "FAIL ($1): a range walk reported a value that does not match the key's$where" ;;
      61) echo "FAIL ($1): an empty map must have count 0, no min, no max, and an empty range walk" ;;
      100) echo "FAIL ($1): does not compile as C89 under -Wall -Wextra against ordered.h" ;;
      101) echo "FAIL ($1): the sanitizer stopped the run. Its summary:"; grep -E "ERROR:|SUMMARY:" _err.txt | sed 's/^/    /' | head -3 ;;
      *)  echo "FAIL ($1): the probe exited $2 -- it was killed (a recursion N deep on sorted input, perhaps)"; sed 's/^/    /' _err.txt | head -6 ;;
    esac
}

run_probe() { # $1 pfx, $2 impl, $3 out
    $CC $SAN -DPFX=$1 -o "$3" _probe.c "$2" 2>/dev/null || return 100
    "./$3" 2>_err.txt
    rc=$?
    grep -qE "(AddressSanitizer|LeakSanitizer|runtime error)" _err.txt && return 101
    return $rc
}

run_probe sa sorted.c saprobe; rc=$?
[ $rc -eq 0 ] || { explain "sorted array" $rc; exit 1; }
run_probe bst bst.c bstprobe; rc=$?
[ $rc -eq 0 ] || { explain "tree" $rc; exit 1; }

$CC -std=c89 -pedantic -Wall -Wextra -O2 -o bench bench.c sorted.c bst.c 2>/dev/null || {
    echo "FAIL: bench.c does not compile against your two files -- the measurement cannot be made"; exit 1; }

[ -f MEASURE.md ] || { echo "FAIL: MEASURE.md is missing -- run ./bench and write down what you saw, with the machine and the method"; exit 1; }
for sec in "Machine" "Method" "Results" "Finding" "Balancing" "Deletion"; do
    grep -q "^## $sec" MEASURE.md || { echo "FAIL: MEASURE.md has no '## $sec' section"; exit 1; }
done
rows=$(awk '/^## Results/{f=1;next} /^## /{f=0} f' MEASURE.md | grep -cE '^\| *[0-9]+ *\|.*[0-9].*\|.*[0-9]')
if [ "$rows" -lt 3 ]; then
    echo "FAIL: '## Results' needs at least three rows beginning | N | with numbers in them (found $rows) -- paste what ./bench printed"
    exit 1
fi
if ! awk '/^## Finding/{f=1;next} /^## /{f=0} f' MEASURE.md | grep -qi 'sorted'; then
    echo "FAIL: '## Finding' must say what SORTED input did to the tree -- that row is the point of running it"
    exit 1
fi
if ! awk '/^## Balancing/{f=1;next} /^## /{f=0} f' MEASURE.md | grep -qiE 'avl|red-black|red black|treap|skip|b-tree|btree|unbalanced|not balanced|no balancing'; then
    echo "FAIL: '## Balancing' must state your position -- unbalanced and why that is acceptable here, or which balanced form you would reach for"
    exit 1
fi
if ! grep -qiE 'cpu|core|ghz|model|machine|laptop|desktop|arm|x86|apple|intel|amd' MEASURE.md; then
    echo "FAIL: '## Machine' should say what you measured on -- a number without its machine is not a measurement"
    exit 1
fi

echo "PASS: both ordered maps are correct on random and on sorted input under ASan/LSan, and the measurement is recorded with its machine, method, finding and positions"
exit 0
