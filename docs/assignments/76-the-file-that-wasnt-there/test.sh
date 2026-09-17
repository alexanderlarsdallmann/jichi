#!/bin/sh
# Task 76 -- reading a file that may not be there. Passes only when ALL hold:
#   1. a MISSING path returns non-zero instead of crashing, and the message on
#      stderr contains the path (a caller cannot name what you did not report);
#   2. a small file reports its REAL length, not a capacity -- the fread return
#      value used, not discarded -- and the buffer is NUL-terminated;
#   3. a file larger than CFG_MAX_BYTES is REFUSED, not silently truncated.
#
# AddressSanitizer is the instrument for (1): the pristine loadcfg.c hands a
# NULL FILE* straight to fread, which traps (verified: SEGV in __GI__IO_fread).
# (2) and (3) are ordinary assertions -- ASan cannot see a wrong length or a
# quiet truncation, and saying so is the point of listing them separately.
#
# The sanitizer grep uses -E and (a|b|c), never "a\|b\|c": BSD grep reads \| as a
# LITERAL pipe, so the GNU spelling would match nothing on the three verified BSD
# rows and this check would be vacuous exactly where nobody looks. posix_utils_lint
# caught it here; the same shape cost this project an OpenBSD row once already.
#
# THE PROBE'S EXIT CODES START AT 21 ON PURPOSE. ASan aborts with exit status 1.
# The first version of this grader numbered its own failures from 1, so a
# sanitizer trap and "reported CFG_OK for a missing file" were the same code --
# and the grader confidently printed the wrong diagnosis at the pristine
# fixture, which is worse than failing, because it sends a learner after a bug
# that is not there. Codes 21+ cannot collide, and a trap is now reported as a
# trap.
#
# HONEST LIMIT. The header requires the size check to happen BEFORE the
# allocation, so a 2 GB file never becomes a 2 GB malloc. A grader cannot
# observe that ordering without a 2 GB file; check 3 proves the refusal, not
# its position. That floor is prose, and the brief says so too.
cd "$(dirname "$0")" || exit 1
trap 'rm -rf cfgprobe _probe.c _fx' EXIT
cc --version >/dev/null 2>&1 || { echo "CANNOT RUN: a C compiler (cc) is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 77; }
CC=${CC:-cc}
SAN="-std=c89 -pedantic -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

rm -rf _fx && mkdir _fx || { echo "FAIL: cannot create the fixture directory"; exit 1; }
# A small file whose length is NOT the cap, so a loader reporting its capacity
# instead of the real byte count is caught.
printf 'name=jichi\nmode=strict\n' > _fx/small.conf
SMALL=$(wc -c < _fx/small.conf | tr -d ' ')
# One byte over the cap.
awk 'BEGIN{ for (i = 0; i < 65537; i++) printf "x" }' > _fx/big.conf

cat > _probe.c <<'PRB'
#include "loadcfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *buf;
    long  len;
    int   rc;

    if (argc != 5) return 99;

    /* 1. a path that is not there: must RETURN, not crash. */
    buf = (char *)1; len = -1;
    rc = cfg_load(argv[1], &buf, &len);
    if (rc == CFG_OK)  return 21;  /* reported success for a missing file */
    if (buf != NULL)   return 22;  /* *out not cleared on failure         */
    if (len != 0)      return 23;  /* *len not cleared on failure         */

    /* 2. a small file: real length, NUL-terminated, contents intact. */
    buf = NULL; len = -1;
    rc = cfg_load(argv[2], &buf, &len);
    if (rc != CFG_OK)  return 24;
    if (buf == NULL)   return 25;
    /* Every early return below frees first. Without that, LeakSanitizer fires
       at exit and reports the PROBE's leak instead of the defect being tested --
       which cost two wrong diagnoses while this grader was being written. */
    if (len != atol(argv[3]))                { free(buf); return 26; }
    if (buf[len] != '\0')                    { free(buf); return 27; }
    if (strncmp(buf, "name=jichi", 10) != 0) { free(buf); return 28; }
    free(buf);

    /* 3. a file over the cap: refused, not truncated. */
    buf = (char *)1; len = -1;
    rc = cfg_load(argv[4], &buf, &len);
    if (rc == CFG_OK) {
        free(buf);                 /* or LeakSanitizer reports the probe's
                                      own leak instead of your defect */
        return 29;                 /* silently truncated a too-big file */
    }
    if (buf != NULL)   return 30;
    if (len != 0)      return 31;

    return 0;
}
PRB

$CC $SAN -o cfgprobe _probe.c loadcfg.c 2>/dev/null || {
    echo "FAIL: loadcfg.c does not compile as C89 under -Wall -Wextra"; exit 1; }

./cfgprobe _fx/absent.conf _fx/small.conf "$SMALL" _fx/big.conf 2>_fx/err.txt
rc=$?

# A sanitizer trap first: it is the loudest failure and must not be read as one
# of the probe's own codes.
# A sanitizer report first: it is the loudest failure and must not be read as
# one of the probe's own codes. The message deliberately does NOT name a cause --
# an earlier version asserted "fopen's NULL return is not checked", which is true
# of the pristine fixture and FALSE of every half-fix that trips a different
# check. A grader that explains a failure it has not diagnosed is worse than one
# that reports it, so the SUMMARY line speaks instead.
if grep -qE "(AddressSanitizer|LeakSanitizer|runtime error)" _fx/err.txt; then
    echo "FAIL: the sanitizer stopped the run. Its own summary:"
    grep -E "ERROR:|SUMMARY:" _fx/err.txt | sed 's/^/    /' | head -4
    echo "  (full report below)"
    sed 's/^/    /' _fx/err.txt | head -8
    exit 1
fi

case $rc in
  0)  : ;;
  21) echo "FAIL: a missing file reported CFG_OK -- fopen's NULL return is not checked"; exit 1 ;;
  22|23) echo "FAIL: on failure *out must be NULL and *len 0 -- a caller that trusts them will free garbage"; exit 1 ;;
  24|25) echo "FAIL: a perfectly good small file was not loaded"; exit 1 ;;
  26) echo "FAIL: *len is not the file's real length -- fread RETURNS how much it read; use it"; exit 1 ;;
  27) echo "FAIL: the buffer is not NUL-terminated at *len"; exit 1 ;;
  28) echo "FAIL: the contents came back wrong"; exit 1 ;;
  29) echo "FAIL: a file larger than CFG_MAX_BYTES was accepted -- silently truncating is worse than refusing"; exit 1 ;;
  30|31) echo "FAIL: on CFG_ETOOBIG *out must be NULL and *len 0"; exit 1 ;;
  99) echo "FAIL: probe called wrongly (grader bug, not yours)"; exit 1 ;;
  *)  echo "FAIL: the probe exited $rc, which is not one of its own codes -- it was killed."
      sed 's/^/    /' _fx/err.txt | head -8; exit 1 ;;
esac

# The message must name the path, or the user cannot act on it.
if ! grep -q "absent.conf" _fx/err.txt; then
    echo "FAIL: nothing on stderr names the missing path -- 'cannot open file' does not tell a user WHICH file"
    exit 1
fi

echo "PASS: missing, short, and oversized inputs are all handled, and the message names the path"
exit 0
