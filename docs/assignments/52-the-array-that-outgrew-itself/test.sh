#!/bin/sh
# The make-it-fail-first task, in C. Passes only when ALL hold:
#   1. you wrote test_ivec.c, and it compiles, runs clean under ASan, and exits 0;
#   2. it actually tests something (>= 3 assert()s -- a hollow suite is not proof);
#   3. an independent acceptance probe -- which pushes well past the initial
#      capacity -- runs clean under ASan against your ivec.c.
# AddressSanitizer is the instrument: the overflow hides past the 4th push, so
# only a test that grows the vector (and a real grow) survives the sanitizer.
cd "$(dirname "$0")" || exit 1
trap 'rm -f ivtest ivaccept _accept.c' EXIT
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

if [ ! -f test_ivec.c ]; then
    echo "FAIL: write the failing test first -- test_ivec.c is missing"; exit 1; fi
$CC $SAN -o ivtest test_ivec.c ivec.c 2>/dev/null || {
    echo "FAIL: test_ivec.c + ivec.c do not compile"; exit 1; }
./ivtest >/dev/null 2>&1 || { echo "FAIL: your tests do not pass (or ASan trapped)"; exit 1; }
n=$(grep -cE 'assert[[:space:]]*\(' test_ivec.c)
[ "$n" -ge 3 ] || { echo "FAIL: only $n assert()s -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: push far past the initial capacity and read it all back.
cat > _accept.c <<'ACC'
#include "ivec.h"
#include <assert.h>
int main(void)
{
    ivec v; int i;
    ivec_init(&v);
    for (i = 0; i < 50; i++) ivec_push(&v, i * i);
    for (i = 0; i < 50; i++) assert(ivec_get(&v, (size_t)i) == i * i);
    ivec_free(&v);
    return 0;
}
ACC
$CC $SAN -o ivaccept _accept.c ivec.c 2>/dev/null
crc=$?
if [ $crc -eq 0 ]; then ./ivaccept >/dev/null 2>&1; crc=$?; fi
rm -f _accept.c ivaccept
[ $crc -eq 0 ] || { echo "FAIL: ivec still overflows once it grows past the initial capacity"; exit 1; }
echo "PASS: the failing test was written, and the vector grows safely"
exit 0
