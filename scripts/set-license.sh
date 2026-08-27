#!/bin/sh
# set-license.sh -- stamp the chosen licence across the whole tree (M497).
#
# WHY THIS EXISTS. 476 source files carry an SPDX line. When the licensing
# decision lands, "publish it" must not mean "hand-edit 476 headers and hope
# none was missed", because a file left saying LicenseRef-UNDECIDED after a
# release is worse than one that never had a header: it contradicts the LICENSE
# file, and a scanner that trusts headers will report the tree as unlicensed.
# So the sweep is a script, and tests/smoke/license_lint.sh fails the tier if
# anything was left behind.
#
# WHAT IT CHANGES, AND WHAT IT DELIBERATELY DOES NOT. It rewrites the token only
# on lines that ASSERT this tree's licence -- the SPDX-License-Identifier headers
# and the JC_LICENSE_SPDX define. Prose that MENTIONS the token (this file, the
# lint, docs/licenses/README.md, docs/LICENSING.md) is left alone: a script that
# rewrote the explanation of the mechanism would corrupt it. The prose that does
# need a human is listed at the end.
#
# It does not commit, and it does not push. A change touching every file in the
# tree gets read before it is committed.
#
# usage: scripts/set-license.sh [-n] <spdx-id>
#        -n, --dry-run   report what would change; touch nothing
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DRY=no
ME=set-license

usage() {
    echo "usage: scripts/set-license.sh [-n] <spdx-id>" >&2
    echo "  e.g. scripts/set-license.sh Apache-2.0" >&2
    echo "  candidates with a verbatim text in docs/licenses:" >&2
    for _c in "$ROOT"/docs/licenses/*.txt; do
        [ -f "$_c" ] || continue
        _b=${_c##*/}
        echo "    ${_b%.txt}" >&2
    done
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--dry-run) DRY=yes; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) echo "$ME: unknown option '$1'" >&2; usage; exit 2 ;;
        *) break ;;
    esac
done

ID=$1
[ -n "$ID" ] || { echo "$ME: no SPDX identifier given" >&2; usage; exit 2; }

# The SPDX identifier charset. Rejecting anything else keeps a shell-quoting
# accident or a pasted sentence out of 476 file headers.
case "$ID" in
    *[!A-Za-z0-9.+-]*)
        echo "$ME: '$ID' is not a plausible SPDX identifier" >&2; exit 2 ;;
esac
case "$ID" in
    LicenseRef-UNDECIDED)
        echo "$ME: that is the placeholder, not a licence" >&2; exit 2 ;;
esac

TEXT="$ROOT/docs/licenses/$ID.txt"
if [ ! -f "$TEXT" ]; then
    echo "$ME: no verbatim text at docs/licenses/$ID.txt" >&2
    echo "$ME: refusing to write a LICENSE whose contents it would have to" >&2
    echo "$ME: invent. Add the text first -- docs/licenses/README.md says how." >&2
    exit 1
fi

# Portable sha256: GNU coreutils, perl's shasum, BSD sha256, then openssl.
sha256_of() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif command -v sha256 > /dev/null 2>&1; then
        sha256 -q "$1"
    elif command -v openssl > /dev/null 2>&1; then
        openssl dgst -sha256 "$1" | sed 's/.*= *//'
    else
        echo ""
    fi
}

# A licence text that has drifted is not that licence. This is the one check
# that cannot be deferred to review, because nobody reads 11 KB of legal text
# in a diff.
SUMS="$ROOT/docs/licenses/SHA256SUMS"
_want=$(grep " ${ID}.txt\$" "$SUMS" 2>/dev/null | cut -d' ' -f1)
_have=$(sha256_of "$TEXT")
if [ -z "$_want" ]; then
    echo "$ME: no checksum for $ID.txt in docs/licenses/SHA256SUMS" >&2
    exit 1
fi
if [ -z "$_have" ]; then
    echo "$ME: no sha256 tool on this host; cannot verify the licence text" >&2
    exit 1
fi
if [ "$_want" != "$_have" ]; then
    echo "$ME: docs/licenses/$ID.txt does not match its recorded checksum" >&2
    echo "$ME:   recorded $_want" >&2
    echo "$ME:   actual   $_have" >&2
    exit 1
fi

# Re-running with the same identifier is fine (the sweep is idempotent). A
# DIFFERENT licence already in place is switched ONLY when the tree is in a
# consistent decided state (LICENSE matches the current identifier's candidate
# text, so the switch is provably from-A-to-B); anything else -- a hand-edited
# LICENSE, a text matching no candidate -- is a decision this script will not
# silently overwrite (M619: the review may deliberately switch Apache-2.0 to
# MIT, and that switch must be the same one command as the first decision).
CUR=$(sed -n 's|^#define JC_LICENSE_SPDX "\(.*\)"$|\1|p' \
    "$ROOT/include/jc_license.h")
