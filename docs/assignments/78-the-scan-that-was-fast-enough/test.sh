#!/bin/sh
# Grader for task 78: a hash table that is CORRECT under collisions, deletion
# and key ownership -- and a MEASUREMENT of it against the linear scan,
# recorded with a machine, a method and a stated crossover.
#
# WHAT IS AND IS NOT CHECKED. Correctness is checked by a probe under
# AddressSanitizer and LeakSanitizer: 5,000 keys into 64 buckets (collisions on
# almost every insert), overwrite keeps the count, the table owns a copy of the
# key, deletion removes exactly one key and leaves every other one findable,
# re-insertion after deletion works, and nothing leaks. The measurement is
# checked for its SHAPE -- a machine, a method, at least three N rows with both
# numbers, a crossover statement, a deletion position -- and never for which
# way it came out. A crossover below jichi's N is a finding, not a failure; the
# brief says why. bench.c is compiled against your table so you can run it, but
# the grader does not time anything: timings in a grader are the flaky check
# nobody trusts, and the number that matters is the one from YOUR machine.
cd "$(dirname "$0")" || exit 1
trap 'rm -rf htprobe bench _probe.c _err.txt' EXIT
cc --version >/dev/null 2>&1 || { echo "CANNOT RUN: a C compiler (cc) is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 77; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

cat > _probe.c <<'PRB'
#include "lookup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NKEYS 5000L

static void mk(char *b, long i) { b[0] = 'k'; sprintf(b + 1, "%06ld", i); }

int main(void)
{
    struct ht *t;
    char key[32];
    char mutable_key[32];
    long i, v;

    t = ht_new(64);
    if (t == NULL) return 21;

    /* 0. FIRST, before the table is full of the probe's one key buffer: the
          table owns a COPY of the key. Scribble on the caller's buffer after
          the put, then ask with a fresh spelling. A table that stored the
          pointer fails here with its own message instead of failing every
          later count for a reason it cannot see. */
    strcpy(mutable_key, "owned-key");
    if (ht_put(t, mutable_key, 99) != 0) { ht_free(t); return 22; }
    strcpy(mutable_key, "SCRIBBLED");
    if (!ht_get(t, "owned-key", &v) || v != 99) { ht_free(t); return 28; }
    if (ht_get(t, "SCRIBBLED", &v)) { ht_free(t); return 28; }
    if (!ht_del(t, "owned-key")) { ht_free(t); return 29; }

    /* 1. 5,000 keys into 64 buckets: every one must come back. */
    for (i = 0; i < NKEYS; i++) {
        mk(key, i);
        if (ht_put(t, key, i) != 0) { ht_free(t); return 22; }
    }
    if (ht_count(t) != (size_t)NKEYS) { ht_free(t); return 23; }
    for (i = 0; i < NKEYS; i++) {
        mk(key, i);
        if (!ht_get(t, key, &v) || v != i) { ht_free(t); return 24; }
    }
    /* an absent key is absent, not a neighbour */
    mk(key, NKEYS + 7);
    if (ht_get(t, key, &v)) { ht_free(t); return 25; }

    /* 2. overwrite: the value changes, the count does not. */
    mk(key, 17);
    if (ht_put(t, key, 4242) != 0) { ht_free(t); return 22; }
    if (!ht_get(t, key, &v) || v != 4242) { ht_free(t); return 26; }
    if (ht_count(t) != (size_t)NKEYS) { ht_free(t); return 27; }

    /* 4. delete every even key: the odd ones must ALL survive (a blanked slot
          under open addressing breaks the chain that ran through it). */
    for (i = 0; i < NKEYS; i += 2) {
        mk(key, i);
        if (!ht_del(t, key)) { ht_free(t); return 29; }
    }
    if (ht_count(t) != (size_t)(NKEYS / 2)) { ht_free(t); return 30; }
    for (i = 1; i < NKEYS; i += 2) {
        mk(key, i);
        if (!ht_get(t, key, &v) || v != (i == 17 ? 4242 : i)) { ht_free(t); return 31; }
    }
    for (i = 0; i < NKEYS; i += 2) {
        mk(key, i);
        if (ht_get(t, key, &v)) { ht_free(t); return 32; }
    }
    mk(key, 2);
    if (ht_del(t, key)) { ht_free(t); return 33; }   /* deleting twice */

    /* 5. re-insert after deletion. */
    for (i = 0; i < 200; i += 2) {
        mk(key, i);
        if (ht_put(t, key, -i) != 0) { ht_free(t); return 22; }
    }
    for (i = 0; i < 200; i += 2) {
        mk(key, i);
        if (!ht_get(t, key, &v) || v != -i) { ht_free(t); return 34; }
    }
    if (ht_count(t) != (size_t)(NKEYS / 2 + 100)) { ht_free(t); return 30; }

    ht_free(t);   /* LeakSanitizer judges this line */
    return 0;
}
PRB

$CC $SAN -o htprobe _probe.c ht.c 2>/dev/null || {
    echo "FAIL: ht.c does not compile as C89 under -Wall -Wextra against lookup.h"; exit 1; }
./htprobe 2>_err.txt
rc=$?
if grep -qE "(AddressSanitizer|LeakSanitizer|runtime error)" _err.txt; then
    echo "FAIL: the sanitizer stopped the run. Its own summary:"
    grep -E "ERROR:|SUMMARY:" _err.txt | sed 's/^/    /' | head -4
    sed 's/^/    /' _err.txt | head -8
    exit 1
fi
case $rc in
  0)  : ;;
  21) echo "FAIL: ht_new(64) returned NULL"; exit 1 ;;
  22) echo "FAIL: ht_put returned -1 on a table that has memory -- the stub, or a resize that fails"; exit 1 ;;
  23) echo "FAIL: after 5,000 distinct puts ht_count is not 5,000"; exit 1 ;;
  24) echo "FAIL: a key that was put is not found, or has the wrong value -- 5,000 keys in 64 buckets collide constantly; check the collision path"; exit 1 ;;
  25) echo "FAIL: an absent key was reported found -- a bucket hit is not a key match; compare the key"; exit 1 ;;
  26) echo "FAIL: putting an existing key did not replace its value"; exit 1 ;;
  27) echo "FAIL: putting an existing key changed the count -- an overwrite is not an insert"; exit 1 ;;
  28) echo "FAIL: the table stored the caller's POINTER, not a copy of the key -- the caller scribbled on its buffer and the table's answer moved with it"; exit 1 ;;
  29) echo "FAIL: ht_del returned 0 for a key that was present"; exit 1 ;;
  30) echo "FAIL: ht_count is wrong after deletions and re-insertions"; exit 1 ;;
  31) echo "FAIL: deleting one key made ANOTHER key unfindable -- under open addressing a blanked slot breaks the probe chain; use a tombstone, a backward shift, or chaining"; exit 1 ;;
  32) echo "FAIL: a deleted key is still found"; exit 1 ;;
  33) echo "FAIL: deleting an already-deleted key returned 1"; exit 1 ;;
  34) echo "FAIL: a key re-inserted after deletion is not found, or has the wrong value"; exit 1 ;;
  *)  echo "FAIL: the probe exited $rc, which is not one of its own codes -- it was killed."
      sed 's/^/    /' _err.txt | head -8; exit 1 ;;
