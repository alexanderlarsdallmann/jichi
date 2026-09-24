#!/bin/sh
# smoke lint: the unit suite removes its fixtures without a shell, and only
# below $TMPDIR (M728).
#
# THE DEFECT THIS EXISTS FOR. Twenty-five sites in tests/*.c built
# `rm -rf %s` into a fixed buffer and handed it to system(). A buffer cuts a
# path short, and a cut path is a PREFIX: under a 127-character TMPDIR,
# tests/test_bounds.c's 128-byte `home` came out as the TMPDIR itself, and a
# traced run executed `rm -rf "$TMPDIR"` four times. A TMPDIR whose 127th
# character is a `/` cut to its PARENT. A space split the argument in two.
# The gate runs with TMPDIR unset, where nothing truncates, so nothing ever
# fired; a canary in a 127-character TMPDIR was deleted by the old suite and
# kept by the new one.
#
# The fix is jc_test_rm_rf (tests/test_main.c): no command line at all, and
# a refusal -- which fails the suite -- for any path not strictly below
# jc_test_tmpdir(). And main() refuses a TMPDIR longer than the length the
# fixtures are measured clean under. This holds all three in place.
#
# WHAT IS AND IS NOT COVERED, stated rather than implied:
#   checked -- no C string literal in tests/*.c BEGINS with an `rm -r `,
#              `rm -fr ` or `/bin/rm -rf ` command and its argument (the form
#              all 25 sites had; the phrase alone, "rm -rf", is data);
#              the helper exists and applies the refusal; at least 29 call
#              sites use it (today's count); main() carries the TMPDIR guard;
#              and `make ci` runs the suite past that limit and, since M729,
#              on a TMPDIR it cannot write.
#   NOT checked -- a removal spelled some other way (`"exec rm ..."`, a
#              command assembled from pieces, an unlink() loop by hand). The
#              matcher is proven below on a planted positive and on the test
#              DATA strings that must not match ("sudo rm -rf /" and the like
#              are inputs to the code under test, not commands).
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
T="$SMOKE_ROOT/tests"

# Comment-aware: skip /* */ blocks and lines that start with `*`, and cut a
# trailing /* comment; then flag a literal that starts with an rm command.
scan() {
    awk '
    FNR == 1 { inblk = 0 }
    {
        line = $0; s = line
        sub(/^[ \t]+/, "", s)
        if (inblk) { if (index(s, "*/") > 0) inblk = 0; next }
        if (s ~ /^\/\*/) { if (index(s, "*/") == 0) inblk = 1; next }
        if (s ~ /^\*/) next
        ci = index(line, "/*"); if (ci > 0) line = substr(line, 1, ci - 1)
        if (line ~ /(^|[^\\])"(\/bin\/)?rm -(r|fr|rf) /) print FILENAME ":" FNR ": " s
    }' "$@" < /dev/null
}

# --- 1: the matcher fires on a planted template and spares test data -------
cat > "$tmp/planted.c" <<'EOF'
    jc_snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    jc_snprintf(cmd, sizeof cmd, "/bin/rm -fr %s", dir);
EOF
cat > "$tmp/data.c" <<'EOF'
    JC_CHECK(jc_priv_allowlisted("sudo rm -rf /", allow, n) == 0);
    JC_CHECK(count("{\"command\":\"rm -rf /\"}", p, 4) == 0);
    JC_CHECK(strstr(pfx, "rm -rf") == NULL);
    /* a comment quoting "rm -rf %s" is history, not a command */
EOF
_pos=$(scan "$tmp/planted.c" | grep -c .)
_neg=$(scan "$tmp/data.c" | grep -c .)
if [ "$_pos" -eq 2 ] && [ "$_neg" -eq 0 ]; then
    t_ok "the matcher flags both planted templates and none of the test-data strings"
else
    t_fail "matcher: $_pos of 2 planted templates flagged, $_neg of 0 data strings -- the scan below cannot be trusted"
fi

# --- 2: no rm command template in the unit tests ---------------------------
_files=$(ls "$T"/*.c 2>/dev/null)
_nf=$(printf '%s\n' "$_files" | grep -c .)
scan $_files > "$tmp/offenders"
if [ "$_nf" -ge 130 ] && [ ! -s "$tmp/offenders" ]; then
    t_ok "no rm command template in the $_nf unit-test sources"
else
    t_fail "rm command templates in the unit tests ($(grep -c . "$tmp/offenders"), $_nf files scanned; floor 130) -- use jc_test_rm_rf:"
    sed 's/^/# /' "$tmp/offenders" | head -10
fi

# --- 3: the helper exists and applies the refusal --------------------------
_main="$T/test_main.c"
if grep -q '^int jc_test_rm_rf(const char \*path)' "$_main" &&
   grep -q '^int jc_test_rm_rf_allowed(const char \*path)' "$_main" &&
   grep -q 'if (!jc_test_rm_rf_allowed(path)) {' "$_main" &&
   grep -q 'jc_test_fails++;' "$_main"; then
    t_ok "jc_test_rm_rf is defined in test_main.c and refuses through jc_test_rm_rf_allowed"
else
    t_fail "tests/test_main.c no longer defines jc_test_rm_rf with its refusal (M728)"
fi

# --- 4: the tests use it ---------------------------------------------------
_uses=$(cat $(printf '%s\n' "$_files" | grep -v -e '/test_main\.c$' -e '/test_harness\.c$') |
        grep -o 'jc_test_rm_rf(' | grep -c .)
if [ "$_uses" -ge 29 ]; then
    t_ok "$_uses fixture removals go through jc_test_rm_rf (floor 29)"
else
    t_fail "only $_uses call sites use jc_test_rm_rf (floor 29) -- a removal moved back to a shell?"
fi

# --- 5: main() refuses a TMPDIR the fixtures were not measured under --------
if grep -q '^#define JC_TEST_TMPDIR_MAX [0-9]' "$_main" &&
   grep -q 'if (strlen(jc_test_tmpdir()) > JC_TEST_TMPDIR_MAX) {' "$_main"; then
    t_ok "main() refuses a TMPDIR longer than JC_TEST_TMPDIR_MAX ($(sed -n 's/^#define JC_TEST_TMPDIR_MAX \([0-9]*\).*/\1/p' "$_main"))"
else
    t_fail "tests/test_main.c lost the TMPDIR length guard (M728)"
fi

# --- 6: the gate runs the suite where the fixtures cannot be written (M729) --
# Two lines in `make ci` hold the environment side: the suite refuses a TMPDIR
# past its measured limit, and it COMPLETES -- a summary line, rc 1, nothing
# new in the directory it ran from -- on a TMPDIR it cannot write. Either line
# deleted, and the gate is back to running only where TMPDIR is /tmp.
_mk="$SMOKE_ROOT/Makefile"
if grep -q 'JC_TEST_TMPDIR_MAX' "$_mk" && grep -q 'TMPDIR=\$\$t.file/x \./\$(TEST)' "$_mk" &&
   grep -q 'cmp -s \$\$t.before \$\$t.after' "$_mk"; then
    t_ok "make ci runs the suite past its TMPDIR limit and on a TMPDIR it cannot write"
else
    t_fail "the Makefile lost a TMPDIR line of make ci: the length refusal (M728) or the unwritable run (M729)"
fi

t_done
