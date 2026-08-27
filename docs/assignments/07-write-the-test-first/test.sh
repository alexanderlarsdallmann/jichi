#!/bin/sh
# Runner for the make-it-fail-first task. Passes only when BOTH hold:
#   1. the learner's test_clamp.c exists, compiles, and passes, and
#   2. an acceptance probe confirms the bug in clamp.c is actually fixed.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test_clamp.c ]; then
    echo "1..1"
    echo "not ok 1 - test_clamp.c is missing: write the failing test first"
    exit 1
fi
cc -std=c89 -pedantic -Wall -Wextra -o run_learner clamp.c test_clamp.c || exit 1
./run_learner || exit 1

cat > _accept.c <<'ACCEPT'
#include <stdio.h>
#include "clamp.h"
int main(void)
{
    if (clamp(9, 0, 5) != 5) {
        printf("not ok - acceptance: clamp(9,0,5) should be 5\n");
        return 1;
    }
    if (clamp(-3, 0, 5) != 0 || clamp(2, 0, 5) != 2) {
        printf("not ok - acceptance: in-range/low cases\n");
        return 1;
    }
    printf("ok - acceptance\n");
    return 0;
}
ACCEPT
cc -std=c89 -pedantic -o run_accept clamp.c _accept.c || exit 1
./run_accept