SWEEP_FROM="LicenseRef-UNDECIDED"
if [ -f "$ROOT/LICENSE" ] && ! cmp -s "$ROOT/LICENSE" "$TEXT"; then
    if [ -n "$CUR" ] && [ "$CUR" != "LicenseRef-UNDECIDED" ] &&
       [ -f "$ROOT/docs/licenses/$CUR.txt" ] &&
       cmp -s "$ROOT/LICENSE" "$ROOT/docs/licenses/$CUR.txt"; then
        SWEEP_FROM="$CUR"
        echo "$ME: switching $CUR -> $ID (deliberate; the tree is consistent)"
    else
        echo "$ME: LICENSE already exists and is not the $ID text" >&2
        echo "$ME: remove or replace it deliberately, then re-run" >&2
        exit 1
    fi
fi

# ---- the sweep -------------------------------------------------------------
# Every C source under src/, include/ and tests/. include/jc_buildrev_stamp.h is
# generated and gitignored, so it is regenerated rather than edited.
files=$(find "$ROOT/src" "$ROOT/include" "$ROOT/tests" \
    \( -name '*.c' -o -name '*.h' \) 2>/dev/null \
    | grep -v 'jc_buildrev_stamp.h' | sort)

n_hdr=0
n_def=0
for f in $files; do
    grep -q "$SWEEP_FROM" "$f" 2>/dev/null || continue
    if grep -q "SPDX-License-Identifier: $SWEEP_FROM" "$f"; then
        n_hdr=$((n_hdr + 1))
    fi
    if grep -q "JC_LICENSE_SPDX \"$SWEEP_FROM\"" "$f"; then
        n_def=$((n_def + 1))
    fi
    [ "$DRY" = yes ] && continue
    sed -e "/SPDX-License-Identifier:/s|$SWEEP_FROM|$ID|" \
        -e "/JC_LICENSE_SPDX/s|$SWEEP_FROM|$ID|" \
        "$f" > "$f.setlic" && mv "$f.setlic" "$f"
done

if [ "$n_hdr" -eq 0 ] && [ "$n_def" -eq 0 ]; then
    echo "$ME: nothing to sweep -- no file says $SWEEP_FROM."
    echo "$ME: (already released? check include/jc_license.h)"
fi

if [ "$DRY" = yes ]; then
    echo "$ME: DRY RUN, nothing written."
    echo "$ME:   would rewrite $n_hdr SPDX header(s) and $n_def define(s) to $ID"
    echo "$ME:   would copy docs/licenses/$ID.txt to LICENSE"
    [ -f "$ROOT/docs/licenses/NOTICE.$ID" ] && \
        echo "$ME:   would copy docs/licenses/NOTICE.$ID to NOTICE"
    exit 0
fi

cp "$TEXT" "$ROOT/LICENSE"
echo "$ME: wrote LICENSE ($ID, $(wc -c < "$ROOT/LICENSE" | tr -d ' ') bytes)"
echo "$ME: rewrote $n_hdr SPDX header(s) and $n_def define(s)"

# Apache-2.0 section 4(d) propagates a NOTICE file if one exists; a licence with
# no template here simply gets none.
if [ -f "$ROOT/docs/licenses/NOTICE.$ID" ]; then
    cp "$ROOT/docs/licenses/NOTICE.$ID" "$ROOT/NOTICE"
    echo "$ME: wrote NOTICE (required to be propagated by $ID)"
elif [ -f "$ROOT/NOTICE" ]; then
    # M619: a NOTICE left over from a licence that propagated one would ride
    # along under a licence that does not name it -- remove it on a switch.
    rm -f "$ROOT/NOTICE"
    echo "$ME: removed the previous licence's NOTICE ($ID propagates none)"
fi

# docs/LICENSING.md carries ONE machine-checkable row -- the identifier in force
# -- and license_lint check 10 reads it back. It is rewritten here rather than
# left to the prose list below, because "release is one command" must include the
# page a reader is sent to; the REST of that page is prose and stays a human's
# job, which is why the file is named first in the list that follows.
LMD="$ROOT/docs/LICENSING.md"
ROW='| SPDX identifier in force |'
if grep -q "^$ROW" "$LMD" 2>/dev/null; then
    sed "s#^| SPDX identifier in force | .* |\$#| SPDX identifier in force | \`$ID\` |#" \
        "$LMD" > "$LMD.setlic" && mv "$LMD.setlic" "$LMD"
    echo "$ME: updated the identifier row in docs/LICENSING.md"
else
    echo "$ME: WARNING: no 'SPDX identifier in force' row in docs/LICENSING.md;" >&2
    echo "$ME: license_lint check 10 will fail until that page is fixed" >&2
fi

# ---- what a human still has to do ------------------------------------------
echo ""
echo "$ME: PROSE STILL MENTIONING THE PLACEHOLDER -- read each, edit the ones"
echo "$ME: that make a CLAIM. The rest of docs/LICENSING.md's 'Current state'"
echo "$ME: table and README.md's License section are the two that must change;"
echo "$ME: the lint and docs/licenses/README.md name the token on purpose and"
echo "$ME: are correct as they stand:"
grep -rl 'LicenseRef-UNDECIDED' "$ROOT" 2>/dev/null \
    | grep -v '/\.git/' | sed "s|^$ROOT/|  |" | sort
echo ""
echo "$ME: then, in order:"
echo "$ME:   make clean && make -j4 WERROR=1 && make ci"
echo "$ME:   ./jichi --version        # must print licence: $ID"
echo "$ME:   git add -A && git commit -m 'M___: release under $ID'"
