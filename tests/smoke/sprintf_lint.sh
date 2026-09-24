#!/bin/sh
# smoke lint: no raw sprintf in first-party shipped code (2026-08).
#
# CONTRIBUTING.md's rule is "never call sprintf -- use jc_snprintf"
# (include/jc_snprintf.h), because sprintf writes an unbounded number of
# bytes into a fixed buffer: a format or argument the author did not size
# for is a silent overflow the C89 compiler does not diagnose. The rule
# was enforced only by reviewer memory; this makes it a lint (the house
# principle: prefer a lint to an audit -- an audit finds it once, a lint
# re-runs on every change).
#
# Scope: src/ and include/ (the shipped product), and since M728 the unit
# tests and their C helpers too. tests/ used to be excluded on the grounds
# that "fixtures legitimately use sprintf into known-safe buffers"; M728
# measured otherwise. Thirty-eight calls wrote $TMPDIR paths into fixed
# buffers, one of them `rm -rf %s` into 128 bytes, and a long TMPDIR
# overflowed them (a stack smash under a 327-character one). A fixture path
# is only as short as the TMPDIR it starts with, and nobody sized for that.
# The one first-party exception is the self-contained JSON
# printer, which must stay jc_snprintf-free to remain swappable as a pair
# (CLAUDE.md M171); its three calls write fixed short formats into
# adequately-sized fixed buffers (tmp[8] for a 7-char \uXXXX, tmp[64] for
# a ~20-char %ld/%g) and are allowlisted by exact line with that reason.
. "$(dirname "$0")/_smoke.sh"

t_plan 2
tmp=$(smoke_tmp)

tab=$(printf '\t')
cat > "$tmp/allow" <<EOF
cJSON.c${tab}sprintf(tmp, "\\\\u%04x", (unsigned int)ch);
cJSON.c${tab}sprintf(tmp, "%ld", (long)d);
cJSON.c${tab}sprintf(tmp, "%g", d);
EOF

# The tests' own C is the top level, tests/tools and tests/fuzz. NOT the rest of
# tests/: tests/bench holds task fixtures a model edits and the workspaces a
# benchmark run leaves behind, which are data, and a recursive find met a
# model's own sprintf in one.
targets=$(find "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" \
    \( -name '*.c' -o -name '*.h' \) 2>/dev/null)
tests_c=$(for f in "$SMOKE_ROOT"/tests/*.c "$SMOKE_ROOT"/tests/*.h \
                   "$SMOKE_ROOT"/tests/tools/*.c "$SMOKE_ROOT"/tests/tools/*.h \
                   "$SMOKE_ROOT"/tests/fuzz/*.c "$SMOKE_ROOT"/tests/fuzz/*.h; do
              [ -f "$f" ] && printf '%s\n' "$f"
          done)
nfiles=$(printf '%s\n' "$targets" | grep -c .)
ntests=$(printf '%s\n' "$tests_c" | grep -c .)
targets="$targets
$tests_c"
if [ "$nfiles" -ge 100 ] && [ "$ntests" -ge 151 ]; then
    t_ok "scanning $nfiles product source files and $ntests of the tests' own"
else
    t_fail "scanned $nfiles product files and $ntests test files (floors 100, 151) -- src/include/tests layout moved?"
fi

# Comment-aware scan (skip /* */ blocks and lines starting with *), matching
# a `sprintf(` not preceded by an identifier char (so snprintf/vsnprintf,
# which do not contain the substring "sprintf" anyway, can never match).
awk -v allowf="$tmp/allow" '
BEGIN {
    FS = "\t"
    while ((getline l < allowf) > 0) {
        ti = index(l, "\t")
        if (ti > 0) A[substr(l, 1, ti - 1) SUBSEP substr(l, ti + 1)] = 1
    }
}
FNR == 1 { inblk = 0; name = FILENAME; sub(/.*\//, "", name) }
{
    line = $0
    stripped = line
    sub(/^[ \t]+/, "", stripped); sub(/[ \t]+$/, "", stripped)
    if (inblk) { if (index(stripped, "*/") > 0) inblk = 0; next }
    if (stripped ~ /^\/\*/) { if (index(stripped, "*/") == 0) inblk = 1; next }
    if (stripped ~ /^\*/) next
    code = line
    ci = index(code, "/*"); if (ci > 0) code = substr(code, 1, ci - 1)
    if (code !~ /(^|[^A-Za-z0-9_])sprintf\(/) next
    if ((name SUBSEP stripped) in A) next
    print FILENAME ":" FNR ": " stripped
}' $targets < /dev/null > "$tmp/offenders"

if [ ! -s "$tmp/offenders" ]; then
    t_ok "no raw sprintf outside the audited allowlist (use jc_snprintf)"
else
    t_fail "raw sprintf in first-party code ($(grep -c . "$tmp/offenders")) -- use jc_snprintf:"
    sed 's/^/# /' "$tmp/offenders" | head -20
fi

t_done