esac

# The bench must build against your table, so the measurement CAN be made.
$CC -std=c89 -pedantic -Wall -Wextra -O2 -o bench bench.c scan.c ht.c 2>/dev/null || {
    echo "FAIL: bench.c does not compile against your ht.c -- the measurement cannot be made"; exit 1; }

# The measurement: shape, never direction.
[ -f MEASURE.md ] || { echo "FAIL: MEASURE.md is missing -- run ./bench and write down what you saw, with the machine and the method"; exit 1; }
for sec in "Machine" "Method" "Results" "Crossover" "Deletion"; do
    grep -q "^## $sec" MEASURE.md || { echo "FAIL: MEASURE.md has no '## $sec' section"; exit 1; }
done
rows=$(awk '/^## Results/{f=1;next} /^## /{f=0} f' MEASURE.md \
       | grep -cE '^\| *[0-9]+ *\| *[0-9]+(\.[0-9]+)? *\| *[0-9]+(\.[0-9]+)? *\|')
if [ "$rows" -lt 3 ]; then
    echo "FAIL: '## Results' needs at least three rows of the form | N | scan ns | hash ns | (found $rows) -- paste what ./bench printed"
    exit 1
fi
cross=$(awk '/^## Crossover/{f=1;next} /^## /{f=0} f' MEASURE.md | grep -c '[^[:space:]]')
if [ "$cross" -eq 0 ]; then
    echo "FAIL: '## Crossover' is empty -- name the N where the hash table started winning on your machine, or say plainly that it never did within the N you tried"
    exit 1
fi
if ! awk '/^## Deletion/{f=1;next} /^## /{f=0} f' MEASURE.md | grep -qiE 'tombstone|backward|back-shift|backshift|rehash|chain|linked list'; then
    echo "FAIL: '## Deletion' must state your position: tombstones, backward shift, rehash, or chaining -- and why"
    exit 1
fi
if ! grep -qiE 'cpu|core|ghz|model|machine|laptop|desktop|arm|x86|apple|intel|amd' MEASURE.md; then
    echo "FAIL: '## Machine' should say what you measured on -- a CPU model or a machine description; a number without its machine is not a measurement"
    exit 1
fi

echo "PASS: the table survives 5,000 colliding keys, deletion and re-insertion under ASan/LSan, and the measurement is recorded with a machine, a method and a crossover"
exit 0
